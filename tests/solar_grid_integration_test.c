#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "grid_control_gate.h"
#include "power_control_policy.h"
#include "source_mode.h"

static void expect_close(float actual, float expected)
{
    assert(isfinite(actual));
    assert(fabsf(actual - expected) < 0.001f);
}

static source_mode_result_t grid_source(bool fresh, bool available,
                                        bool breaker_closed)
{
    source_evidence_t evidence = {
        .evidence_fresh = fresh,
        .grid_available = available,
        .grid_breaker_closed = breaker_closed,
    };
    return source_mode_evaluate(&evidence);
}

static grid_gate_output_t gate_step(grid_gate_memory_t *memory,
                                    uint32_t timestamp_ms,
                                    bool configured,
                                    bool fresh,
                                    bool available,
                                    bool breaker_closed)
{
    source_mode_result_t source = grid_source(fresh, available, breaker_closed);
    grid_gate_input_t input = {
        .configured = configured,
        .evidence_fresh = fresh,
        .source_mode = source.mode,
        .source_control_allowed = source.control_allowed,
        .timestamp_ms = timestamp_ms,
        .loss_trip_ms = 250U,
        .recovery_stable_ms = 5000U,
    };
    return grid_control_gate_step(memory, &input);
}

static power_control_output_t policy_step(grid_policy_t policy,
                                          float measured_grid_kw,
                                          float current_pv_kw,
                                          float export_limit_kw,
                                          float minimum_import_kw)
{
    power_control_input_t input = {
        .measurement_fresh = true,
        .source_mode = SOURCE_MODE_GRID_ONLY,
        .policy = policy,
        .measured_grid_kw = measured_grid_kw,
        .export_limit_kw = export_limit_kw,
        .minimum_import_kw = minimum_import_kw,
        .current_pv_command_kw = current_pv_kw,
        .fleet_capacity_kw = 200.0f,
        .kp = 0.5f,
        .ki = 0.0f,
        .deadband_kw = 0.0f,
        .interval_seconds = 1.0f,
        .ramp_up_kw_per_second = 200.0f,
        .ramp_down_kw_per_second = 200.0f,
        .integral_kw = 0.0f,
        .generator_safe_limit_kw = 0.0f,
    };
    return power_control_step(&input);
}

static void test_grid_gate(void)
{
    grid_gate_memory_t memory = {0};

    grid_gate_output_t gate = gate_step(&memory, 0U, false, false, false, false);
    assert(gate.state == GRID_GATE_UNCONFIGURED);
    assert(!gate.control_allowed);

    gate = gate_step(&memory, 1000U, true, true, true, true);
    assert(gate.state == GRID_GATE_RECOVERY_STABILIZING);
    assert(!gate.control_allowed);

    gate = gate_step(&memory, 5999U, true, true, true, true);
    assert(gate.state == GRID_GATE_RECOVERY_STABILIZING);
    assert(!gate.control_allowed);

    gate = gate_step(&memory, 6000U, true, true, true, true);
    assert(gate.state == GRID_GATE_READY);
    assert(gate.control_allowed);
    assert(gate.recovery_stable);

    /* Communication loss blocks control immediately, before the outage is
     * classified as persistent. */
    gate = gate_step(&memory, 6001U, true, false, true, true);
    assert(gate.state == GRID_GATE_WAITING_EVIDENCE);
    assert(!gate.control_allowed);
    assert(!gate.loss_confirmed);

    gate = gate_step(&memory, 6251U, true, false, true, true);
    assert(gate.state == GRID_GATE_LOST);
    assert(!gate.control_allowed);
    assert(gate.loss_confirmed);

    /* Recovery must earn a new uninterrupted stabilization interval. */
    gate = gate_step(&memory, 7000U, true, true, true, true);
    assert(gate.state == GRID_GATE_RECOVERY_STABILIZING);
    assert(!gate.control_allowed);
    gate = gate_step(&memory, 11999U, true, true, true, true);
    assert(!gate.control_allowed);
    gate = gate_step(&memory, 12000U, true, true, true, true);
    assert(gate.state == GRID_GATE_READY);
    assert(gate.control_allowed);

    /* Closed breaker without grid availability is contradictory evidence. */
    gate = gate_step(&memory, 12001U, true, true, false, true);
    assert(gate.state == GRID_GATE_CONFLICT);
    assert(!gate.control_allowed);
}

