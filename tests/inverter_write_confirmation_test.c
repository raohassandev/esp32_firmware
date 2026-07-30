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

/* Minimum command interval. Asymmetric on purpose: withholding an increase is
 * harmless, withholding a reduction is the harm this must never cause, because
 * reducing PV is how the product protects a generator from under-loading and
 * reverse power.
 *
 * The concrete constraint being honoured: the Huawei SmartLogger Modbus
 * definitions state the adjustment value "should be issued at intervals of not
 * less than 1 seconds", while this controller's default control period is 250 ms. */
static void test_command_rate_limit(void)
{
    /* No configured minimum: never limited, whatever the timing. */
    assert(!inverter_command_rate_limited(0, true, 1000, 50.0f, 100.0f, 1001));

    /* The first command always goes, even inside the interval. */
    assert(!inverter_command_rate_limited(1000, false, 0, 0.0f, 100.0f, 10));

    /* An increase inside the interval is withheld; once elapsed it goes. */
    assert(inverter_command_rate_limited(1000, true, 5000, 50.0f, 60.0f, 5001));
    assert(inverter_command_rate_limited(1000, true, 5000, 50.0f, 60.0f, 5999));
    assert(!inverter_command_rate_limited(1000, true, 5000, 50.0f, 60.0f, 6000));
    assert(!inverter_command_rate_limited(1000, true, 5000, 50.0f, 60.0f, 6001));

    /* A repeat of the same value is treated as an increase: it carries no
     * protective urgency, so it may wait. */
    assert(inverter_command_rate_limited(1000, true, 5000, 50.0f, 50.0f, 5100));

    /* A REDUCTION is never withheld, however soon it arrives. This is the
     * property that protects the generator. */
    assert(!inverter_command_rate_limited(1000, true, 5000, 50.0f, 49.9f, 5001));
    assert(!inverter_command_rate_limited(1000, true, 5000, 100.0f, 0.0f, 5000));
    assert(!inverter_command_rate_limited(1000, true, 5000, 0.5f, 0.0f, 5000));

    /* Millisecond rollover must not make an old command look recent. Last
     * command just below 2^32, now just after wrapping: 20 ms have elapsed. */
    assert(inverter_command_rate_limited(1000, true, 0xFFFFFFF0u, 50.0f, 60.0f, 4u));
    /* And a genuinely old command across the wrap is not limited. */
    assert(!inverter_command_rate_limited(1000, true, 0xFFFFF000u, 50.0f, 60.0f, 4000u));

    /* A non-finite setpoint on either side must not be able to block a command,
     * because it must never block a reduction. */
    assert(!inverter_command_rate_limited(1000, true, 5000, NAN, 60.0f, 5001));
    assert(!inverter_command_rate_limited(1000, true, 5000, 50.0f, NAN, 5001));
}

/* ------------------------------------------------------------------------- */
/* Measured-power confirmation.                                              */
/*                                                                           */
/* The property under test is the one that decides whether this feature is    */
/* safe or dangerous: measured output BELOW a commanded limit is equally      */
/* consistent with the limit being honoured and with the sun going in, so it  */
/* must NEVER read as confirmed. Only a fall from ABOVE the new limit to      */
/* at-or-below it demonstrates a limit.                                      */
/* ------------------------------------------------------------------------- */

/* A plant on the Huawei SmartLogger plant interface: 100 kW of inverters,
 * commanded to 60 %, so the limit is 60 kW with a 2 %-of-capacity (2 kW) band.
 * The setpoint readback agrees, which for this interface is ACCEPTANCE only. */
static inverter_write_evidence_t measured_evidence(void)
{
    inverter_write_evidence_t e = good_evidence();
    e.commanded_percent = 60.0f;
    e.readback_percent = 60.0f;
    e.tolerance_percent = 1.0f;
    e.settle_ms = 1000;
    e.deadline_ms = 5000;
    e.age_since_write_ms = 1500;

    e.measured_mode = INVERTER_MEASURED_CONFIRM_REQUIRED;
    e.capacity_kw = 100.0f;
    e.measured_tolerance_percent_of_capacity = 2.0f;
    e.measured_valid = true;
    e.measured_after_write = true;
    e.measured_kw = 59.0f;      /* at the limit, having come down */
    e.baseline_valid = true;
    e.baseline_before_write = true;
    e.baseline_kw = 88.0f;      /* was well above the new limit */
    return e;
}

