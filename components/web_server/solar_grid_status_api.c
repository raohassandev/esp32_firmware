#include "solar_grid_status_api.h"

#include <math.h>
#include <stdlib.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "config_manager.h"
#include "config_types.h"
#include "control_engine.h"
#include "esp_timer.h"
#include "generator_fleet_limit.h"
#include "grid_control_gate.h"
#include "inverter_write_confirmation.h"
#include "meter_manager.h"
#include "solar_grid_config.h"
#include "source_mode.h"
#include "write_provenance_api.h"

static void add_finite(cJSON *root, const char *name, float value)
{
    if (isfinite(value)) cJSON_AddNumberToObject(root, name, value);
    else cJSON_AddNullToObject(root, name);
}

/* Same clock the control loop and the meter manager stamp their samples with, so a
 * sample age computed here is comparable with the one the control loop computes. */
static uint32_t status_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/*
 * THE AGGREGATE GENERATOR LIMIT, PUBLISHED.
 *
 * The controller derives a minimum-loading floor for the whole engine fleet and
 * refuses to command PV when it cannot. Until now none of that reached any client:
 * an engineer commissioning a two- or three-genset plant could see that control was
 * inhibited but not which engines the controller counted, what floor it derived, or
 * what the base-load setpoints contributed to it.
 *
 * WHAT THIS IS, EXACTLY, because the distinction is the whole value of the number.
 *
 * `derived_floor` is generator_fleet_limit_evaluate() -- the control loop's own
 * function, not a second implementation -- evaluated over the COMMISSIONED set with
 * every in-service engine treated as on the bus. That is the largest denominator the
 * commissioned policy admits and therefore the largest floor, which is the figure a
 * commissioning engineer needs: it is the one that has to be satisfiable. It is NOT
 * the cycle-by-cycle evaluation, it does not depend on any meter, and no safe-PV
 * figure is published from it -- a safe-PV number computed against a plant load of
 * zero would be a real quantity answering an imaginary question.
 *
 * The per-engine meter rows below are the raw, already-acquired samples the control
 * loop reads. Deliberately NO freshness verdict is recomputed here: the control loop
 * owns the one definition of "fresh", so this publishes the observation and the
 * configured timeout and lets the reader apply that one rule rather than inventing a
 * second one that could disagree.
 *
 * No Modbus I/O: solar_grid_config_get_snapshot() and meter_manager_get_data() both
 * return already-acquired cached state.
 */
