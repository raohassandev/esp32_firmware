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

    /* Generator-only operation does not chase a grid PI target. It rises toward
     * the safe generator-fleet ceiling and ignores a missing grid measurement. */
    in = base_input();
    in.source_mode = SOURCE_MODE_GENERATOR_ONLY;
    in.generator_safe_limit_kw = 100.0f;
    in.current_pv_command_kw = 50.0f;
    in.measured_grid_kw = NAN;
    in.integral_kw = 75.0f;
    out = power_control_step(&in);
    assert(out.valid);
    assert(out.error_kw == 0.0f && out.next_integral_kw == 0.0f);
    assert(out.requested_pv_kw > 50.0f && out.requested_pv_kw <= 60.0f + 0.001f);

    /* A falling generator-safe ceiling is a safety clamp, not a normal ramp:
     * curtail in the same cycle even if the configured down-ramp is slow. */
    in = base_input();
    in.source_mode = SOURCE_MODE_GENERATOR_ONLY;
    in.generator_safe_limit_kw = 30.0f;
    in.current_pv_command_kw = 80.0f;
    in.ramp_down_kw_per_second = 1.0f;
    in.measured_grid_kw = NAN;
    out = power_control_step(&in);
    assert(out.valid && out.curtailed_by_generator);
    assert(out.requested_pv_kw <= 30.0f + 0.001f);

    in = base_input();
    in.source_mode = SOURCE_MODE_ISLAND;
    in.generator_safe_limit_kw = 0.0f;
    in.current_pv_command_kw = 150.0f;
    in.measured_grid_kw = NAN;
    out = power_control_step(&in);
    assert(out.valid && out.requested_pv_kw == 0.0f);

    /* Synchronized Grid+Generator operation still needs grid measurement because
     * grid-exchange policy and the generator safe ceiling apply simultaneously. */
    in = base_input();
    in.source_mode = SOURCE_MODE_GRID_GENERATOR_SYNC;
    in.generator_safe_limit_kw = 100.0f;
    in.measured_grid_kw = NAN;
    out = power_control_step(&in);
    assert(!out.valid && out.transition_blocked);

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

    /* Phase 4: every unsettled source must zero the command in the same cycle,
     * not ramp down and not hold the previous value. A source change always
     * passes through one of these while the debounce runs, so this is what
     * stops PV being held on a machine that has just been connected. */
    const source_mode_t unsettled[] = {
        SOURCE_MODE_UNKNOWN,
        SOURCE_MODE_NO_SOURCE,
        SOURCE_MODE_TRANSFER,
        SOURCE_MODE_CONFLICT,
    };
    for (unsigned i = 0; i < sizeof(unsettled) / sizeof(unsettled[0]); ++i) {
        in = base_input();
        in.source_mode = unsettled[i];
        in.current_pv_command_kw = 150.0f;   /* was carrying real output */
        in.ramp_down_kw_per_second = 1.0f;   /* a slow ramp must not soften this */
        out = power_control_step(&in);
        assert(!out.valid);
        assert(out.requested_pv_kw == 0.0f);
    }

    puts("power control policy tests passed");
    return 0;
}
