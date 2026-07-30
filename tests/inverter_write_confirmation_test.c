/* Host-compiled unit test for deferred write confirmation (P0-9).
 *
 * Executes the real evaluator. The property under test is that a write is
 * reported as confirmed only on the strength of a post-write readback that
 * matched, and that everything else reports pending, mismatched or unverified -
 * never a silent success. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "inverter_write_confirmation.h"

/* A healthy, freshly confirmed write. Each test breaks exactly one thing. */
static inverter_write_evidence_t good_evidence(void)
{
    inverter_write_evidence_t e;
    memset(&e, 0, sizeof(e));
    e.readback_supported = true;
    e.write_issued = true;
    e.write_accepted = true;
    e.readback_valid = true;
    e.readback_after_write = true;
    e.commanded_percent = 60.0f;
    e.readback_percent = 60.0f;
    e.tolerance_percent = 1.0f;
    e.age_since_write_ms = 900;
    e.settle_ms = 500;
    e.deadline_ms = 5000;
    return e;
}

static void test_confirmed(void)
{
    inverter_write_evidence_t e = good_evidence();
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(!v.requires_safe_zero);
    assert(v.settled);

    /* Exactly on the tolerance boundary still counts as confirmed. */
    e.readback_percent = 61.0f;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    e.readback_percent = 59.0f;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
}

static void test_mismatch_demands_safe_zero(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.readback_percent = 20.0f;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_MISMATCHED);
    assert(v.requires_safe_zero);
    assert(v.settled);
}

/* A disagreeing sample inside the settle window is timing, not a fault. */
static void test_disagreement_inside_settle_window_is_pending(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.readback_percent = 0.0f;
    e.age_since_write_ms = 200; /* < settle_ms */
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_PENDING);
    assert(!v.requires_safe_zero);
    assert(!v.settled);

    /* Once the settle window has passed, the same sample is a mismatch. */
    e.age_since_write_ms = 501;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_MISMATCHED);
    assert(v.requires_safe_zero);
}

static void test_no_sample_yet_is_pending_then_unverified(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.readback_valid = false;
    e.age_since_write_ms = 100;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_PENDING);
    assert(!v.requires_safe_zero);

    e.age_since_write_ms = 5000; /* exactly the deadline */
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_PENDING);

    e.age_since_write_ms = 5001; /* past the deadline */
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);
    assert(v.settled);
}

/* A readback taken before the write proves nothing about that write. */
static void test_pre_write_sample_does_not_confirm(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.readback_after_write = false;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_PENDING);

    e.age_since_write_ms = 60000;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);
}

/* The safety rule for manufacturers whose readback register this firmware does
 * not have a manual for: unverified, always, and never upgraded. */
static void test_unsupported_readback_is_always_unverified(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.readback_supported = false;
    for (uint32_t age = 0; age <= 100000; age += 25000) {
        e.age_since_write_ms = age;
        inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
        assert(v.state == INVERTER_WRITE_UNVERIFIED);
        assert(v.requires_safe_zero);
        assert(v.settled);
    }

    /* Not even a perfectly matching readback value can promote it: without a
     * qualified register there is nothing legitimate to have read. */
    e.readback_percent = e.commanded_percent;
    e.age_since_write_ms = 1000;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);
}

static void test_nothing_written_needs_no_rollback(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.write_issued = false;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(!v.requires_safe_zero);
    assert(v.settled);
}

static void test_rejected_write_demands_safe_zero(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.write_accepted = false;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);
}

static void test_null_and_nonsense_inputs_fail_closed(void)
{
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(NULL);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);

    const float bad_commands[] = {NAN, INFINITY, -1.0f, 101.0f};
    for (size_t i = 0; i < sizeof(bad_commands) / sizeof(bad_commands[0]); ++i) {
        inverter_write_evidence_t e = good_evidence();
        e.commanded_percent = bad_commands[i];
        v = inverter_write_confirmation_evaluate(&e);
        assert(v.state == INVERTER_WRITE_UNVERIFIED);
        assert(v.requires_safe_zero);
    }

    inverter_write_evidence_t e = good_evidence();
    e.tolerance_percent = -0.5f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    e = good_evidence();
    e.tolerance_percent = NAN;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    e = good_evidence();
    e.deadline_ms = 100; /* shorter than the settle window: incoherent */
    e.settle_ms = 500;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    /* A non-finite or out-of-range readback value is not a mismatch, it is an
     * unusable sample: keep waiting rather than invent a fault. */
    e = good_evidence();
    e.readback_percent = NAN;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_PENDING);
    e.readback_percent = 400.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_PENDING);
}

/* A device that defers applying a setpoint, reproduced from measurements against
 * the SolTrix lab simulator: it accepts a 40125 write and applies it ~1500 ms
 * later, reporting the PREVIOUS active limit in the meantime.
 *
 * With the old global 500 ms settle window this sequence produced MISMATCHED for
 * a perfectly accepted command, which latches a confirmation fault, removes the
 * inverter from commandable capacity and drives it to zero. A false fault on a
 * healthy 100 kW machine is as harmful as missing a real one. The profile now
 * carries the device's own settle window, and this pins the behaviour. */
