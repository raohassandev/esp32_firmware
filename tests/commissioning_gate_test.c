/* Host-compiled unit test for the commissioning gate (P0-6).
 *
 * This executes the real evaluator; it does not grep source. The behaviour it
 * pins down is the safety property: unknown or unreadable state must never
 * evaluate to commissioned. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "commissioning_gate.h"

/* A fully commissioned plant. Every test below starts from this and breaks
 * exactly one thing, so a failure names the prerequisite that regressed. */
static commissioning_inputs_t good_inputs(void)
{
    commissioning_inputs_t in;
    memset(&in, 0, sizeof(in));
    in.state_readable = true;

    in.meter_roles_known = true;
    in.meter_roles_valid = true;
    in.grid_meter_count = 1;
    in.duplicate_generator_slot = false;

    in.inverter_fleet_known = true;
    in.enabled_inverter_count = 3;
    in.write_qualified_inverter_count = 3;
    in.readback_capable_inverter_count = 3;
    in.commissioned_capacity_kw = 300.0f;

    in.ramp_policy_known = true;
    in.generator_ramp_enabled = true;
    in.generator_ramp_up_percent_per_second = 5.0f;
    in.generator_ramp_down_percent_per_second = 20.0f;

    in.source_detection_known = true;
    in.source_detection_configured = false;
    in.grid_evidence_configured = true;

    in.grid_policy_known = true;
    in.grid_policy_valid = true;

    in.generator_limits_known = true;
    in.generator_rated_kw = 500.0f;
    in.generator_minimum_loading_percent = 30.0f;

    in.control_tuning_known = true;
    in.kp = 0.8f;
    in.ki = 0.1f;
    in.deadband_kw = 1.0f;
    in.interval_ms = 500;
    in.meter_stale_timeout_ms = 3000;
    return in;
}

static bool prereq_satisfied(const commissioning_status_t *status,
                             commissioning_prereq_t prereq)
{
    return status->results[prereq].satisfied;
}

static uint8_t prereq_reason(const commissioning_status_t *status,
                             commissioning_prereq_t prereq)
{
    return status->results[prereq].reason;
}

static void test_zeroed_input_is_never_commissioned(void)
{
    commissioning_inputs_t in;
    memset(&in, 0, sizeof(in));
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(status.unmet_count == COMMISSIONING_PREREQ_COUNT);
    assert(status.satisfied_count == 0);
    for (uint8_t i = 0; i < COMMISSIONING_PREREQ_COUNT; ++i) {
        assert(!status.results[i].satisfied);
        assert(status.results[i].reason == COMMISSIONING_REASON_STATE_UNREADABLE);
    }
    assert(commissioning_gate_summary(&status)[0] != '\0');
}

static void test_null_input_is_never_commissioned(void)
{
    commissioning_status_t status = commissioning_gate_evaluate(NULL);
    assert(!status.commissioned);
    assert(status.unmet_count == COMMISSIONING_PREREQ_COUNT);
    assert(commissioning_gate_summary(NULL)[0] != '\0');
}

static void test_fully_commissioned(void)
{
    commissioning_inputs_t in = good_inputs();
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.unmet_count == 0);
    assert(status.satisfied_count == COMMISSIONING_PREREQ_COUNT);
    assert(commissioning_gate_summary(&status)[0] == '\0');
}

/* Every single "known" flag, cleared on its own, must close the gate. This is
 * the fail-closed property stated as an exhaustive test rather than a comment. */
static void test_each_unknown_group_closes_the_gate(void)
{
    struct {
        size_t offset;
        commissioning_prereq_t prereq;
    } const groups[] = {
        {offsetof(commissioning_inputs_t, meter_roles_known), COMMISSIONING_PREREQ_METER_ROLES},
        {offsetof(commissioning_inputs_t, inverter_fleet_known), COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED},
        {offsetof(commissioning_inputs_t, ramp_policy_known), COMMISSIONING_PREREQ_RAMP_POLICY},
        {offsetof(commissioning_inputs_t, source_detection_known), COMMISSIONING_PREREQ_SOURCE_DETECTION},
        {offsetof(commissioning_inputs_t, grid_policy_known), COMMISSIONING_PREREQ_GRID_POLICY},
        {offsetof(commissioning_inputs_t, generator_limits_known), COMMISSIONING_PREREQ_GENERATOR_LIMITS},
        {offsetof(commissioning_inputs_t, control_tuning_known), COMMISSIONING_PREREQ_CONTROL_TUNING},
    };

    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
        commissioning_inputs_t in = good_inputs();
        *(bool *)((char *)&in + groups[i].offset) = false;
        commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(!prereq_satisfied(&status, groups[i].prereq));
        assert(prereq_reason(&status, groups[i].prereq) == COMMISSIONING_REASON_STATE_UNREADABLE);
    }
}

