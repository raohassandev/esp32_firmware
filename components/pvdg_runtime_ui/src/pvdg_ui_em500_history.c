#include "pvdg_ui_em500_history.h"

#include <math.h>
#include <string.h>

static pvdg_ui_measurement_t unavailable(void)
{
    pvdg_ui_measurement_t value = {0};
    value.quality = PVDG_UI_QUALITY_UNAVAILABLE;
    return value;
}

static pvdg_ui_measurement_t map_measurement(bool scope_available,
                                             bool stale,
                                             bool has_value,
                                             double value,
                                             bool has_age_ms,
                                             uint32_t age_ms)
{
    if (!scope_available || !has_value || !isfinite(value)) return unavailable();
    pvdg_ui_measurement_t mapped = {
        .available = true,
        .value = value,
        .quality = stale ? PVDG_UI_QUALITY_STALE : PVDG_UI_QUALITY_GOOD,
        .has_age_ms = has_age_ms,
        .age_ms = has_age_ms ? age_ms : 0U,
    };
    return mapped;
}

void pvdg_ui_em500_statistics_reset(pvdg_ui_em500_statistics_t *statistics)
{
    if (!statistics) return;
    memset(statistics, 0, sizeof(*statistics));
    for (unsigned i = 0; i < PVDG_UI_EM500_STAT_COUNT; ++i) {
        statistics->blocks[i].active_power_kw = unavailable();
        statistics->blocks[i].reactive_power_kvar = unavailable();
        statistics->blocks[i].apparent_power_kva = unavailable();
        statistics->blocks[i].power_factor = unavailable();
        statistics->blocks[i].frequency_hz = unavailable();
        statistics->blocks[i].voltage_phase_v = unavailable();
        statistics->blocks[i].current_a = unavailable();
    }
}

void pvdg_ui_em500_statistics_apply_block(
    pvdg_ui_em500_statistics_t *statistics,
    pvdg_ui_em500_stat_kind_t kind,
    const pvdg_ui_firmware_em500_stat_block_t *source)
{
    if (!statistics || !source || kind >= PVDG_UI_EM500_STAT_COUNT) return;
    pvdg_ui_em500_stat_block_t *block = &statistics->blocks[kind];
    memset(block, 0, sizeof(*block));
    block->available = source->available;
    block->active_power_kw = map_measurement(
        source->available, source->stale, source->has_active_power_kw,
        source->active_power_kw, source->has_age_ms, source->age_ms);
    block->reactive_power_kvar = map_measurement(
        source->available, source->stale, source->has_reactive_power_kvar,
        source->reactive_power_kvar, source->has_age_ms, source->age_ms);
    block->apparent_power_kva = map_measurement(
        source->available, source->stale, source->has_apparent_power_kva,
        source->apparent_power_kva, source->has_age_ms, source->age_ms);
    block->power_factor = map_measurement(
        source->available, source->stale, source->has_power_factor,
        source->power_factor, source->has_age_ms, source->age_ms);
    block->frequency_hz = map_measurement(
        source->available, source->stale, source->has_frequency_hz,
        source->frequency_hz, source->has_age_ms, source->age_ms);
    block->voltage_phase_v = map_measurement(
        source->available, source->stale, source->has_voltage_phase_v,
        source->voltage_phase_v, source->has_age_ms, source->age_ms);
    block->current_a = map_measurement(
        source->available, source->stale, source->has_current_a,
        source->current_a, source->has_age_ms, source->age_ms);
}

const char *pvdg_ui_em500_stat_label(pvdg_ui_em500_stat_kind_t kind)
{
    switch (kind) {
    case PVDG_UI_EM500_STAT_MAXIMUM: return "Maximum / HI";
    case PVDG_UI_EM500_STAT_MINIMUM: return "Minimum / LO";
    case PVDG_UI_EM500_STAT_AVERAGE: return "Average";
    case PVDG_UI_EM500_STAT_DEMAND: return "Maximum demand";
    case PVDG_UI_EM500_STAT_COUNT:
    default: return "Unknown";
    }
}
