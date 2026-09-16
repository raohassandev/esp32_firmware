#include "pvdg_ui_adapter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void copy_text(char *dst, size_t size, const char *src)
{
    if (!dst || size == 0U) return;
    snprintf(dst, size, "%s", src ? src : "");
}

static pvdg_ui_measurement_t unavailable(void)
{
    pvdg_ui_measurement_t m = {0};
    m.quality = PVDG_UI_QUALITY_UNAVAILABLE;
    return m;
}

static pvdg_ui_measurement_t measurement(double value, pvdg_ui_quality_t quality,
                                         bool has_age_ms, uint32_t age_ms)
{
    if (!isfinite(value)) return unavailable();
    pvdg_ui_measurement_t m = {
        .available = true,
        .value = value,
        .quality = quality,
        .has_age_ms = has_age_ms,
        .age_ms = has_age_ms ? age_ms : 0U,
    };
    return m;
}

static bool reliable_measurement(const pvdg_ui_measurement_t *m)
{
    return m && m->available && m->quality == PVDG_UI_QUALITY_GOOD &&
           isfinite(m->value);
}

static pvdg_ui_quality_t grid_quality(const pvdg_ui_firmware_snapshot_t *s)
{
    if (!s->meter_online || !s->meter_has_data || !s->has_grid_power_kw) {
        return PVDG_UI_QUALITY_UNAVAILABLE;
    }
    if (s->meter_stale) return PVDG_UI_QUALITY_STALE;
    if (s->meter_degraded) return PVDG_UI_QUALITY_DEGRADED;
    return PVDG_UI_QUALITY_GOOD;
}

static pvdg_ui_measurement_t optional_scope_measurement(bool scope_available,
                                                        bool stale,
                                                        bool has_value,
                                                        double value,
                                                        bool has_age_ms,
                                                        uint32_t age_ms)
{
    if (!scope_available || !has_value) return unavailable();
    return measurement(value,
                       stale ? PVDG_UI_QUALITY_STALE : PVDG_UI_QUALITY_GOOD,
                       has_age_ms, age_ms);
}

static void adapt_grid_detail(const pvdg_ui_firmware_snapshot_t *s,
                              pvdg_ui_model_t *m)
{
    m->grid.voltage_l1_v = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_voltage_l1_v, s->voltage_l1_v,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.voltage_l2_v = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_voltage_l2_v, s->voltage_l2_v,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.voltage_l3_v = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_voltage_l3_v, s->voltage_l3_v,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.current_l1_a = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_current_l1_a, s->current_l1_a,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.current_l2_a = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_current_l2_a, s->current_l2_a,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.current_l3_a = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_current_l3_a, s->current_l3_a,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.frequency_hz = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_frequency_hz, s->frequency_hz,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.power_factor = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_power_factor, s->power_factor,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.reactive_kvar = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_reactive_kvar, s->reactive_kvar,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);
    m->grid.apparent_kva = optional_scope_measurement(
        s->em500_instantaneous_available, s->em500_instantaneous_stale,
        s->has_apparent_kva, s->apparent_kva,
        s->has_em500_instantaneous_age_ms, s->em500_instantaneous_age_ms);

    m->grid.imported_energy_kwh = optional_scope_measurement(
        s->em500_energy_available, s->em500_energy_stale,
        s->has_imported_energy_kwh, s->imported_energy_kwh,
        s->has_em500_energy_age_ms, s->em500_energy_age_ms);
    m->grid.exported_energy_kwh = optional_scope_measurement(
        s->em500_energy_available, s->em500_energy_stale,
        s->has_exported_energy_kwh, s->exported_energy_kwh,
        s->has_em500_energy_age_ms, s->em500_energy_age_ms);
}