static void add_generator_fleet(cJSON *root, const control_status_t *status)
{
    cJSON *fleet = cJSON_AddObjectToObject(root, "generator_fleet");
    if (!fleet) return;
    cJSON_AddBoolToObject(fleet, "modbus_io_in_http_handler", false);

    solar_grid_config_t config;
    if (solar_grid_config_get_snapshot(&config) != ESP_OK) {
        /* Unreadable is reported as unreadable. It is never reported as a
         * commissioned fleet of zero engines, which would read as a policy. */
        cJSON_AddBoolToObject(fleet, "policy_readable", false);
        return;
    }
    cJSON_AddBoolToObject(fleet, "policy_readable", true);

    const uint8_t mode = solar_grid_config_load_sharing_mode(&config);
    cJSON_AddStringToObject(fleet, "sharing_mode", solar_grid_load_sharing_name(mode));
    cJSON_AddBoolToObject(fleet, "sharing_mode_supported",
                          generator_sharing_mode_supported(mode));
    cJSON_AddNumberToObject(fleet, "engine_slot_count", SOLAR_GRID_MAX_GENERATORS);
    cJSON_AddNumberToObject(fleet, "engines_in_service",
                            solar_grid_config_enabled_generator_count(&config));

    /* Meter attribution and the configured staleness rule, read once. */
    meter_role_assignment_t roles = {0};
    uint32_t stale_timeout_ms = 0U;
    bool attribution_readable = false;
    app_config_t *application = malloc(sizeof(*application));
    if (application) {
        if (config_manager_get_snapshot(application) == ESP_OK) {
            roles = config_manager_role_assignment(application);
            stale_timeout_ms = application->control.meter_stale_timeout_ms;
            attribution_readable = true;
        }
        free(application);
    }
    cJSON_AddBoolToObject(fleet, "meter_attribution_readable", attribution_readable);
    cJSON_AddBoolToObject(fleet, "meter_attribution_valid",
                          attribution_readable && roles.valid);
    if (attribution_readable) {
        cJSON_AddNumberToObject(fleet, "meter_stale_timeout_ms", stale_timeout_ms);
    } else {
        cJSON_AddNullToObject(fleet, "meter_stale_timeout_ms");
    }

    generator_fleet_input_t input = {
        .evidence_fresh = true,
        /* Zero, and deliberately not the measured plant load: only safe_pv_kw
         * depends on it, and safe_pv_kw is not published from this evaluation. */
        .facility_load_kw = 0.0f,
        .allow_unmetered_single_engine = false,
        .sharing_mode = mode,
        .engine_count = (uint8_t)SOLAR_GRID_MAX_GENERATORS,
    };

    cJSON *engines = cJSON_AddArrayToObject(fleet, "engines");
    const uint32_t timestamp = status_now_ms();
    for (uint8_t slot = 0U; slot < SOLAR_GRID_MAX_GENERATORS; ++slot) {
        const solar_grid_generator_limits_t limits =
            solar_grid_config_generator(&config, slot);
        generator_engine_input_t *engine = &input.engines[slot];
        engine->configured = limits.enabled;
        engine->rated_kw = limits.rated_kw;
        engine->minimum_loading_percent = limits.minimum_loading_percent;
        engine->reserve_kw = limits.reserve_kw;
        engine->reverse_power_margin_kw = limits.reverse_power_margin_kw;
        engine->role = solar_grid_config_engine_role(&config, slot);
        engine->base_load_kw = solar_grid_config_engine_base_load_kw(&config, slot);
        /* Every in-service engine is placed on the bus for the commissioned-set
         * evaluation. That is the largest denominator the policy admits, so it is
         * the largest floor: the conservative direction and the one commissioning
         * has to satisfy. */
        engine->metered = limits.enabled;
        engine->sample_fresh = limits.enabled;
        engine->measured_kw = 0.0f;

        if (!engines) continue;
        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddNumberToObject(entry, "slot", slot);
        cJSON_AddBoolToObject(entry, "in_service", limits.enabled);
        add_finite(entry, "rated_kw", limits.rated_kw);
        add_finite(entry, "minimum_loading_percent", limits.minimum_loading_percent);
        add_finite(entry, "minimum_loading_kw",
                   limits.rated_kw * limits.minimum_loading_percent / 100.0f);
        add_finite(entry, "reserve_kw", limits.reserve_kw);
        add_finite(entry, "reverse_power_margin_kw", limits.reverse_power_margin_kw);
        cJSON_AddStringToObject(entry, "role",
                                solar_grid_engine_role_name(engine->role));
        add_finite(entry, "base_load_kw", engine->base_load_kw);
        /* A base-loaded engine held below its own minimum loading is a
         * commissioning fault: its load does not follow the plant total, so no PV
         * limit corrects it. Reported per engine so the fault names the machine. */
        cJSON_AddBoolToObject(entry, "base_load_below_minimum",
                              engine->role == (uint8_t)GENERATOR_ENGINE_ROLE_BASE_LOAD &&
                                  engine->base_load_kw > 0.0f &&
                                  engine->base_load_kw <
                                      limits.rated_kw * limits.minimum_loading_percent / 100.0f);

        const uint8_t meter_index =
            attribution_readable ? roles.generator_index[slot] : METER_ROLE_INDEX_NONE;
        if (meter_index == METER_ROLE_INDEX_NONE) {
            cJSON_AddNullToObject(entry, "meter_index");
        } else {
            cJSON_AddNumberToObject(entry, "meter_index", meter_index);
        }
        meter_data_t meter = {0};
        const bool have_sample = meter_index != METER_ROLE_INDEX_NONE &&
                                 meter_manager_get_data(meter_index, &meter);
        cJSON_AddBoolToObject(entry, "meter_sample_present", have_sample);
        if (have_sample) {
            cJSON_AddBoolToObject(entry, "meter_online", meter.online);
            cJSON_AddBoolToObject(entry, "meter_degraded", meter.degraded);
            add_finite(entry, "meter_power_kw", meter.active_power_kw);
            if (meter.last_update_ms == 0U) {
                cJSON_AddNullToObject(entry, "meter_sample_age_ms");
            } else {
                cJSON_AddNumberToObject(entry, "meter_sample_age_ms",
                                        timestamp - meter.last_update_ms);
            }
        } else {
            cJSON_AddNullToObject(entry, "meter_online");
            cJSON_AddNullToObject(entry, "meter_degraded");
            cJSON_AddNullToObject(entry, "meter_power_kw");
            cJSON_AddNullToObject(entry, "meter_sample_age_ms");
        }
        cJSON_AddItemToArray(engines, entry);
    }

    const generator_fleet_limit_t derived = generator_fleet_limit_evaluate(&input);
    cJSON *floor_object = cJSON_AddObjectToObject(fleet, "derived_floor");
    if (!floor_object) return;
    cJSON_AddStringToObject(floor_object, "basis",
                            "every_engine_in_service_treated_as_on_the_bus");
    cJSON_AddBoolToObject(floor_object, "known", derived.known);
    /* The control engine's own slug vocabulary, so a report or a support call can
     * quote something stable. */
    cJSON_AddStringToObject(floor_object, "reason",
                            generator_fleet_reason_id(derived.reason));
    cJSON_AddStringToObject(floor_object, "sharing_mode",
                            generator_sharing_mode_id(derived.sharing_mode));
    cJSON_AddNumberToObject(floor_object, "online_count", derived.online_count);
    add_finite(floor_object, "online_rated_kw", derived.online_rated_kw);
    add_finite(floor_object, "minimum_loading_kw", derived.minimum_loading_kw);
    cJSON_AddNumberToObject(floor_object, "base_loaded_count", derived.base_loaded_count);
    add_finite(floor_object, "base_load_total_kw", derived.base_load_total_kw);
    add_finite(floor_object, "required_generator_kw", derived.required_generator_kw);
    /* Stated rather than left to be inferred from the absence of a key. */
    cJSON_AddBoolToObject(floor_object, "safe_pv_published", false);

    /* THE RUNTIME VERDICT -- what the control loop actually acted on, taken from the
     * loop itself rather than recomputed here.
     *
     * The distinction matters and is the reason both objects exist. `derived_floor`
     * above answers "what would the floor be if every in-service engine were on the
     * bus"; this answers "what floor did the loop use, given which engines it
     * believed were running". They differ whenever an engine is commissioned but its
     * meter is not reporting, and presenting the first as the second would misreport
     * why PV is being held down. */
    cJSON *runtime = cJSON_AddObjectToObject(fleet, "runtime_floor");
    if (!runtime) return;
    generator_fleet_limit_t live = {0};
    const bool have_live = control_engine_get_generator_fleet(&live);
    /* False until the loop has evaluated once, so "no verdict yet" stays
     * distinguishable from "a verdict of zero". */
    cJSON_AddBoolToObject(fleet, "runtime_fleet_limit_published", have_live);
    cJSON_AddBoolToObject(runtime, "evaluated", have_live);
    if (have_live) {
        cJSON_AddStringToObject(runtime, "basis", "engines_the_control_loop_believed_online");
        cJSON_AddBoolToObject(runtime, "known", live.known);
        cJSON_AddStringToObject(runtime, "reason", generator_fleet_reason_id(live.reason));
        cJSON_AddStringToObject(runtime, "sharing_mode",
                                generator_sharing_mode_id(live.sharing_mode));
        cJSON_AddNumberToObject(runtime, "online_count", live.online_count);
        add_finite(runtime, "online_rated_kw", live.online_rated_kw);
        add_finite(runtime, "minimum_loading_kw", live.minimum_loading_kw);
        cJSON_AddNumberToObject(runtime, "base_loaded_count", live.base_loaded_count);
        add_finite(runtime, "base_load_total_kw", live.base_load_total_kw);
        add_finite(runtime, "required_generator_kw", live.required_generator_kw);
        /* Published here, unlike derived_floor, because this one was computed against
         * the real plant load and is therefore a real answer. */
        add_finite(runtime, "safe_pv_kw", live.safe_pv_kw);
    }
    cJSON_AddStringToObject(fleet, "runtime_reason_field", "inhibit_reason");
    cJSON_AddStringToObject(fleet, "runtime_reason",
                            status ? status->inhibit_reason : "");
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
    /* WHAT THE WRITE CONFIRMATION RESTS ON, beside the verdict and never instead
     * of it.
     *
     * write_confirmation above is one of four words. Since plant-level logger
     * control landed, the word "confirmed" covers two kinds of evidence that are
     * not equally strong: a limit DEMONSTRATED by measured power, and a setpoint
     * readback that on a stored-command interface is an echo proving acceptance
     * only. A client given the verdict and not the proof cannot tell them apart,
     * and would show an operator a limited plant that may not be limited.
     *
     * Unconditional for the same reason commissioning_scope above is
     * unconditional. The roll-up walks the inverter manager's already-acquired
     * snapshots; this handler still performs no Modbus I/O. */
    write_provenance_rollup_t provenance;
    write_provenance_collect(&provenance);
    write_provenance_add_fleet(root, &provenance);
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

    /* THE GENERATOR FLEET. Published unconditionally, for the same reason
     * commissioning_scope above is unconditional: a client that can read that
     * automatic control is inhibited must be able to read which engines the
     * generator policy describes and what floor they imply, or the inhibit is a
     * verdict with no evidence behind it. Reads cached configuration and the last
     * meter samples; this handler still performs no Modbus I/O. */
    add_generator_fleet(root, &status);

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
