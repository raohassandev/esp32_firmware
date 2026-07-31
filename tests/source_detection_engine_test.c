#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "source_detection_engine.h"

static source_detection_policy_t single_policy(void)
{
    source_detection_policy_t policy = {
        .mode = SOURCE_DETECTION_MODE_SINGLE_INPUT,
        .debounce_ms = 100U,
        .stale_timeout_ms = 500U,
        .single_grid_value = 0U,
        .single_generator_value = 1U,
        .grid_threshold_kw = 1.0f,
        .generator_threshold_kw = 1.0f,
    };
    return policy;
}

static source_detection_policy_t dual_policy(void)
{
    source_detection_policy_t policy = single_policy();
    policy.mode = SOURCE_DETECTION_MODE_DUAL_METER;
    return policy;
}

static source_detection_result_t settle(
    source_detection_memory_t *memory,
    const source_detection_policy_t *policy,
    const source_detection_evidence_t *evidence,
    uint32_t start_ms)
{
    source_detection_result_t first = source_detection_step(memory, policy, evidence, start_ms);
    assert(first.state == SOURCE_STATE_UNKNOWN);
    assert(first.reason == SOURCE_REASON_DEBOUNCE_PENDING);
    return source_detection_step(memory, policy, evidence, start_ms + policy->debounce_ms);
}

static void test_single_grid_generator_unknown(void)
{
    source_detection_policy_t policy = single_policy();
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .single_has_sample = true,
        .single_raw_value = 0U,
        .single_age_ms = 0U,
    };

    source_detection_result_t result = settle(&memory, &policy, &evidence, 0U);
    assert(result.state == SOURCE_STATE_GRID);
    assert(result.tariff == SOURCE_TARIFF_1);
    assert(result.control_allowed);

    evidence.single_raw_value = 1U;
    result = source_detection_step(&memory, &policy, &evidence, 200U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_DEBOUNCE_PENDING);
    assert(result.fail_closed);
    result = source_detection_step(&memory, &policy, &evidence, 300U);
    assert(result.state == SOURCE_STATE_GENERATOR);
    assert(result.tariff == SOURCE_TARIFF_2);

    /* 0x2100 is a bitmask of all digital inputs, so a second wired input makes
     * the generator read 3 or 7 rather than 1. With a zero grid value every
     * non-zero word is generator, and because the plant is ALREADY generator
     * here the state must hold without re-debouncing -- the machine must not
     * lose its protection because another input changed. */
    evidence.single_raw_value = 7U;
    result = source_detection_step(&memory, &policy, &evidence, 301U);
    assert(result.state == SOURCE_STATE_GENERATOR);
    assert(result.candidate_state == SOURCE_STATE_GENERATOR);
    assert(result.tariff == SOURCE_TARIFF_2);
    assert(result.control_allowed);
    assert(!result.fail_closed);

    evidence.single_raw_value = 0x8000U;
    result = source_detection_step(&memory, &policy, &evidence, 302U);
    assert(result.state == SOURCE_STATE_GENERATOR);

    evidence.single_raw_value = 0xFFFFU;
    result = source_detection_step(&memory, &policy, &evidence, 303U);
    assert(result.state == SOURCE_STATE_GENERATOR);
}

/* A site that commissions a NON-ZERO grid value is not describing a bitmask, so
 * the bitmask rule must not touch it: strict equality still applies and an
 * unmodelled word is still refused rather than guessed as generator. */
static void test_single_non_zero_grid_value_keeps_strict_equality(void)
{
    source_detection_policy_t policy = single_policy();
    policy.single_grid_value = 1U;
    policy.single_generator_value = 2U;
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .single_has_sample = true,
        .single_raw_value = 1U,
        .single_age_ms = 0U,
    };

    source_detection_result_t result = settle(&memory, &policy, &evidence, 0U);
    assert(result.state == SOURCE_STATE_GRID);

    evidence.single_raw_value = 2U;
    result = source_detection_step(&memory, &policy, &evidence, 100U);
    result = source_detection_step(&memory, &policy, &evidence, 300U);
    assert(result.state == SOURCE_STATE_GENERATOR);

    evidence.single_raw_value = 7U;
    result = source_detection_step(&memory, &policy, &evidence, 301U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_UNKNOWN_INPUT_VALUE);
    assert(result.tariff == SOURCE_TARIFF_NONE);
    assert(result.fail_closed);

    /* Zero is not the commissioned grid value on such a site either. */
    evidence.single_raw_value = 0U;
    result = source_detection_step(&memory, &policy, &evidence, 302U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_UNKNOWN_INPUT_VALUE);
}

