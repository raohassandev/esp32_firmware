#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pvdg_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PVDG_UI_ADAPTER_MAX_INVERTERS PVDG_UI_MAX_INVERTERS

typedef struct {
    bool online;
    bool telemetry_valid;
    bool telemetry_stale;
    const char *name;
    bool has_measured_power_kw;
    double measured_power_kw;
    bool has_telemetry_age_ms;
    uint32_t telemetry_age_ms;
    bool has_readback_percent;
    double readback_percent;
    const char *status;
} pvdg_ui_firmware_inverter_t;

typedef struct {
    bool status_valid;
    bool network_online;
    const char *ssid;
    const char *ip;
    int rssi;
    bool control_enabled;
    bool command_authority;
    const char *authority_label;
    const char *inhibit_reason;
    bool has_requested_pv_kw;
    double requested_pv_kw;
    bool has_applied_pv_kw;
    double applied_pv_kw;

    bool meter_online;
    bool meter_has_data;
    bool meter_stale;
    bool meter_degraded;
    bool has_grid_power_kw;
    double grid_power_kw;
    bool has_grid_age_ms;
    uint32_t grid_age_ms;
    const char *grid_meter_name;
    bool grid_available;
    bool grid_breaker_known;
    bool grid_breaker_closed;

    /* EM500 instantaneous scope. These fields are optional and never inferred
     * from grid active power. The host integration must only mark the scope
     * available after a successful, verified EM500 read. */
    bool em500_instantaneous_available;
    bool em500_instantaneous_stale;
    bool has_em500_instantaneous_age_ms;
    uint32_t em500_instantaneous_age_ms;
    bool has_voltage_l1_v;
    double voltage_l1_v;
    bool has_voltage_l2_v;
    double voltage_l2_v;
    bool has_voltage_l3_v;
    double voltage_l3_v;
    bool has_current_l1_a;
    double current_l1_a;
    bool has_current_l2_a;
    double current_l2_a;
    bool has_current_l3_a;
    double current_l3_a;
    bool has_frequency_hz;
    double frequency_hz;
    bool has_power_factor;
    double power_factor;
    bool has_reactive_kvar;
    double reactive_kvar;
    bool has_apparent_kva;
    double apparent_kva;

    /* Verified cumulative EM500 energy counters. Keeping energy availability
     * separate from instantaneous scope prevents one failed scope from
     * poisoning the other. */
    bool em500_energy_available;
    bool em500_energy_stale;
    bool has_em500_energy_age_ms;
    uint32_t em500_energy_age_ms;
    bool has_imported_energy_kwh;
    double imported_energy_kwh;
    bool has_exported_energy_kwh;
    double exported_energy_kwh;

    int grid_policy;
    const char *grid_policy_name;
    bool generator_evidence_configured;
    bool generator_running;
    bool generator_breaker_known;
    bool generator_breaker_closed;
    uint8_t generator_running_count;
    bool has_generator_measured_total_kw;
    double generator_measured_total_kw;
    bool has_generator_running_rated_kw;
    double generator_running_rated_kw;
    bool has_generator_required_minimum_kw;
    double generator_required_minimum_kw;
    bool has_generator_safe_pv_limit_kw;
    double generator_safe_pv_limit_kw;
    bool facility_load_supported;
    bool has_facility_load_kw;
    double facility_load_kw;
    uint8_t inverter_count;
    pvdg_ui_firmware_inverter_t inverters[PVDG_UI_ADAPTER_MAX_INVERTERS];
    uint32_t alarm_flags;
    uint8_t alarm_count;
    const char *alarms[PVDG_UI_MAX_ALARMS];
} pvdg_ui_firmware_snapshot_t;

/* grid_power_kw must already be normalized to firmware convention: +import/-export. */
void pvdg_ui_adapter_build_model(const pvdg_ui_firmware_snapshot_t *snapshot,
                                 pvdg_ui_model_t *model);

#ifdef __cplusplus
}
#endif
