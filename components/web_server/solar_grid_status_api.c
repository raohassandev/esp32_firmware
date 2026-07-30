#include "solar_grid_status_api.h"

#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "control_engine.h"
#include "grid_control_gate.h"
#include "inverter_write_confirmation.h"
#include "solar_grid_config.h"
#include "source_mode.h"

static void add_finite(cJSON *root, const char *name, float value)
{
    if (isfinite(value)) cJSON_AddNumberToObject(root, name, value);
    else cJSON_AddNullToObject(root, name);
}

static esp_err_t status_get(httpd_req_t *request)
{
    control_status_t status = {0};
    control_engine_get_status(&status);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "modbus_io_in_http_handler", false);
    cJSON_AddBoolToObject(root, "control_enabled", status.enabled);
    cJSON_AddNumberToObject(root, "application_mode", status.mode);
    cJSON_AddNumberToObject(root, "grid_policy", status.grid_policy);
    cJSON_AddStringToObject(root, "grid_policy_name",
                            solar_grid_policy_name((solar_grid_policy_t)status.grid_policy));
    cJSON_AddNumberToObject(root, "source_mode", status.source_mode);
    cJSON_AddStringToObject(root, "source_mode_name",
                            source_mode_name((source_mode_t)status.source_mode));
    cJSON_AddNumberToObject(root, "grid_gate_state", status.grid_gate_state);
    cJSON_AddStringToObject(root, "grid_gate_state_name",
                            grid_control_gate_state_name((grid_gate_state_t)status.grid_gate_state));
    cJSON_AddBoolToObject(root, "grid_evidence_configured",
                          status.grid_evidence_configured);
    cJSON_AddBoolToObject(root, "grid_evidence_fresh",
                          status.grid_evidence_fresh);
    cJSON_AddBoolToObject(root, "grid_available", status.grid_available);
    cJSON_AddBoolToObject(root, "grid_breaker_closed",
                          status.grid_breaker_closed);
    cJSON_AddBoolToObject(root, "grid_recovery_stable",
                          status.grid_recovery_stable);
    cJSON_AddBoolToObject(root, "grid_loss_confirmed",
                          status.grid_loss_confirmed);
    if (status.grid_evidence_success_count > 0U) {
        cJSON_AddNumberToObject(root, "grid_evidence_age_ms",
                                status.grid_evidence_age_ms);
    } else {
        cJSON_AddNullToObject(root, "grid_evidence_age_ms");
    }
    cJSON_AddNumberToObject(root, "grid_evidence_success_count",
                            status.grid_evidence_success_count);
    cJSON_AddNumberToObject(root, "grid_evidence_error_count",
                            status.grid_evidence_error_count);
    cJSON_AddNumberToObject(root, "grid_evidence_last_error",
                            status.grid_evidence_last_error);
    cJSON_AddStringToObject(root, "grid_evidence_last_error_name",
                            esp_err_to_name(status.grid_evidence_last_error));
    cJSON_AddNumberToObject(root, "grid_available_raw",
                            status.grid_available_raw);
    cJSON_AddNumberToObject(root, "grid_breaker_raw",
                            status.grid_breaker_raw);
    add_finite(root, "raw_grid_power_kw", status.raw_grid_power_kw);
    add_finite(root, "grid_power_kw", status.grid_power_kw);
    add_finite(root, "grid_target_kw", status.grid_target_kw);
    add_finite(root, "error_kw", status.error_kw);
    add_finite(root, "requested_pv_kw", status.requested_pv_kw);
    add_finite(root, "applied_pv_kw", status.applied_pv_kw);
    cJSON_AddNumberToObject(root, "last_cycle_ms", status.last_cycle_ms);
    /* Commissioning gate and write confirmation, so a single status poll never
     * has to guess why control is inhibited. The per-prerequisite detail lives
     * at /api/commissioning/gate. */
    cJSON_AddBoolToObject(root, "commissioned", status.commissioned);
    /* Always published alongside `commissioned`, never conditionally: a client
     * that reads one must be able to read the other, or "commissioned" will be
     * shown for a plant made entirely of simulators. */
    cJSON_AddStringToObject(root, "commissioning_scope",
                            commissioning_scope_label(
                                (commissioning_scope_t)status.commissioning_scope));
    const bool lab_only = (commissioning_scope_t)status.commissioning_scope ==
                          COMMISSIONING_SCOPE_LAB;
    cJSON_AddBoolToObject(root, "lab_simulator_mode", lab_only);
    if (lab_only) {
        cJSON_AddStringToObject(root, "lab_simulator_notice",
                                "At least one commanded inverter is a declared Modbus "
                                "simulator. This is lab validation, not production control, "
                                "and nothing observed here is evidence about physical "
                                "equipment.");
    }
    cJSON_AddNumberToObject(root, "commissioning_unmet_count",
                            status.commissioning_unmet_count);
    cJSON_AddStringToObject(root, "commissioning_first_unmet",
                            status.commissioned
                                ? ""
                                : commissioning_prereq_id(status.commissioning_first_unmet));
    cJSON_AddBoolToObject(root, "command_authority", status.command_authority);
    cJSON_AddStringToObject(root, "inhibit_reason", status.inhibit_reason);
    cJSON_AddStringToObject(root, "write_confirmation",
                            inverter_write_state_name(
                                (inverter_write_state_t)status.write_confirmation));
    cJSON_AddBoolToObject(root, "write_confirmation_fault",
                          status.write_confirmation_fault);
    /* PREREQUISITE ENABLE REGISTERS. A separate fault from write confirmation
     * above, published unconditionally next to it for the same reason
     * commissioning_scope is published unconditionally: a client that can read
     * one of these must be able to read the other, or an inverter disappears
     * from the commandable fleet with no reason given.
     *
     * write_confirmation_fault  - the SETPOINT register read back wrong.
     * prerequisite_enable_fault - the ENABLE register is not confirmed to hold,
     *                             so the setpoint reads back PERFECTLY and is
     *                             ignored. This is the more dangerous of the two
     *                             and is never merged into the other.
     *
     * The counts are the control engine's own snapshot; this handler performs no
     * Modbus I/O. */
    cJSON_AddBoolToObject(root, "prerequisite_enable_fault",
                          status.prerequisite_enable_fault);
    cJSON_AddNumberToObject(root, "prerequisite_required_count",
                            status.prerequisite_required_count);
    /* Transient: not confirmed by a read right now, and retried. */
    cJSON_AddNumberToObject(root, "prerequisite_unconfirmed_count",
                            status.prerequisite_unconfirmed_count);
    /* Permanent: no writable and readable prerequisite can be described, so no
     * amount of polling resolves it. The remedy is a manual citation. */
    cJSON_AddNumberToObject(root, "prerequisite_unverifiable_count",
                            status.prerequisite_unverifiable_count);
    if (status.prerequisite_enable_fault) {
        cJSON_AddStringToObject(root, "prerequisite_enable_notice",
                                "An inverter enable register is not confirmed to hold. Its "
                                "setpoint would be accepted, echoed back and then ignored, so "
                                "the setpoint readback reads as a perfect match while the "
                                "inverter keeps generating at full output. This is not a "
                                "setpoint fault and will not appear as one.");
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t error = httpd_resp_sendstr(request, json);
    free(json);
    return error;
}

esp_err_t solar_grid_status_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/solar-grid/status",
        .method = HTTP_GET,
        .handler = status_get,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