static void test_dual_meter_states(void)
{
    source_detection_policy_t policy = dual_policy();
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .grid_has_sample = true,
        .grid_power_kw = 5.0f,
        .grid_age_ms = 0U,
        .generator_has_sample = true,
        .generator_power_kw = 0.2f,
        .generator_age_ms = 0U,
    };

    source_detection_result_t result = settle(&memory, &policy, &evidence, 0U);
    assert(result.state == SOURCE_STATE_GRID);
    assert(result.tariff == SOURCE_TARIFF_1);

    source_detection_reset(&memory);
    evidence.grid_power_kw = 0.2f;
    evidence.generator_power_kw = 4.0f;
    result = settle(&memory, &policy, &evidence, 200U);
    assert(result.state == SOURCE_STATE_GENERATOR);
    assert(result.tariff == SOURCE_TARIFF_2);

    evidence.grid_power_kw = 3.0f;
    evidence.generator_power_kw = 4.0f;
    result = source_detection_step(&memory, &policy, &evidence, 400U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_CONFLICT);
    assert(result.conflict);
    assert(result.fail_closed);

    evidence.grid_power_kw = 0.3f;
    evidence.generator_power_kw = 0.4f;
    result = source_detection_step(&memory, &policy, &evidence, 401U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_NO_SOURCE);
}

static void test_stale_and_non_finite(void)
{
    source_detection_policy_t policy = dual_policy();
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .grid_has_sample = true,
        .grid_power_kw = 3.0f,
        .grid_age_ms = 501U,
        .generator_has_sample = true,
        .generator_power_kw = 0.0f,
        .generator_age_ms = 0U,
    };

    source_detection_result_t result = source_detection_step(&memory, &policy, &evidence, 0U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_EVIDENCE_STALE);

    evidence.grid_age_ms = 0U;
    evidence.grid_power_kw = NAN;
    result = source_detection_step(&memory, &policy, &evidence, 1U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_NON_FINITE);
    assert(result.fail_closed);
}

/* Absent evidence must be reported as unavailable, not silently treated as a
 * reading of zero. Named in the phase 1 acceptance criteria. */
static void test_evidence_unavailable(void)
{
    source_detection_policy_t single = single_policy();
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .single_has_sample = false,
        .single_raw_value = 0U,
        .single_age_ms = 0U,
    };

    source_detection_result_t result = source_detection_step(&memory, &single, &evidence, 0U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_EVIDENCE_UNAVAILABLE);
    assert(result.fail_closed);
    assert(result.tariff == SOURCE_TARIFF_NONE);

    /* A raw value matching the configured grid value must still not resolve
     * while the sample itself is absent. */
    evidence.single_raw_value = 0U;
    result = source_detection_step(&memory, &single, &evidence, 100U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_EVIDENCE_UNAVAILABLE);

    source_detection_policy_t dual = dual_policy();
    source_detection_memory_t dual_memory = {0};
    source_detection_evidence_t dual_evidence = {
        .grid_has_sample = false,
        .generator_has_sample = false,
    };
    result = source_detection_step(&dual_memory, &dual, &dual_evidence, 0U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    assert(result.reason == SOURCE_REASON_EVIDENCE_UNAVAILABLE);
    assert(result.fail_closed);
}

static void test_flapping_input_never_settles(void)
{
    source_detection_policy_t policy = single_policy();
    source_detection_memory_t memory = {0};
    source_detection_evidence_t evidence = {
        .single_has_sample = true,
        .single_age_ms = 0U,
    };

    for (uint32_t now = 0U; now < 500U; now += 40U) {
        evidence.single_raw_value = ((now / 40U) % 2U) == 0U ? 0U : 1U;
        source_detection_result_t result = source_detection_step(&memory, &policy, &evidence, now);
        assert(result.state == SOURCE_STATE_UNKNOWN);
        assert(result.fail_closed);
    }

    evidence.single_raw_value = 1U;
    source_detection_result_t result = source_detection_step(&memory, &policy, &evidence, 520U);
    assert(result.state == SOURCE_STATE_UNKNOWN);
    result = source_detection_step(&memory, &policy, &evidence, 620U);
    assert(result.state == SOURCE_STATE_GENERATOR);
}

int main(void)
{
    test_single_grid_generator_unknown();
    test_single_non_zero_grid_value_keeps_strict_equality();
    test_dual_meter_states();
    test_stale_and_non_finite();
    test_evidence_unavailable();
    test_flapping_input_never_settles();
    puts("EM500 source detection unit tests passed");
    return 0;
}
