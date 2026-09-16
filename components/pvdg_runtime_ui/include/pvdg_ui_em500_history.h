#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pvdg_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PVDG_UI_EM500_STAT_MAXIMUM = 0,
    PVDG_UI_EM500_STAT_MINIMUM,
    PVDG_UI_EM500_STAT_AVERAGE,
    PVDG_UI_EM500_STAT_DEMAND,
    PVDG_UI_EM500_STAT_COUNT
} pvdg_ui_em500_stat_kind_t;

typedef struct {
    bool available;
    bool stale;
    bool has_age_ms;
    uint32_t age_ms;
    bool has_active_power_kw;
    double active_power_kw;
    bool has_reactive_power_kvar;
    double reactive_power_kvar;
    bool has_apparent_power_kva;
    double apparent_power_kva;
    bool has_power_factor;
    double power_factor;
    bool has_frequency_hz;
    double frequency_hz;
    bool has_voltage_phase_v;
    double voltage_phase_v;
    bool has_current_a;
    double current_a;
} pvdg_ui_firmware_em500_stat_block_t;

typedef struct {
    bool available;
    pvdg_ui_measurement_t active_power_kw;
    pvdg_ui_measurement_t reactive_power_kvar;
    pvdg_ui_measurement_t apparent_power_kva;
    pvdg_ui_measurement_t power_factor;
    pvdg_ui_measurement_t frequency_hz;
    pvdg_ui_measurement_t voltage_phase_v;
    pvdg_ui_measurement_t current_a;
} pvdg_ui_em500_stat_block_t;

typedef struct {
    pvdg_ui_em500_stat_block_t blocks[PVDG_UI_EM500_STAT_COUNT];
} pvdg_ui_em500_statistics_t;

void pvdg_ui_em500_statistics_reset(pvdg_ui_em500_statistics_t *statistics);
void pvdg_ui_em500_statistics_apply_block(
    pvdg_ui_em500_statistics_t *statistics,
    pvdg_ui_em500_stat_kind_t kind,
    const pvdg_ui_firmware_em500_stat_block_t *source);
const char *pvdg_ui_em500_stat_label(pvdg_ui_em500_stat_kind_t kind);

#ifdef __cplusplus
}
#endif
