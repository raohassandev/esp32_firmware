#include "commissioning_gate_api.h"

#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_err.h"
#include "http_json.h"
#include "inverter_manager.h"
#include "inverter_write_confirmation.h"
#include "write_provenance_api.h"

/* Every header value below is a string literal with static storage duration.
 * httpd_resp_set_hdr() stores the pointer and does NOT copy, so a value that
 * did not outlive the response would be a use-after-free. */
static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static void add_finite(cJSON *root, const char *name, float value)
{
    if (isfinite(value)) cJSON_AddNumberToObject(root, name, value);
    else cJSON_AddNullToObject(root, name);
}

/*
 * GET /api/commissioning/gate
 *
 * States, per enumerated prerequisite, whether it is satisfied and - when it is
 * not - exactly why, in the firmware's own words. The interface never has to
 * infer a safety decision it did not make.
 */
static esp_err_t gate_get(httpd_req_t *request)
{
    commissioning_status_t status;
    control_engine_get_commissioning(&status);
    control_status_t control = {0};
    control_engine_get_status(&control);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    cJSON_AddBoolToObject(root, "modbus_io_in_http_handler", false);
    cJSON_AddBoolToObject(root, "commissioned", status.commissioned);
    /* Scope travels with the verdict. "commissioned": true on its own is not a
     * meaningful answer -- it does not say whether the plant behind it is real. */
    cJSON_AddStringToObject(root, "scope", commissioning_scope_label(status.scope));
    cJSON_AddBoolToObject(root, "lab_simulator_mode",
                          status.scope == COMMISSIONING_SCOPE_LAB);
    cJSON_AddBoolToObject(root, "production_qualified",
                          status.scope == COMMISSIONING_SCOPE_PRODUCTION);
    if (status.scope == COMMISSIONING_SCOPE_LAB) {
        cJSON_AddStringToObject(root, "scope_notice",
                                "Commissioned for lab validation only: at least one commanded "
                                "inverter is a declared Modbus simulator. Physical readback "
                                "qualification on real equipment has not been performed.");
    }
    cJSON_AddNumberToObject(root, "prerequisite_count", COMMISSIONING_PREREQ_COUNT);
    cJSON_AddNumberToObject(root, "satisfied_count", status.satisfied_count);
    cJSON_AddNumberToObject(root, "unmet_count", status.unmet_count);
    if (status.commissioned) {
        cJSON_AddNullToObject(root, "first_unmet");
    } else {
        cJSON_AddStringToObject(root, "first_unmet",
                                commissioning_prereq_id(status.first_unmet));
    }
    cJSON_AddStringToObject(root, "summary", commissioning_gate_summary(&status));

    /* The consequence, stated plainly next to the cause. */
    cJSON_AddBoolToObject(root, "automatic_control_permitted",
                          status.commissioned && control.command_authority);
    cJSON_AddBoolToObject(root, "command_authority", control.command_authority);
    cJSON_AddStringToObject(root, "inhibit_reason", control.inhibit_reason);

    /* PREREQUISITE ENABLE REGISTERS, published unconditionally and next to the
     * write-confirmation fault rather than merged with it.
     *
     * Unconditional for the same reason `scope` above is unconditional: a client
     * that can read one verdict field must be able to read this one, or an
     * inverter will be missing from the commandable fleet with no stated reason.
     *
     * Reported side by side because the two faults are indistinguishable from
     * outside -- both end with the inverter excluded -- and have nothing in
     * common as remedies:
     *
     *   write_confirmation_fault  - the SETPOINT register read back the wrong
     *                               value, or could not be read.
     *   prerequisite_enable_fault - the ENABLE register is not confirmed to
     *                               hold. The setpoint is accepted, echoed back
     *                               and ignored, so its readback looks perfect
     *                               while the inverter runs unlimited.
     *
     * Both predicates read already-acquired state; no Modbus I/O happens here. */
    inverter_fleet_commissioning_t fleet;
    inverter_manager_commissioning_summary(&fleet);
    cJSON_AddBoolToObject(root, "write_confirmation_fault",
                          inverter_manager_write_confirmation_fault());
    cJSON_AddBoolToObject(root, "prerequisite_enable_fault",
                          inverter_manager_prerequisite_enable_fault());
    /* Enabled inverters whose profile says the device needs an enable register. */
    cJSON_AddNumberToObject(root, "prerequisite_required_count",
                            fleet.prerequisite_required_count);
    /* Of those, the ones not currently confirmed by a READ, and therefore out of
     * the commandable fleet right now. Transient: the background task retries.
     * Includes every inverter that simply has not been read yet, because unknown
     * is not confirmed. */
    cJSON_AddNumberToObject(root, "prerequisite_unconfirmed_count",
                            fleet.prerequisite_unconfirmed_count);
    /* Of those, the ones whose profile cannot describe a writable AND readable
     * prerequisite. Permanent: polling will never resolve it, and the remedy is a
     * manual citation for the register and its readback. */
    cJSON_AddNumberToObject(root, "prerequisite_unverifiable_count",
                            fleet.prerequisite_unverifiable_count);
    cJSON_AddStringToObject(root, "prerequisite_notice",
                            "An unconfirmed prerequisite enable register is not a setpoint "
                            "fault and does not appear in the setpoint readback. The setpoint "
                            "is accepted, echoed back and then ignored, so the readback reads "
                            "as a perfect match while the inverter keeps generating at full "
                            "output. Unconfirmed is transient and retried; unverifiable is "
                            "permanent and needs a manual citation for the register and its "
                            "readback.");

    /* WHAT CONFIRMED THE WRITES, published unconditionally beside the verdict.
     *
     * write_confirmation_fault above says whether a write could not be confirmed.
     * It does NOT say what confirmed the ones that were, and since plant-level
     * logger control landed there are two answers with very different weight: a
     * limit demonstrated by measured power, and an echo of a stored command. A
     * client shown only the verdict would print "confirmed" for both.
     *
     * Unconditional for the same reason `scope` above is unconditional: a client
     * that can read one verdict field must be able to read this one. Reads
     * already-acquired snapshots; no Modbus I/O happens here. */
    write_provenance_rollup_t provenance;
    write_provenance_collect(&provenance);
    write_provenance_add_fleet(root, &provenance);

    cJSON *items = cJSON_AddArrayToObject(root, "prerequisites");
    if (!items) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    for (uint8_t i = 0; i < COMMISSIONING_PREREQ_COUNT; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "id", commissioning_prereq_id(i));
        cJSON_AddStringToObject(item, "title", commissioning_prereq_title(i));
        cJSON_AddBoolToObject(item, "satisfied", status.results[i].satisfied);
        cJSON_AddStringToObject(item, "reason",
                                commissioning_reason_id(status.results[i].reason));
        cJSON_AddStringToObject(item, "detail",
                                commissioning_reason_message(status.results[i].reason));
        cJSON_AddItemToArray(items, item);
    }
    return send_json(request, root);
}