static void test_measured_power_demonstrates_a_limit(void)
{
    inverter_write_evidence_t e = measured_evidence();
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_MEASURED_POWER);
    assert(!v.requires_safe_zero);
    assert(v.settled);

    /* Exactly on the upper edge of the band still counts as at-or-below. */
    e.measured_kw = 62.0f; /* limit 60 + band 2 */
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(v.limit_demonstrated);

    /* And the baseline must clear the band too, not merely the limit. */
    e.measured_kw = 59.0f;
    e.baseline_kw = 62.0f; /* == limit + band, not ABOVE it */
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(!v.limit_demonstrated);
    e.baseline_kw = 62.01f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_CONFIRMED);
}

/* THE CRUX. Falling irradiance must not read as a confirmed limit.
 *
 * The plant was already below the commanded limit when the command went out, so
 * a subsequent measurement below the limit is exactly what a cloud produces. The
 * limit may well be in force -- but this evidence cannot show it, and the honest
 * verdict is UNVERIFIED. */
static void test_falling_irradiance_is_not_a_confirmed_limit(void)
{
    inverter_write_evidence_t e = measured_evidence();

    /* Cloud: 30 kW before the command, 22 kW after, limit 60 kW. Nothing about
     * this sequence demonstrates a 60 kW limit. */
    e.baseline_kw = 30.0f;
    e.measured_kw = 22.0f;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(!v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);
    /* And it must NOT demand a safe zero: driving PV to zero every time
     * irradiance falls below the commanded limit is worse than the ambiguity. */
    assert(!v.requires_safe_zero);
    assert(v.settled);

    /* A perfectly matching setpoint readback cannot rescue it. That echo is the
     * value the logger STORED; it is acceptance, not application. */
    e.readback_percent = e.commanded_percent;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);

    /* Nor can time. Waiting past the deadline does not create evidence. */
    for (uint32_t age = 1000; age <= 120000; age += 20000) {
        e.age_since_write_ms = age;
        v = inverter_write_confirmation_evaluate(&e);
        assert(v.state != INVERTER_WRITE_CONFIRMED);
        assert(!v.limit_demonstrated);
    }

    /* A baseline exactly at the limit is still ambiguity, not demonstration. */
    e = measured_evidence();
    e.baseline_kw = 60.0f;
    e.measured_kw = 60.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    /* Missing baseline: same answer. This is the state after a restart, and it
     * must not silently confirm. */
    e = measured_evidence();
    e.baseline_valid = false;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);

    /* A "baseline" sampled after the write has already been affected by it. */
    e = measured_evidence();
    e.baseline_before_write = false;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    /* A non-finite baseline is not a baseline. */
    e = measured_evidence();
    e.baseline_kw = NAN;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);
}

/* A command of 100 % can never be demonstrated by measurement, because the
 * baseline can never be above capacity. Permanently ambiguous, and correctly so:
 * "no limit" is not a limit to demonstrate. */
static void test_full_output_command_is_never_demonstrated(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.commanded_percent = 100.0f;
    e.readback_percent = 100.0f;
    e.baseline_kw = 99.0f;
    e.measured_kw = 97.0f;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(!v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);
    assert(!v.requires_safe_zero);
}

/* The direction that IS unambiguous, and the direction that protects the
 * generator: no change in irradiance can lift a plant ABOVE a limit in force. */
static void test_output_above_the_limit_is_a_mismatch(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.measured_kw = 80.0f; /* limit 60, band 2 */

    /* Inside the settle window the plant is still allowed to be ramping down. */
    e.age_since_write_ms = 900;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_PENDING);
    assert(!v.requires_safe_zero);

    /* Past it, the limit is demonstrably NOT being honoured. */
    e.age_since_write_ms = 1001;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_MISMATCHED);
    assert(v.requires_safe_zero);
    assert(v.settled);

    /* This is the escalation path out of the ambiguous verdict: the sun comes
     * back, output climbs past the limit, and the fault appears. Note the
     * baseline is irrelevant here -- above the limit needs no baseline. */
    e.baseline_valid = false;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_MISMATCHED);
}

/* No measurement yet is transient, then unknown. Never confirmed, and unlike the
 * ambiguous verdict this one DOES demand the safe fallback: the plant's output is
 * not known at all. */
