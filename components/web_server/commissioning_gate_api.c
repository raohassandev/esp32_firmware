#include "commissioning_gate_api.h"

#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "control_engine.h"
#include "esp_err.h"
#include "inverter_manager.h"
#include "inverter_write_confirmation.h"

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

esp_err_t commissioning_gate_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
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