/*
 * One stable slug for an inverter's prerequisite enable register. Four answers,
 * and the distinction between the middle two is the whole point:
 *
 *   not_required  - the profile says this device honours a setpoint without an
 *                   enable register. Nothing to arm, nothing to report.
 *   confirmed     - a READ of the enable register found the required value. Only
 *                   a read produces this; an accepted write never does.
 *   unverifiable  - PERMANENT. The profile cannot describe a writable AND
 *                   readable prerequisite, so this inverter is refused write
 *                   authority outright. Polling will never change it; the remedy
 *                   is a manual citation for the register and its readback.
 *   unconfirmed   - TRANSIENT. It needs one, it can be verified, and it is not
 *                   confirmed right now. The background task keeps retrying.
 *
 * Unverifiable and unconfirmed must never be reported as the same thing: one
 * needs a human with a manual, the other needs nothing but time. Fail-closed
 * ordering - anything not positively confirmed comes back as unconfirmed, so a
 * zeroed struct cannot read as permission.
 */
static const char *prerequisite_state_slug(const inverter_data_t *data)
{
    if (!data) return "unconfirmed";
    if (!data->prerequisite_required) return "not_required";
    if (data->prerequisite_unverifiable) return "unverifiable";
    if (data->prerequisite_satisfied) return "confirmed";
    return "unconfirmed";
}