static void test_missing_measurement_is_pending_then_unverified(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.measured_valid = false;
    e.age_since_write_ms = 2000;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_PENDING);
    assert(!v.requires_safe_zero);

    e.age_since_write_ms = 5001;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);
    assert(v.settled);

    /* A measurement older than the write proves nothing about the write. */
    e = measured_evidence();
    e.measured_after_write = false;
    e.age_since_write_ms = 5001;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);

    /* A non-finite measurement is an unusable sample, not a fault. */
    e = measured_evidence();
    e.measured_kw = NAN;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_PENDING);
}

/* Incompletely described measured evidence is REFUSED, never quietly reverted to
 * confirming on the setpoint echo. A transcription slip must not turn into the
 * exact false confirmation this mode exists to prevent. */
static void test_incomplete_measured_description_fails_closed(void)
{
    /* No capacity: the commanded limit in kW cannot be derived at all. */
    inverter_write_evidence_t e = measured_evidence();
    e.capacity_kw = 0.0f;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);

    const float bad_capacities[] = {-1.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(bad_capacities) / sizeof(bad_capacities[0]); ++i) {
        e = measured_evidence();
        e.capacity_kw = bad_capacities[i];
        v = inverter_write_confirmation_evaluate(&e);
        assert(v.state == INVERTER_WRITE_UNVERIFIED);
        assert(v.requires_safe_zero);
    }

    /* No stated tolerance at all. A zero band on a physical measurement is not a
     * tolerance, it is a bug, and it must not be treated as "exact". */
    e = measured_evidence();
    e.measured_tolerance_percent_of_capacity = 0.0f;
    e.measured_tolerance_kw = 0.0f;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);

    e = measured_evidence();
    e.measured_tolerance_percent_of_capacity = 0.0f;
    e.measured_tolerance_kw = -1.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    /* An absolute kW band alone is sufficient. */
    e = measured_evidence();
    e.measured_tolerance_percent_of_capacity = 0.0f;
    e.measured_tolerance_kw = 2.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_CONFIRMED);

    /* With both stated the WIDER band is used. A 10 kW absolute band against a
     * 2 kW relative one must let 69 kW pass as at-or-below a 60 kW limit. */
    e.measured_tolerance_percent_of_capacity = 2.0f;
    e.measured_tolerance_kw = 10.0f;
    e.measured_kw = 69.0f;
    e.baseline_kw = 95.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_CONFIRMED);
    /* ...and it must widen the baseline requirement in the same step: a baseline
     * of 69 kW no longer clears a 60 + 10 kW threshold. */
    e.baseline_kw = 69.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);
}

/* Measured confirmation may be used INSTEAD of a setpoint readback: for a target
 * whose command register cannot be read back at all, measurement is the stronger
 * witness, not a weaker substitute. It still may not confirm on ambiguity. */
static void test_measured_required_without_any_readback(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.readback_supported = false;
    e.readback_valid = false;
    e.readback_percent = 0.0f;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_MEASURED_POWER);

    e.baseline_kw = 10.0f; /* already below the limit */
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);

    /* Without a readback AND without a required measured mode there is no
     * confirmation source at all, which stays unverified forever. */
    e = measured_evidence();
    e.readback_supported = false;
    e.measured_mode = INVERTER_MEASURED_CONFIRM_NONE;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);
}

/* A setpoint readback that DISAGREES is a real fault even when confirmation
 * closes on measurement: whatever the meter says, the device did not take the
 * value that was sent. */
static void test_disagreeing_readback_still_faults_in_measured_mode(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.readback_percent = 100.0f; /* the logger stored something else */
    e.age_since_write_ms = 900;  /* inside settle: timing, not a fault */
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_PENDING);

    e.age_since_write_ms = 1001;
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_MISMATCHED);
    assert(v.requires_safe_zero);
}

/* CORROBORATING mode: measurement is preferred, but a matching readback may
 * still confirm when measurement cannot demonstrate anything. The distinction is
 * visible in limit_demonstrated, so a caller can never confuse the two. */
static void test_corroborating_mode_falls_back_to_the_readback(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.measured_mode = INVERTER_MEASURED_CONFIRM_CORROBORATING;

    /* Demonstrated by measurement: the stronger proof is the one reported. */
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_MEASURED_POWER);

    /* Ambiguous measurement, matching readback: confirmed, but explicitly NOT
     * demonstrated. */
    e.baseline_kw = 20.0f;
    e.measured_kw = 15.0f;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(!v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_SETPOINT_READBACK);

    /* Output above the limit still outranks a matching readback. */
    e.measured_kw = 80.0f;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_MISMATCHED);
    assert(v.requires_safe_zero);

    /* And in REQUIRED mode the same ambiguous case may not confirm. */
    e.measured_mode = INVERTER_MEASURED_CONFIRM_REQUIRED;
    e.measured_kw = 15.0f;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);
}

