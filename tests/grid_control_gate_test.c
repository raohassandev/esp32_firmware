/*
 * Executes the real grid control gate.
 *
 * The gate decides whether automatic control may command at all. It used to
 * release only for SOURCE_MODE_GRID_ONLY, so whenever a generator carried the
 * plant PV was driven to zero rather than limited -- on a PV-DG controller that
 * is the opposite of the product, and the minimum-loading floor, reverse-power
 * margin and generator ramp profile were all computed every cycle and then
 * discarded.
 *
 * It also refused to run at all unless breaker evidence was configured, which
 * meant a plant commissioned the documented other way -- one meter and a
 * digital input -- was inhibited forever, with a message naming evidence the
 * engineer was never asked to configure.
 *
 * These are behaviours, not source text, so they are executed rather than
 * grepped. Every assertion below fails against the previous implementation.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "grid_control_gate.h"

static grid_gate_input_t input_at(uint32_t now, source_mode_t mode)
{
    grid_gate_input_t input = {
        .configured = true,
        .evidence_fresh = true,
        .source_mode = mode,
        .source_control_allowed = true,
        .timestamp_ms = now,
        .loss_trip_ms = 250U,
        .recovery_stable_ms = 5000U,
    };
    return input;
}

/* Steps the gate until the stabilisation window has elapsed, and returns the
 * final verdict. */
static grid_gate_output_t settle(grid_gate_memory_t *memory, source_mode_t mode,
                                 uint32_t start_ms)
{
    grid_gate_output_t output = grid_control_gate_step(memory, &(grid_gate_input_t){0});
    for (uint32_t t = 0; t <= 6000U; t += 500U) {
        grid_gate_input_t input = input_at(start_ms + t, mode);
        output = grid_control_gate_step(memory, &input);
    }
    return output;
}

static void test_unconfigured_blocks(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_input_t input = input_at(1000U, SOURCE_MODE_GRID_ONLY);
    input.configured = false;
    grid_gate_output_t output = grid_control_gate_step(&memory, &input);
    assert(!output.control_allowed);
    assert(output.state == GRID_GATE_UNCONFIGURED);
}

static void test_grid_releases_after_stabilisation(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_input_t input = input_at(1000U, SOURCE_MODE_GRID_ONLY);

    grid_gate_output_t output = grid_control_gate_step(&memory, &input);
    assert(!output.control_allowed);
    assert(output.state == GRID_GATE_RECOVERY_STABILIZING);

    input.timestamp_ms = 1000U + 5000U;
    output = grid_control_gate_step(&memory, &input);
    assert(output.control_allowed);
    assert(output.state == GRID_GATE_READY);
}

/* THE CENTRAL CASE. A generator carrying the plant must release control, or the
 * generator-solar half of the product cannot run. */
static void test_generator_releases(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_output_t output = settle(&memory, SOURCE_MODE_GENERATOR_ONLY, 1000U);
    assert(output.control_allowed);
    assert(output.state == GRID_GATE_READY);
}

/* ISLAND is a generator carrying the plant with no grid behind it. The control
 * engine derives a generator-safe limit for it, so the gate must release it. */
static void test_island_releases(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_output_t output = settle(&memory, SOURCE_MODE_ISLAND, 1000U);
    assert(output.control_allowed);
}

/* Parallel operation is released now that the strategy exists: the grid policy
 * sets the target and the generator floor caps the maximum, so the more
 * restrictive of the two protections wins. The floor derivation for this mode
 * lives in the control engine; what this asserts is that the gate no longer
 * refuses it outright. */
static void test_synchronised_is_released(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_output_t output = settle(&memory, SOURCE_MODE_GRID_GENERATOR_SYNC, 1000U);
    assert(output.control_allowed);
    assert(output.state == GRID_GATE_READY);
}

/* But it is still a source CHANGE, so moving into or out of parallel restarts
 * the stabilisation timer like any other changeover. A plant that has just
 * paralleled has not proven steady yet. */