/*
 * GET /api/inverters/write-confirmation
 *
 * Per-inverter setpoint confirmation. Every value here was acquired by the
 * background telemetry task; this handler performs no Modbus I/O.
 */
static esp_err_t confirmation_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    cJSON_AddBoolToObject(root, "modbus_io_in_http_handler", false);
    cJSON_AddBoolToObject(root, "read_only_endpoint", true);
    cJSON_AddBoolToObject(root, "writes_issued", false);
    cJSON_AddStringToObject(root, "fleet_state",
                            inverter_write_state_name(
                                inverter_manager_fleet_write_confirmation()));
    cJSON_AddBoolToObject(root, "confirmation_fault",
                          inverter_manager_write_confirmation_fault());
    /* The prerequisite fault is a SEPARATE fleet answer from confirmation_fault
     * above and is never folded into it. Published unconditionally so a client
     * that can read the setpoint verdict can always read the enable verdict too. */
    inverter_fleet_commissioning_t fleet;
    inverter_manager_commissioning_summary(&fleet);
    cJSON_AddBoolToObject(root, "prerequisite_enable_fault",
                          inverter_manager_prerequisite_enable_fault());
    cJSON_AddNumberToObject(root, "prerequisite_required_count",
                            fleet.prerequisite_required_count);
    cJSON_AddNumberToObject(root, "prerequisite_unconfirmed_count",
                            fleet.prerequisite_unconfirmed_count);
    cJSON_AddNumberToObject(root, "prerequisite_unverifiable_count",
                            fleet.prerequisite_unverifiable_count);
    cJSON_AddStringToObject(root, "prerequisite_notice",
                            "A setpoint state on this page says only what the SETPOINT "
                            "register reported. It cannot detect an unarmed enable register: "
                            "the setpoint is accepted, echoed back and ignored, so it reads "
                            "as confirmed while the inverter ignores the limit. Read the "
                            "prerequisite state on each inverter as well.");

    /* Fleet provenance, beside fleet_state rather than instead of it.
     * fleet_state says WHETHER the fleet is confirmed; these keys say what that
     * rests on. Unconditional: a client that can read the first must be able to
     * read the second, or "confirmed" is unreadable. */
    write_provenance_rollup_t provenance;
    write_provenance_collect(&provenance);
    write_provenance_add_fleet(root, &provenance);

    uint8_t count = inverter_manager_get_count();
    cJSON_AddNumberToObject(root, "count", count);
    cJSON *items = cJSON_AddArrayToObject(root, "inverters");
    if (!items) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    for (uint8_t i = 0; i < count; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddStringToObject(item, "state",
                                inverter_write_state_name(
                                    (inverter_write_state_t)data.write_confirmation));
        cJSON_AddBoolToObject(item, "write_issued", data.write_issued);
        cJSON_AddBoolToObject(item, "last_write_accepted", data.last_write_accepted);
        cJSON_AddBoolToObject(item, "confirmation_fault", data.confirmation_fault);
        cJSON_AddBoolToObject(item, "mismatch", data.command_mismatch);
        /* Requested is what was written; commanded is what a readback actually
         * confirmed. They are reported separately on purpose. */
        if (data.write_issued) add_finite(item, "requested_percent", data.requested_percent);
        else cJSON_AddNullToObject(item, "requested_percent");
        if (data.has_command) add_finite(item, "confirmed_percent", data.commanded_percent);
        else cJSON_AddNullToObject(item, "confirmed_percent");
        if (data.has_readback) add_finite(item, "readback_percent", data.readback_percent);
        else cJSON_AddNullToObject(item, "readback_percent");
        cJSON_AddNumberToObject(item, "confirmed_count", data.confirmed_count);
        cJSON_AddNumberToObject(item, "unverified_count", data.unverified_count);
        cJSON_AddNumberToObject(item, "mismatch_count", data.mismatch_count);
        cJSON_AddNumberToObject(item, "write_successes", data.write_successes);
        cJSON_AddNumberToObject(item, "write_errors", data.write_errors);

        /* PER-INVERTER PROVENANCE. Placed immediately after the verdict and the
         * three percent figures, because it is what says whether the verdict
         * above is a demonstrated limit or an echo of a stored command. */
        write_provenance_add_inverter(item, &data);

        /* PER-INVERTER PREREQUISITE STATE. Nested in its own object so it can
         * never be mistaken for one of the four setpoint confirmation states
         * above: this inverter can be "confirmed" on its setpoint and still be
         * ignoring the limit entirely. */
        cJSON *prerequisite = cJSON_AddObjectToObject(item, "prerequisite");
        if (prerequisite) {
            cJSON_AddStringToObject(prerequisite, "state",
                                    prerequisite_state_slug(&data));
            cJSON_AddBoolToObject(prerequisite, "required", data.prerequisite_required);
            /* satisfied is set ONLY by a successful read of the enable register,
             * never by an accepted write. False when unknown. */
            cJSON_AddBoolToObject(prerequisite, "satisfied", data.prerequisite_satisfied);
            /* Permanent versus transient. These demand different actions and are
             * reported as two separate booleans rather than one severity. */
            cJSON_AddBoolToObject(prerequisite, "unverifiable",
                                  data.prerequisite_unverifiable);
            cJSON_AddBoolToObject(prerequisite, "describable",
                                  data.prerequisite_describable);
            cJSON_AddBoolToObject(prerequisite, "read_valid",
                                  data.prerequisite_read_valid);
            /* What the last read actually found, versus what the profile wanted.
             * holds is the decoded verdict on the raw word. */
            cJSON_AddBoolToObject(prerequisite, "holds", data.prerequisite_holds);
            cJSON_AddNumberToObject(prerequisite, "raw", data.prerequisite_raw);
            cJSON_AddBoolToObject(prerequisite, "write_issued",
                                  data.prerequisite_write_issued);
            cJSON_AddNumberToObject(prerequisite, "confirmed_count",
                                    data.prerequisite_confirmed_count);
            cJSON_AddNumberToObject(prerequisite, "write_count",
                                    data.prerequisite_write_count);
            /* Non-zero means the limit was armed and then switched off underneath
             * this controller, which for Solis returns the machine to 100 %. */
            cJSON_AddNumberToObject(prerequisite, "lost_count",
                                    data.prerequisite_lost_count);
            cJSON_AddNumberToObject(prerequisite, "last_read_ms",
                                    data.last_prerequisite_read_ms);
            cJSON_AddNumberToObject(prerequisite, "last_write_ms",
                                    data.last_prerequisite_write_ms);
            cJSON_AddNumberToObject(prerequisite, "last_error",
                                    data.prerequisite_last_error);
            cJSON_AddStringToObject(prerequisite, "last_error_name",
                                    esp_err_to_name((esp_err_t)data.prerequisite_last_error));
        }
        cJSON_AddItemToArray(items, item);
    }
    return send_json(request, root);
}

