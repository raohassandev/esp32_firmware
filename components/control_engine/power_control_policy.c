#include "power_control_policy.h"
#include <math.h>

static float clamp_zero_max(float value, float maximum)
{
    if (!isfinite(value) || !isfinite(maximum) || maximum <= 0.0f) return 0.0f;
    if (value <= 0.0f) return 0.0f;
    return value > maximum ? maximum : value;
}

static bool source_mode_is_stable(source_mode_t mode)
{
    return mode == SOURCE_MODE_GRID_ONLY ||
           mode == SOURCE_MODE_GENERATOR_ONLY ||
           mode == SOURCE_MODE_GRID_GENERATOR_SYNC ||
           mode == SOURCE_MODE_ISLAND;
}

static float target_grid_kw(const power_control_input_t *input)
{
    switch (input->policy) {
        case GRID_POLICY_LIMITED_EXPORT:
            return -fabsf(input->export_limit_kw);
        case GRID_POLICY_MINIMUM_IMPORT:
            return fmaxf(0.0f, input->minimum_import_kw);
        case GRID_POLICY_ZERO_EXPORT:
        default:
            return 0.0f;
    }
}

power_control_output_t power_control_step(const power_control_input_t *input)
{
    power_control_output_t output = {0};
    if (!input || !input->measurement_fresh || !source_mode_is_stable(input->source_mode) ||
        !isfinite(input->measured_grid_kw) || !isfinite(input->current_pv_command_kw) ||
        !isfinite(input->fleet_capacity_kw) || input->fleet_capacity_kw <= 0.0f ||
        !isfinite(input->kp) || input->kp < 0.0f ||
        !isfinite(input->ki) || input->ki < 0.0f ||
        !isfinite(input->deadband_kw) || input->deadband_kw < 0.0f ||
        !isfinite(input->interval_seconds) || input->interval_seconds <= 0.0f ||
        !isfinite(input->ramp_up_kw_per_second) || input->ramp_up_kw_per_second < 0.0f ||
        !isfinite(input->ramp_down_kw_per_second) || input->ramp_down_kw_per_second < 0.0f ||
        !isfinite(input->integral_kw) ||
        !isfinite(input->export_limit_kw) || input->export_limit_kw < 0.0f ||
        !isfinite(input->minimum_import_kw) || input->minimum_import_kw < 0.0f) {
        output.transition_blocked = true;
        return output;
    }

    const bool generator_mode = input->source_mode == SOURCE_MODE_GENERATOR_ONLY ||
                                input->source_mode == SOURCE_MODE_ISLAND ||
                                input->source_mode == SOURCE_MODE_GRID_GENERATOR_SYNC;
    float maximum = input->fleet_capacity_kw;
    if (generator_mode) {
        if (!isfinite(input->generator_safe_limit_kw) || input->generator_safe_limit_kw < 0.0f) {
            output.transition_blocked = true;
            return output;
        }
        if (input->generator_safe_limit_kw < maximum) {
            maximum = input->generator_safe_limit_kw;
            output.curtailed_by_generator = true;
        }
    }

    output.target_grid_kw = target_grid_kw(input);
    output.error_kw = input->measured_grid_kw - output.target_grid_kw;
    if (fabsf(output.error_kw) <= input->deadband_kw) output.error_kw = 0.0f;

    float candidate_integral = input->integral_kw + input->ki * output.error_kw * input->interval_seconds;
    if (!isfinite(candidate_integral)) return (power_control_output_t){0};
    if (candidate_integral > maximum) candidate_integral = maximum;
    if (candidate_integral < -maximum) candidate_integral = -maximum;

    float unconstrained = input->current_pv_command_kw + input->kp * output.error_kw + candidate_integral;
    float constrained = clamp_zero_max(unconstrained, maximum);

    /* Conditional integration: do not wind the integral further into an active
     * output limit. Preserve the previous integral when saturation and error
     * point in the same direction. */
    bool upper_windup = unconstrained > maximum && output.error_kw > 0.0f;
    bool lower_windup = unconstrained < 0.0f && output.error_kw < 0.0f;
    output.next_integral_kw = (upper_windup || lower_windup) ? input->integral_kw : candidate_integral;

    float upward_step = input->ramp_up_kw_per_second * input->interval_seconds;
    float downward_step = input->ramp_down_kw_per_second * input->interval_seconds;
    float lower = input->current_pv_command_kw - downward_step;
    float upper = input->current_pv_command_kw + upward_step;
    if (lower < 0.0f) lower = 0.0f;
    if (upper > maximum) upper = maximum;
    if (constrained < lower) constrained = lower;
    if (constrained > upper) constrained = upper;

    output.requested_pv_kw = clamp_zero_max(constrained, maximum);
    output.valid = true;
    return output;
}
