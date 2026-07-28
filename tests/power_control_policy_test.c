#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "power_control_policy.h"

static power_control_input_t base_input(void)
{
    return (power_control_input_t){
        .measurement_fresh = true,
        .source_mode = SOURCE_MODE_GRID_ONLY,
        .policy = GRID_POLICY_ZERO_EXPORT,
        .measured_grid_kw = 20.0f,
        .current_pv_command_kw = 50.0f,
        .fleet_capacity_kw = 200.0f,
        .kp = 0.5f,
        .ki = 0.1f,
        .deadband_kw = 1.0f,
        .interval_seconds = 1.0f,
        .ramp_up_kw_per_second = 10.0f,
        .ramp_down_kw_per_second = 40.0f,
        .integral_kw = 0.0f,
        .generator_safe_limit_kw = 0.0f,
    };
}

int main(void)
{
    power_control_input_t in = base_input();
    power_control_output_t out = power_control_step(&in);
    assert(out.valid);
    assert(out.target_grid_kw == 0.0f);
    assert(out.requested_pv_kw > in.current_pv_command_kw);
    assert(out.requested_pv_kw <= 60.0f + 0.001f);

    in = base_input();
    in.policy = GRID_POLICY_LIMITED_EXPORT;
    in.export_limit_kw = 15.0f;
    out = power_control_step(&in);
    assert(out.valid && fabsf(out.target_grid_kw + 15.0f) < 0.001f);

    in = base_input();
    in.policy = GRID_POLICY_MINIMUM_IMPORT;
    in.minimum_import_kw = 5.0f;
    in.measured_grid_kw = 5.5f;
    out = power_control_step(&in);
    assert(out.valid && out.error_kw == 0.0f);

    in = base_input();
    in.source_mode = SOURCE_MODE_GENERATOR_ONLY;
    in.generator_safe_limit_kw = 30.0f;
    in.current_pv_command_kw = 80.0f;
    out = power_control_step(&in);
    assert(out.valid && out.curtailed_by_generator);
    assert(out.requested_pv_kw <= 30.0f + 0.001f);

    in = base_input();
    in.source_mode = SOURCE_MODE_TRANSFER;
    out = power_control_step(&in);
    assert(!out.valid && out.transition_blocked && out.requested_pv_kw == 0.0f);

    in = base_input();
    in.measured_grid_kw = NAN;
    out = power_control_step(&in);
    assert(!out.valid && out.requested_pv_kw == 0.0f);

    in = base_input();
    in.measured_grid_kw = -100.0f;
    in.current_pv_command_kw = 50.0f;
    out = power_control_step(&in);
    assert(out.valid);
    assert(out.requested_pv_kw >= 10.0f - 0.001f); /* ramp-down limit */
    assert(out.requested_pv_kw < 50.0f);

    in = base_input();
    in.measured_grid_kw = 1000.0f;
    in.current_pv_command_kw = 195.0f;
    in.integral_kw = 100.0f;
    out = power_control_step(&in);
    assert(out.valid);
    assert(out.requested_pv_kw <= 200.0f);
    assert(out.next_integral_kw == in.integral_kw); /* anti-windup */

    puts("power control policy tests passed");
    return 0;
}