/*
 * POST /api/control/enable   body: {"enabled": true | false}
 *
 * WHY THIS EXISTS. There was no way to arm automatic control. control.enabled
 * was written false in four places and true in none, and no route reached it.
 * The control engine was complete -- step, ramp, readback confirmation -- and
 * unreachable, which is the reason the loop had never run end to end. The
 * missing piece was never hardware; it was the switch.
 *
 * WHAT PROTECTS THE PLANT IS NOT THIS HANDLER. The control task re-evaluates the
 * commissioning gate every cycle and withholds command authority on its own, so
 * arming cannot grant authority the gate refuses. The check here exists so an
 * engineer is told why nothing happened rather than arming into a controller
 * that silently does nothing.
 *
 * The intent is PERSISTED. A controller that disarmed on every power blip would
 * be unusable on a site nobody is standing at, and persisting is safe precisely
 * because the gate is re-evaluated on boot: a stored "armed" that no longer
 * qualifies commands nothing. It is also cleared by every configuration write
 * that invalidates commissioning -- meter, inverter and solar-grid saves each
 * force control.enabled false already.
 *
 * Disarming is unconditional. Being unable to command is recoverable; being
 * unable to stop commanding is not. So a disable is honoured even if persisting
 * it fails, and the failure is reported alongside rather than instead.
 *
 * Engineering-gated: no public_uri() entry, so the guard defaults it to
 * GATEWAY_MODE_PROTECTED. No Modbus I/O happens here -- control_engine_set_enabled()
 * takes a spinlock and returns; the control task applies the change on its next
 * cycle.
 */