static void test_deferred_apply_device_is_pending_not_mismatched(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.settle_ms = 2500;     /* the device's declared window */
    e.deadline_ms = 5000;
    e.commanded_percent = 50.0f;
    e.readback_percent = 100.0f; /* still the old active limit */

    /* Throughout the deferral the verdict must be PENDING, never MISMATCHED. */
    const uint32_t ages[] = {0, 200, 600, 1000, 1499, 2000, 2500};
    for (size_t i = 0; i < sizeof(ages) / sizeof(ages[0]); ++i) {
        e.age_since_write_ms = ages[i];
        inverter_write_verdict_t verdict = inverter_write_confirmation_evaluate(&e);
        assert(verdict.state == INVERTER_WRITE_PENDING);
        /* Pending must not demand a safe-zero: that would fight the command. */
        assert(!verdict.requires_safe_zero);
    }

    /* Once the device has applied it, the same readback confirms. */
    e.age_since_write_ms = 2000;
    e.readback_percent = 50.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_CONFIRMED);

    /* But the longer window must NOT hide a genuine disagreement: past settle,
     * a disagreeing readback is still a mismatch. */
    e.age_since_write_ms = 2501;
    e.readback_percent = 100.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_MISMATCHED);

    /* And the deadline still bounds an unconfirmed setpoint regardless of how
     * generous the settle window is. */
    e.age_since_write_ms = 5001;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_MISMATCHED ||
           inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);
}

static void test_fleet_rollup(void)
{
    /* An empty fleet is unverified, never confirmed. */
    assert(inverter_write_state_worst(NULL, 0) == INVERTER_WRITE_UNVERIFIED);
    const inverter_write_state_t none[1] = {INVERTER_WRITE_CONFIRMED};
    assert(inverter_write_state_worst(none, 0) == INVERTER_WRITE_UNVERIFIED);
    assert(inverter_write_state_worst(NULL, 4) == INVERTER_WRITE_UNVERIFIED);

    const inverter_write_state_t all_ok[3] = {
        INVERTER_WRITE_CONFIRMED, INVERTER_WRITE_CONFIRMED, INVERTER_WRITE_CONFIRMED};
    assert(inverter_write_state_worst(all_ok, 3) == INVERTER_WRITE_CONFIRMED);

    const inverter_write_state_t one_pending[3] = {
        INVERTER_WRITE_CONFIRMED, INVERTER_WRITE_PENDING, INVERTER_WRITE_CONFIRMED};
    assert(inverter_write_state_worst(one_pending, 3) == INVERTER_WRITE_PENDING);

    const inverter_write_state_t one_unverified[3] = {
        INVERTER_WRITE_CONFIRMED, INVERTER_WRITE_PENDING, INVERTER_WRITE_UNVERIFIED};
    assert(inverter_write_state_worst(one_unverified, 3) == INVERTER_WRITE_UNVERIFIED);

    /* A mismatch outranks everything, including an unverified peer. */
    const inverter_write_state_t one_mismatch[4] = {
        INVERTER_WRITE_CONFIRMED, INVERTER_WRITE_UNVERIFIED,
        INVERTER_WRITE_MISMATCHED, INVERTER_WRITE_PENDING};
    assert(inverter_write_state_worst(one_mismatch, 4) == INVERTER_WRITE_MISMATCHED);
}

static void test_state_names(void)
{
    assert(strcmp(inverter_write_state_name(INVERTER_WRITE_UNVERIFIED), "unverified") == 0);
    assert(strcmp(inverter_write_state_name(INVERTER_WRITE_PENDING), "pending") == 0);
    assert(strcmp(inverter_write_state_name(INVERTER_WRITE_CONFIRMED), "confirmed") == 0);
    assert(strcmp(inverter_write_state_name(INVERTER_WRITE_MISMATCHED), "mismatched") == 0);
    /* An out-of-range value must degrade to the least trusting label. */
    assert(strcmp(inverter_write_state_name((inverter_write_state_t)99), "unverified") == 0);
}

int main(void)
{
    test_confirmed();
    test_mismatch_demands_safe_zero();
    test_disagreement_inside_settle_window_is_pending();
    test_no_sample_yet_is_pending_then_unverified();
    test_pre_write_sample_does_not_confirm();
    test_unsupported_readback_is_always_unverified();
    test_nothing_written_needs_no_rollback();
    test_rejected_write_demands_safe_zero();
    test_null_and_nonsense_inputs_fail_closed();
    test_deferred_apply_device_is_pending_not_mismatched();
    test_fleet_rollup();
    test_state_names();
    printf("inverter write confirmation unit tests passed\n");
    return 0;
}