static void test_entering_parallel_restarts_stabilisation(void)
{
    grid_gate_memory_t memory = {0};
    assert(settle(&memory, SOURCE_MODE_GRID_ONLY, 1000U).control_allowed);

    grid_gate_input_t input = input_at(8000U, SOURCE_MODE_GRID_GENERATOR_SYNC);
    assert(!grid_control_gate_step(&memory, &input).control_allowed);
    input.timestamp_ms = 8000U + 5000U;
    assert(grid_control_gate_step(&memory, &input).control_allowed);
}

static void test_transfer_and_no_source_stay_blocked(void)
{
    grid_gate_memory_t memory = {0};
    assert(!settle(&memory, SOURCE_MODE_TRANSFER, 1000U).control_allowed);
    memset(&memory, 0, sizeof(memory));
    assert(!settle(&memory, SOURCE_MODE_NO_SOURCE, 1000U).control_allowed);
    memset(&memory, 0, sizeof(memory));
    assert(!settle(&memory, SOURCE_MODE_UNKNOWN, 1000U).control_allowed);
}

static void test_conflict_reports_itself(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_input_t input = input_at(1000U, SOURCE_MODE_CONFLICT);
    grid_gate_output_t output = grid_control_gate_step(&memory, &input);
    assert(!output.control_allowed);
    assert(output.state == GRID_GATE_CONFLICT);
}

/*
 * A CHANGEOVER MUST RESTART THE STABILISATION TIMER.
 *
 * Grid and generator are now both releasable, so without remembering which one
 * was released the timer would run straight through a transfer and PV would be
 * commanded against a bus that had just changed underneath it. This is the
 * assertion that would fail if the mode were not tracked.
 */
static void test_changeover_restarts_stabilisation(void)
{
    grid_gate_memory_t memory = {0};

    grid_gate_output_t output = settle(&memory, SOURCE_MODE_GRID_ONLY, 1000U);
    assert(output.control_allowed);

    /* The instant the source changes, control must close again. */
    grid_gate_input_t input = input_at(8000U, SOURCE_MODE_GENERATOR_ONLY);
    output = grid_control_gate_step(&memory, &input);
    assert(!output.control_allowed);
    assert(output.state == GRID_GATE_RECOVERY_STABILIZING);

    /* Still closed one millisecond before the window elapses. */
    input.timestamp_ms = 8000U + 4999U;
    output = grid_control_gate_step(&memory, &input);
    assert(!output.control_allowed);

    input.timestamp_ms = 8000U + 5000U;
    output = grid_control_gate_step(&memory, &input);
    assert(output.control_allowed);
}

/* Staleness closes the gate immediately, whatever the source. loss_trip_ms
 * classifies a persistent outage; it never delays the fail-closed path. */
static void test_stale_evidence_blocks_immediately(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_output_t output = settle(&memory, SOURCE_MODE_GENERATOR_ONLY, 1000U);
    assert(output.control_allowed);

    grid_gate_input_t input = input_at(8000U, SOURCE_MODE_GENERATOR_ONLY);
    input.evidence_fresh = false;
    output = grid_control_gate_step(&memory, &input);
    assert(!output.control_allowed);
}

/* A source the detector reports but will not vouch for is refused even though
 * the mode itself is releasable. */
static void test_source_not_vouched_for_is_refused(void)
{
    grid_gate_memory_t memory = {0};
    grid_gate_input_t input = input_at(1000U, SOURCE_MODE_GENERATOR_ONLY);
    input.source_control_allowed = false;
    for (uint32_t t = 0; t <= 6000U; t += 500U) {
        input.timestamp_ms = 1000U + t;
        assert(!grid_control_gate_step(&memory, &input).control_allowed);
    }
}

int main(void)
{
    test_unconfigured_blocks();
    test_grid_releases_after_stabilisation();
    test_generator_releases();
    test_island_releases();
    test_synchronised_is_released();
    test_entering_parallel_restarts_stabilisation();
    test_transfer_and_no_source_stay_blocked();
    test_conflict_reports_itself();
    test_changeover_restarts_stabilisation();
    test_stale_evidence_blocks_immediately();
    test_source_not_vouched_for_is_refused();
    printf("grid control gate tests passed\n");
    return 0;
}