static void test_meter_roles(void)
{
    commissioning_inputs_t in = good_inputs();
    in.grid_meter_count = 0;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_METER_ROLES) ==
           COMMISSIONING_REASON_GRID_METER_MISSING);

    in = good_inputs();
    in.grid_meter_count = 2;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_METER_ROLES) ==
           COMMISSIONING_REASON_GRID_METER_AMBIGUOUS);

    in = good_inputs();
    in.duplicate_generator_slot = true;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_METER_ROLES) ==
           COMMISSIONING_REASON_GENERATOR_SLOT_DUPLICATE);

    /* Count looks right but the resolver itself said the assignment is not
     * usable. The resolver wins. */
    in = good_inputs();
    in.meter_roles_valid = false;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_METER_ROLES));
}

static void test_partially_qualified_fleet_is_rejected(void)
{
    commissioning_inputs_t in = good_inputs();
    in.write_qualified_inverter_count = 2; /* one of three unqualified */
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED) ==
           COMMISSIONING_REASON_PROFILE_NOT_WRITE_QUALIFIED);

    in = good_inputs();
    in.enabled_inverter_count = 0;
    in.write_qualified_inverter_count = 0;
    in.readback_capable_inverter_count = 0;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED) ==
           COMMISSIONING_REASON_NO_ENABLED_INVERTER);
}

/* P0-9 tied into P0-6: a fleet whose commands can never be confirmed is not a
 * commissioned fleet. */
static void test_missing_readback_blocks_commissioning(void)
{
    commissioning_inputs_t in = good_inputs();
    in.readback_capable_inverter_count = 2;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_WRITE_READBACK) ==
           COMMISSIONING_REASON_READBACK_UNAVAILABLE);
}

static void test_capacity(void)
{
    const float bad[] = {0.0f, -10.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        commissioning_inputs_t in = good_inputs();
        in.commissioned_capacity_kw = bad[i];
        commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_FLEET_CAPACITY) ==
               COMMISSIONING_REASON_CAPACITY_NOT_COMMISSIONED);
    }
}

static void test_ramp_policy(void)
{
    /* A disabled generator ramp is a deliberate engineering decision and is not
     * inherited from a default: the gate refuses it. */
    commissioning_inputs_t in = good_inputs();
    in.generator_ramp_enabled = false;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_RAMP_POLICY) ==
           COMMISSIONING_REASON_RAMP_POLICY_INVALID);

    /* Down slower than up leaves a genset exposed in the direction that matters. */
    in = good_inputs();
    in.generator_ramp_down_percent_per_second = 1.0f;
    in.generator_ramp_up_percent_per_second = 5.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));

    in = good_inputs();
    in.generator_ramp_up_percent_per_second = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));

    in = good_inputs();
    in.generator_ramp_down_percent_per_second = NAN;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));

    /* Equal rates are acceptable. */
    in = good_inputs();
    in.generator_ramp_up_percent_per_second = 10.0f;
    in.generator_ramp_down_percent_per_second = 10.0f;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));
}

static void test_source_detection_accepts_either_evidence_path(void)
{
    commissioning_inputs_t in = good_inputs();
    in.grid_evidence_configured = false;
    in.source_detection_configured = false;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_SOURCE_DETECTION) ==
           COMMISSIONING_REASON_SOURCE_EVIDENCE_UNCONFIGURED);

    in.source_detection_configured = true;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_SOURCE_DETECTION));
    assert(status.commissioned);

    in.source_detection_configured = false;
    in.grid_evidence_configured = true;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_SOURCE_DETECTION));
}

static void test_generator_limits(void)
{
    commissioning_inputs_t in = good_inputs();
    in.generator_rated_kw = 0.0f;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);

    in = good_inputs();
    in.generator_minimum_loading_percent = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN);

    in = good_inputs();
    in.generator_minimum_loading_percent = 140.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS));
}

static void test_control_tuning(void)
{
    commissioning_inputs_t in = good_inputs();
    in.kp = 0.0f;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_CONTROL_TUNING) ==
           COMMISSIONING_REASON_CONTROL_TUNING_INVALID);

    /* The control period bound is 10 ms, matching the 1 ms scheduler tick. A fast
     * loop is the product requirement -- a real meter answers in under 40 ms, so a
     * 100 ms floor discarded most of that responsiveness. Below the bound is a
     * configuration error, not a faster loop. */
    in = good_inputs();
    in.interval_ms = 9;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    in = good_inputs();
    in.interval_ms = 0; /* not a period at all */
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    /* 10 ms and a genuinely fast 20 ms must both be accepted. */
    in = good_inputs();
    in.interval_ms = 10;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    in = good_inputs();
    in.interval_ms = 20;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    /* Still bounded above. */
    in = good_inputs();
    in.interval_ms = 10001;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    /* A stale timeout shorter than the control period can never be met. */
    in = good_inputs();
    in.meter_stale_timeout_ms = 100;
    in.interval_ms = 500;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    in = good_inputs();
    in.ki = NAN;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));
}

/* first_unmet must be the lowest-numbered failure so the interface always
 * points the engineer at one actionable item rather than a list. */