static esp_err_t control_enable_post(httpd_req_t *request)
{
    cJSON *root = NULL;
    esp_err_t err = http_json_parse_bounded(request, 256U, 5000U, 4U, &root);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON *error = cJSON_CreateObject();
        if (!error) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(error, "error", "A JSON body with a boolean 'enabled' is required");
        httpd_resp_set_status(request, "400 Bad Request");
        return send_json(request, error);
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (!cJSON_IsBool(item)) {
        cJSON_Delete(root);
        cJSON *error = cJSON_CreateObject();
        if (!error) return httpd_resp_send_500(request);
        cJSON_AddStringToObject(error, "error", "'enabled' must be true or false");
        httpd_resp_set_status(request, "400 Bad Request");
        return send_json(request, error);
    }
    const bool wanted = cJSON_IsTrue(item);
    cJSON_Delete(root);

    const esp_err_t applied = control_engine_set_enabled(wanted);

    /* Persist only what was actually applied. Storing an armed intent the engine
     * refused would arm the plant on the next reboot on the strength of a
     * request that was declined. */
    bool persisted = false;
    if (applied == ESP_OK) {
        app_config_t *config = malloc(sizeof(*config));
        if (config) {
            if (config_manager_get_snapshot(config) == ESP_OK) {
                config->control.enabled = wanted;
                persisted = config_manager_save(config) == ESP_OK;
            }
            free(config);
        }
    }

    commissioning_status_t status;
    control_engine_get_commissioning(&status);
    control_status_t control = {0};
    control_engine_get_status(&control);

    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(response, "requested", wanted);
    cJSON_AddBoolToObject(response, "enabled", control.enabled);
    cJSON_AddBoolToObject(response, "persisted", persisted);
    cJSON_AddBoolToObject(response, "commissioned", status.commissioned);
    cJSON_AddStringToObject(response, "scope", commissioning_scope_label(status.scope));
    cJSON_AddNumberToObject(response, "unmet_count", status.unmet_count);
    if (applied != ESP_OK) {
        cJSON_AddStringToObject(response, "error",
                                status.commissioned
                                    ? "A previous disable has not yet confirmed safe zero"
                                    : commissioning_gate_summary(&status));
        httpd_resp_set_status(request, "409 Conflict");
        return send_json(request, response);
    }
    /* Scope travels with the verdict, in the response that grants the authority.
     * An engineer who armed in lab scope must be told, in the same breath, that
     * what they armed is not production control and is not evidence about
     * physical equipment. */
    if (wanted && status.scope == COMMISSIONING_SCOPE_LAB) {
        cJSON_AddStringToObject(response, "notice",
                                "Armed in lab simulator scope. Commands are issued to an endpoint "
                                "declared a simulator. This is not production control and is not "
                                "evidence about physical equipment.");
    }
    if (applied == ESP_OK && !persisted) {
        cJSON_AddStringToObject(response, "persist_error",
                                "Applied to the running controller but not persisted; "
                                "this will not survive a restart");
    }
    return send_json(request, response);
}

esp_err_t commissioning_gate_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t arm = {
        .uri = "/api/control/enable",
        .method = HTTP_POST,
        .handler = control_enable_post,
    };
    esp_err_t armed = httpd_register_uri_handler(server, &arm);
    if (armed != ESP_OK) return armed;

    const httpd_uri_t gate = {
        .uri = "/api/commissioning/gate",
        .method = HTTP_GET,
        .handler = gate_get,
    };
    esp_err_t err = httpd_register_uri_handler(server, &gate);
    if (err != ESP_OK) return err;

    const httpd_uri_t confirmation = {
        .uri = "/api/inverters/write-confirmation",
        .method = HTTP_GET,
        .handler = confirmation_get,
    };
    return httpd_register_uri_handler(server, &confirmation);
}