static void adapt_policy(const pvdg_ui_firmware_snapshot_t *s,
                         pvdg_ui_model_t *m)
{
    switch (s->grid_policy) {
    case 0:
        m->export_policy.kind = PVDG_UI_EXPORT_POLICY_ZERO_EXPORT;
        copy_text(m->export_policy.label, sizeof(m->export_policy.label),
                  s->grid_policy_name ? s->grid_policy_name : "Zero Export");
        break;
    case 1:
        m->export_policy.kind = PVDG_UI_EXPORT_POLICY_EXPORT_ENABLED;
        copy_text(m->export_policy.label, sizeof(m->export_policy.label),
                  s->grid_policy_name ? s->grid_policy_name : "Limited Export");
        break;
    case 2:
        m->export_policy.kind = PVDG_UI_EXPORT_POLICY_MINIMUM_IMPORT;
        copy_text(m->export_policy.label, sizeof(m->export_policy.label),
                  s->grid_policy_name ? s->grid_policy_name : "Minimum Import");
        break;
    default:
        m->export_policy.kind = PVDG_UI_EXPORT_POLICY_UNKNOWN;
        copy_text(m->export_policy.label, sizeof(m->export_policy.label), "Unknown");
        break;
    }
}

static void adapt_inverters(const pvdg_ui_firmware_snapshot_t *s,
                            pvdg_ui_model_t *m)
{
    const uint8_t count = s->inverter_count > PVDG_UI_MAX_INVERTERS
                              ? PVDG_UI_MAX_INVERTERS
                              : s->inverter_count;
    m->solar.configured_count = count;
    m->solar.inverter_count = count;
    double total = 0.0;
    uint8_t total_valid = 0U;

    for (uint8_t i = 0; i < count; ++i) {
        const pvdg_ui_firmware_inverter_t *src = &s->inverters[i];
        pvdg_ui_inverter_t *dst = &m->solar.inverters[i];
        dst->online = src->online;
        dst->telemetry_valid = src->telemetry_valid;
        dst->telemetry_stale = src->telemetry_stale;
        if (src->online) m->solar.online_count++;
        copy_text(dst->name, sizeof(dst->name), src->name);
        copy_text(dst->status, sizeof(dst->status), src->status);

        const bool good_power = src->telemetry_valid &&
                                !src->telemetry_stale &&
                                src->has_measured_power_kw &&
                                isfinite(src->measured_power_kw);
        if (good_power) {
            dst->measured_power_kw = measurement(
                src->measured_power_kw, PVDG_UI_QUALITY_GOOD,
                src->has_telemetry_age_ms, src->telemetry_age_ms);
            total += src->measured_power_kw;
            total_valid++;
        } else {
            dst->measured_power_kw = unavailable();
        }

        if (src->has_readback_percent && isfinite(src->readback_percent)) {
            dst->readback_percent = measurement(
                src->readback_percent,
                src->telemetry_stale ? PVDG_UI_QUALITY_STALE : PVDG_UI_QUALITY_GOOD,
                src->has_telemetry_age_ms, src->telemetry_age_ms);
        } else {
            dst->readback_percent = unavailable();
        }
    }

    /* Never publish a partial fleet sum as total measured solar production. */
    m->solar.measured_kw =
        count > 0U && total_valid == count
            ? measurement(total, PVDG_UI_QUALITY_GOOD, false, 0U)
            : unavailable();
}

static bool generator_zero_is_evidenced(const pvdg_ui_firmware_snapshot_t *s)
{
    return s->generator_evidence_configured &&
           !s->generator_running &&
           s->generator_breaker_known &&
           !s->generator_breaker_closed;
}

static void adapt_generator(const pvdg_ui_firmware_snapshot_t *s,
                            pvdg_ui_model_t *m)
{
    m->generator.evidence_configured = s->generator_evidence_configured;
    m->generator.running = s->generator_running;
    m->generator.breaker_known = s->generator_breaker_known;
    m->generator.breaker_closed = s->generator_breaker_closed;
    m->generator.running_count = s->generator_running_count;

    if (s->has_generator_measured_total_kw &&
        isfinite(s->generator_measured_total_kw)) {
        m->generator.measured_kw =
            measurement(s->generator_measured_total_kw,
                        PVDG_UI_QUALITY_GOOD, false, 0U);
    } else if (generator_zero_is_evidenced(s)) {
        m->generator.measured_kw =
            measurement(0.0, PVDG_UI_QUALITY_GOOD, false, 0U);
    } else {
        m->generator.measured_kw = unavailable();
    }

    m->generator.running_rated_kw =
        s->has_generator_running_rated_kw
            ? measurement(s->generator_running_rated_kw,
                          PVDG_UI_QUALITY_GOOD, false, 0U)
            : unavailable();
    m->generator.required_minimum_kw =
        s->has_generator_required_minimum_kw
            ? measurement(s->generator_required_minimum_kw,
                          PVDG_UI_QUALITY_GOOD, false, 0U)
            : unavailable();
    m->generator.safe_pv_limit_kw =
        s->has_generator_safe_pv_limit_kw
            ? measurement(s->generator_safe_pv_limit_kw,
                          PVDG_UI_QUALITY_GOOD, false, 0U)
            : unavailable();
}