static void test_first_unmet_is_lowest_index(void)
{
    commissioning_inputs_t in = good_inputs();
    in.grid_meter_count = 0;              /* prerequisite 0 */
    in.generator_rated_kw = 0.0f;         /* prerequisite 7 */
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.first_unmet == COMMISSIONING_PREREQ_METER_ROLES);
    assert(status.unmet_count == 2);

    in = good_inputs();
    in.generator_rated_kw = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(status.first_unmet == COMMISSIONING_PREREQ_GENERATOR_LIMITS);
    assert(status.unmet_count == 1);
    assert(status.satisfied_count == COMMISSIONING_PREREQ_COUNT - 1);
}

/* Every enumerated value must carry API text; a silent "unknown" in the UI is a
 * defect, and out-of-range values must not read out of bounds. */
static void test_labels_are_complete_and_bounded(void)
{
    for (uint8_t i = 0; i < COMMISSIONING_PREREQ_COUNT; ++i) {
        assert(commissioning_prereq_id(i)[0] != '\0');
        assert(strcmp(commissioning_prereq_id(i), "unknown") != 0);
        assert(commissioning_prereq_title(i)[0] != '\0');
    }
    assert(strcmp(commissioning_prereq_id(COMMISSIONING_PREREQ_COUNT), "unknown") == 0);
    assert(strcmp(commissioning_prereq_id(200), "unknown") == 0);

    for (uint8_t i = 1; i < COMMISSIONING_REASON_COUNT; ++i) {
        assert(commissioning_reason_id(i)[0] != '\0');
        assert(commissioning_reason_message(i)[0] != '\0');
    }
    assert(commissioning_reason_message(COMMISSIONING_REASON_SATISFIED)[0] == '\0');
    assert(strcmp(commissioning_reason_id(COMMISSIONING_REASON_COUNT), "unknown") == 0);
    assert(commissioning_reason_message(250)[0] != '\0');

    /* Prerequisite slugs must be unique: they are the API keys. */
    for (uint8_t a = 0; a < COMMISSIONING_PREREQ_COUNT; ++a) {
        for (uint8_t b = (uint8_t)(a + 1); b < COMMISSIONING_PREREQ_COUNT; ++b) {
            assert(strcmp(commissioning_prereq_id(a), commissioning_prereq_id(b)) != 0);
        }
    }
}

/* Lab-target declarations satisfy the profile prerequisite, but must never be
 * reported as production commissioning. These execute the real evaluator. */
static void test_lab_scope(void)
{
    /* A fully production-qualified fleet is PRODUCTION scope. */
    commissioning_inputs_t in = good_inputs();
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_PRODUCTION);

    /* An entirely simulated fleet commissions, but only for the lab. */
    in = good_inputs();
    in.write_qualified_inverter_count = 0;
    in.lab_only_inverter_count = in.enabled_inverter_count;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_LAB);

    /* One simulator among production-qualified machines drags the whole verdict
     * down to LAB. The weakest link decides. */
    in = good_inputs();
    in.write_qualified_inverter_count = (uint8_t)(in.enabled_inverter_count - 1);
    in.lab_only_inverter_count = 1;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_LAB);

    /* Counts that do not cover every enabled inverter still fail, whichever
     * kind is short. */
    in = good_inputs();
    in.write_qualified_inverter_count = 1;
    in.lab_only_inverter_count = 1; /* 2 of 3 */
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_NONE);

    /* A lab declaration cannot substitute for a readback register: an
     * unconfirmable command stays blocked even in the lab. */
    in = good_inputs();
    in.write_qualified_inverter_count = 0;
    in.lab_only_inverter_count = in.enabled_inverter_count;
    in.readback_capable_inverter_count = 0;
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);

    /* An uncommissioned gate never authorises anything, and a zeroed status
     * authorises nothing. */
    commissioning_status_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    assert(zeroed.scope == COMMISSIONING_SCOPE_NONE);
    assert(commissioning_gate_evaluate(NULL).scope == COMMISSIONING_SCOPE_NONE);

    assert(strcmp(commissioning_scope_label(COMMISSIONING_SCOPE_NONE), "none") == 0);
    assert(strcmp(commissioning_scope_label(COMMISSIONING_SCOPE_LAB), "lab_simulator_only") == 0);
    assert(strcmp(commissioning_scope_label(COMMISSIONING_SCOPE_PRODUCTION), "production") == 0);
    /* Out-of-range must not report production. */
    assert(strcmp(commissioning_scope_label((commissioning_scope_t)99), "none") == 0);
}

int main(void)
{
    test_zeroed_input_is_never_commissioned();
    test_null_input_is_never_commissioned();
    test_fully_commissioned();
    test_each_unknown_group_closes_the_gate();
    test_meter_roles();
    test_partially_qualified_fleet_is_rejected();
    test_lab_scope();
    test_missing_readback_blocks_commissioning();
    test_capacity();
    test_ramp_policy();
    test_source_detection_accepts_either_evidence_path();
    test_generator_limits();
    test_control_tuning();
    test_first_unmet_is_lowest_index();
    test_labels_are_complete_and_bounded();
    printf("commissioning gate unit tests passed\n");
    return 0;
}
