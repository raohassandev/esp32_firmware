#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_MODE_DISABLED = 0,
    APP_MODE_GRID,
    APP_MODE_GENERATOR,
    APP_MODE_MANUAL,
    APP_MODE_FAILSAFE,
    APP_MODE_EMERGENCY
} app_mode_t;

typedef struct {
    bool enabled;
    app_mode_t mode;
    float grid_power_kw;
    float grid_target_kw;
    float error_kw;
    float requested_pv_kw;
    float applied_pv_kw;
    uint32_t alarm_flags;
    uint32_t last_cycle_ms;
} control_status_t;