static void adapt_load(const pvdg_ui_firmware_snapshot_t *s,
                       pvdg_ui_model_t *m)
{
    if (s->facility_load_supported &&
        s->has_facility_load_kw &&
        isfinite(s->facility_load_kw)) {
        m->load.power_kw =
            measurement(s->facility_load_kw,
                        PVDG_UI_QUALITY_GOOD, false, 0U);
        m->load.derived = false;
        return;
    }

    /* Derived load is a current operating value. Do not derive it from stale,
     * degraded or unavailable contributors even when their last numeric value
     * remains displayable elsewhere in the UI. */
    if (reliable_measurement(&m->solar.measured_kw) &&
        reliable_measurement(&m->generator.measured_kw) &&
        reliable_measurement(&m->grid.power_kw)) {
        const double value = m->solar.measured_kw.value +
                             m->generator.measured_kw.value +
                             m->grid.power_kw.value;
        if (isfinite(value) && value >= -0.01) {
            m->load.power_kw =
                measurement(value < 0.0 ? 0.0 : value,
                            PVDG_UI_QUALITY_GOOD, false, 0U);
            m->load.derived = true;
            return;
        }
    }

    m->load.power_kw = unavailable();
    m->load.derived = false;
}

void pvdg_ui_adapter_build_model(const pvdg_ui_firmware_snapshot_t *s,
                                 pvdg_ui_model_t *m)
{
    if (!m) return;
    pvdg_ui_model_reset(m);
    if (!s) return;

    m->valid = s->status_valid;
    m->network.online = s->network_online;
    m->network.rssi = s->rssi;
    copy_text(m->network.ssid, sizeof(m->network.ssid), s->ssid);
    copy_text(m->network.ip, sizeof(m->network.ip), s->ip);

    m->controller.enabled = s->control_enabled;
    m->controller.command_authority = s->command_authority;
    copy_text(m->controller.authority_label,
              sizeof(m->controller.authority_label), s->authority_label);
    copy_text(m->controller.inhibit_reason,
              sizeof(m->controller.inhibit_reason), s->inhibit_reason);
    m->controller.requested_pv_kw =
        s->has_requested_pv_kw
            ? measurement(s->requested_pv_kw,
                          PVDG_UI_QUALITY_GOOD, false, 0U)
            : unavailable();
    m->controller.applied_pv_kw =
        s->has_applied_pv_kw
            ? measurement(s->applied_pv_kw,
                          PVDG_UI_QUALITY_GOOD, false, 0U)
            : unavailable();

    m->grid.available = s->grid_available;
    m->grid.breaker_known = s->grid_breaker_known;
    m->grid.breaker_closed = s->grid_breaker_closed;
    copy_text(m->grid.meter_name, sizeof(m->grid.meter_name),
              s->grid_meter_name);

    const pvdg_ui_quality_t q = grid_quality(s);
    m->grid.power_kw =
        q == PVDG_UI_QUALITY_UNAVAILABLE
            ? unavailable()
            : measurement(s->grid_power_kw, q,
                          s->has_grid_age_ms, s->grid_age_ms);
    adapt_grid_detail(s, m);

    adapt_policy(s, m);
    adapt_inverters(s, m);
    adapt_generator(s, m);
    adapt_load(s, m);

    m->alarm_flags = s->alarm_flags;
    m->alarm_count =
        s->alarm_count > PVDG_UI_MAX_ALARMS
            ? PVDG_UI_MAX_ALARMS
            : s->alarm_count;
    for (uint8_t i = 0; i < m->alarm_count; ++i) {
        copy_text(m->alarms[i], sizeof(m->alarms[i]), s->alarms[i]);
    }
}
