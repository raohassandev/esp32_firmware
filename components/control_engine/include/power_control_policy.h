#pragma once

#include <stdbool.h>
#include "source_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GRID_POLICY_ZERO_EXPORT = 0,
    GRID_POLICY_LIMITED_EXPORT,
    GRID_POLICY_MINIMUM_IMPORT
} grid_policy_t;

typedef struct {
    bool measurement_fresh;
    source_mode_t source_mode;
    grid_policy_t policy;
    float measured_grid_kw;       /* positive = import, negative = export */
    float export_limit_kw;        /* positive magnitude */
    float minimum_import_kw;
    float current_pv_command_kw;
    float fleet_capacity_kw;
    float kp;
    float ki;
    float deadband_kw;
    float interval_seconds;
    float ramp_up_kw_per_second;
    float ramp_down_kw_per_second;
    float integral_kw;
    float generator_safe_limit_kw;
} power_control_input_t;

typedef struct {
    bool valid;
    bool curtailed_by_generator;
    bool transition_blocked;
    float target_grid_kw;
    float error_kw;
    float requested_pv_kw;
    float next_integral_kw;
} power_control_output_t;

power_control_output_t power_control_step(const power_control_input_t *input);

#ifdef __cplusplus
}
#endif