/* The scheduling-authority assertion: after our own command the target must name
 * this controller's channel. Anything else means another master owns the plant.
 *
 * On the SmartLogger the register is 40737 "Active power control mode" and the
 * expected value is 4 "Remote scheduling". It is checked only AFTER a write,
 * because the logger is documented to adopt that mode on RECEIPT of a scheduling
 * command -- gating a command on it beforehand would deadlock. */
static void test_scheduling_authority_contention(void)
{
    inverter_write_evidence_t e = measured_evidence();
    e.authority_checked = true;
    e.authority_valid = true;
    e.authority_after_write = true;
    e.authority_holds = true;

    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_CONFIRMED);
    assert(v.limit_demonstrated);

    /* Another authority owns the plant. Inside the settle window the equipment
     * is still allowed to be adopting our mode. */
    e.authority_holds = false;
    e.age_since_write_ms = 900;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_PENDING);

    /* Past it, this is contention and the safe fallback is demanded -- even
     * though the measurement on its own would have confirmed. */
    e.age_since_write_ms = 1001;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_MISMATCHED);
    assert(v.requires_safe_zero);
    assert(!v.limit_demonstrated);

    /* Not read yet: transient, then unknown. A perfect measurement must not
     * confirm while it is unknown who owns the plant. */
    e.authority_holds = true;
    e.authority_valid = false;
    e.age_since_write_ms = 2000;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_PENDING);
    e.age_since_write_ms = 5001;
    v = inverter_write_confirmation_evaluate(&e);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(v.requires_safe_zero);

    /* An authority reading taken before our write says nothing about it. */
    e.authority_valid = true;
    e.authority_after_write = false;
    e.age_since_write_ms = 5001;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_UNVERIFIED);

    /* A profile that does not describe the register is unaffected. */
    e = measured_evidence();
    e.authority_checked = false;
    e.authority_holds = false;
    assert(inverter_write_confirmation_evaluate(&e).state == INVERTER_WRITE_CONFIRMED);
}

/* Zeroed state must never read as confirmed, and must never read as a
 * demonstrated limit. UNVERIFIED is 0 and PROOF_NONE is 0. */
static void test_zeroed_state_is_never_confirmed(void)
{
    assert(INVERTER_WRITE_UNVERIFIED == 0);
    assert(INVERTER_MEASURED_CONFIRM_NONE == 0);
    assert(INVERTER_WRITE_PROOF_NONE == 0);

    inverter_write_evidence_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(&zeroed);
    assert(v.state == INVERTER_WRITE_UNVERIFIED);
    assert(!v.limit_demonstrated);
    assert(v.proof == INVERTER_WRITE_PROOF_NONE);
}

static void test_proof_names(void)
{
    assert(strcmp(inverter_write_proof_name(INVERTER_WRITE_PROOF_NONE), "none") == 0);
    assert(strcmp(inverter_write_proof_name(INVERTER_WRITE_PROOF_SETPOINT_READBACK),
                  "setpoint_readback") == 0);
    assert(strcmp(inverter_write_proof_name(INVERTER_WRITE_PROOF_MEASURED_POWER),
                  "measured_power") == 0);
    assert(strcmp(inverter_write_proof_name(INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM),
                  "ambiguous_headroom") == 0);
    /* An out-of-range value must claim the least. */
    assert(strcmp(inverter_write_proof_name((inverter_write_proof_t)77), "none") == 0);
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
    test_command_rate_limit();
    test_measured_power_demonstrates_a_limit();
    test_falling_irradiance_is_not_a_confirmed_limit();
    test_full_output_command_is_never_demonstrated();
    test_output_above_the_limit_is_a_mismatch();
    test_missing_measurement_is_pending_then_unverified();
    test_incomplete_measured_description_fails_closed();
    test_measured_required_without_any_readback();
    test_disagreeing_readback_still_faults_in_measured_mode();
    test_corroborating_mode_falls_back_to_the_readback();
    test_scheduling_authority_contention();
    test_zeroed_state_is_never_confirmed();
    test_proof_names();
    test_fleet_rollup();
    test_state_names();
    printf("inverter write confirmation unit tests passed\n");
    return 0;
}
