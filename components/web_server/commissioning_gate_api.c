#include "commissioning_gate_api.h"

#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "control_engine.h"
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