static void test_policy_targets_and_load_steps(void)
{
    power_control_output_t output = policy_step(
        GRID_POLICY_MINIMUM_IMPORT, 20.0f, 30.0f, 0.0f, 5.0f);
    assert(output.valid);
    expect_close(output.target_grid_kw, 5.0f);
    expect_close(output.error_kw, 15.0f);
    expect_close(output.requested_pv_kw, 37.5f);

    output = policy_step(GRID_POLICY_ZERO_EXPORT, 10.0f, 40.0f, 0.0f, 0.0f);
    assert(output.valid);
    expect_close(output.target_grid_kw, 0.0f);
    expect_close(output.requested_pv_kw, 45.0f);

    output = policy_step(GRID_POLICY_LIMITED_EXPORT, -2.0f, 40.0f, 5.0f, 0.0f);
    assert(output.valid);
    expect_close(output.target_grid_kw, -5.0f);
    expect_close(output.error_kw, 3.0f);
    expect_close(output.requested_pv_kw, 41.5f);

    /* A load rejection toward export must curtail PV in the safe direction. */
    output = policy_step(GRID_POLICY_ZERO_EXPORT, -20.0f, 80.0f, 0.0f, 0.0f);
    assert(output.valid);
    expect_close(output.error_kw, -20.0f);
    expect_close(output.requested_pv_kw, 70.0f);
}

static void test_policy_blocks_without_evidence(void)
{
    power_control_input_t input = {
        .measurement_fresh = false,
        .source_mode = SOURCE_MODE_UNKNOWN,
        .policy = GRID_POLICY_ZERO_EXPORT,
        .measured_grid_kw = 10.0f,
        .fleet_capacity_kw = 100.0f,
        .interval_seconds = 1.0f,
        .ramp_up_kw_per_second = 10.0f,
        .ramp_down_kw_per_second = 20.0f,
    };
    power_control_output_t output = power_control_step(&input);
    assert(!output.valid);
    assert(output.transition_blocked);
    expect_close(output.requested_pv_kw, 0.0f);
}

/* The rate limiter must scale with the control interval, not be defined per
 * second and then silently shrink as the loop runs faster.
 *
 * The product requirement is that grid mode reaches the allowed target
 * immediately, which the firmware implements by handing the policy a rate that
 * covers the whole fleet range in ONE cycle. An earlier version handed it
 * "capacity per second" instead, so at the shipped 250 ms interval each cycle
 * moved only a quarter of the range -- and polling faster for fresher data made
 * the effective ramp slower as a fraction of range, which is exactly backwards.
 *
 * This test states the policy-level property both ways so a regression in either
 * the rate calculation or the limiter shows up here. */
static void test_rate_limit_scales_with_interval(void)
{
    const float capacity = 100.0f;
    const float intervals[] = {1.0f, 0.5f, 0.25f, 0.125f, 0.05f};

    for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        const float interval = intervals[i];
        power_control_input_t input = {
            .measurement_fresh = true,
            .source_mode = SOURCE_MODE_GRID_ONLY,
            .policy = GRID_POLICY_ZERO_EXPORT,
            .measured_grid_kw = 100.0f,   /* heavy import: the target is full PV */
            .current_pv_command_kw = 0.0f, /* starting from zero */
            .fleet_capacity_kw = capacity,
            .kp = 1.0f,
            .interval_seconds = interval,
            /* What the control engine supplies for a DISABLED ramp: full range
             * per CYCLE, i.e. capacity / interval. */
            .ramp_up_kw_per_second = capacity / interval,
            .ramp_down_kw_per_second = capacity / interval,
            .generator_safe_limit_kw = capacity,
        };
        power_control_output_t output = power_control_step(&input);
        assert(output.valid);
        /* One cycle must reach the target at every interval. */
        expect_close(output.requested_pv_kw, capacity);

        /* And the old formulation must still be genuinely rate limited, so this
         * test cannot pass for the wrong reason: "capacity per second" over a
         * sub-second interval moves only interval x capacity. */
        input.ramp_up_kw_per_second = capacity;
        input.ramp_down_kw_per_second = capacity;
        output = power_control_step(&input);
        assert(output.valid);
        expect_close(output.requested_pv_kw, capacity * interval);
    }

    /* An ENABLED ramp is a true rate: the same kW/s must move proportionally
     * more in a longer cycle, which is what makes it a commissioning value an
     * engineer can reason about independently of the poll rate. */
    power_control_input_t enabled = {
        .measurement_fresh = true,
        .source_mode = SOURCE_MODE_GENERATOR_ONLY,
        .policy = GRID_POLICY_ZERO_EXPORT,
        .measured_grid_kw = 100.0f,
        .current_pv_command_kw = 0.0f,
        .fleet_capacity_kw = capacity,
        .kp = 1.0f,
        .interval_seconds = 1.0f,
        .ramp_up_kw_per_second = 5.0f,   /* 5 %/s of a 100 kW fleet */
        .ramp_down_kw_per_second = 20.0f,
        .generator_safe_limit_kw = capacity,
    };
    power_control_output_t slow = power_control_step(&enabled);
    assert(slow.valid);
    expect_close(slow.requested_pv_kw, 5.0f);
    enabled.interval_seconds = 0.25f;
    power_control_output_t fast = power_control_step(&enabled);
    assert(fast.valid);
    expect_close(fast.requested_pv_kw, 1.25f);
}

int main(void)
{
    test_grid_gate();
    test_policy_targets_and_load_steps();
    test_policy_blocks_without_evidence();
    test_rate_limit_scales_with_interval();
    puts("Solar-Grid integration tests passed");
    return 0;
}
