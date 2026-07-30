/* Host-compiled unit test for the prerequisite enable-register sequencing rule.
 *
 * This EXECUTES the real decision function. It exists because the failure it
 * guards against is invisible from every other signal the controller has: Solis
 * (tag 3070 = 0xAA), Sungrow (tag 5007 = 0xAA) and Chint/CPS (0x2602 = 1) each
 * ignore their active-power setpoint until a separate register holds a value --
 * but the setpoint register still ACCEPTS the write and still ECHOES it back. The
 * ordinary readback matches. The write is reported CONFIRMED. The 100 kW machine
 * keeps generating.
 *
 * So the properties tested here are not "does the state machine work". They are:
 *
 *   1. Nothing but a READ may ever set satisfied. An accepted write proves only
 *      that the transport took the frame.
 *   2. Unknown is not satisfied. Zeroed state, unpopulated evidence, an absent
 *      sample and an expired sample all mean not satisfied.
 *   3. A prerequisite that cannot be read back is unusable, permanently.
 *   4. A prerequisite that STOPS holding takes the inverter out again. Solis
 *      returns the machine to 100 % when the switch goes off, so a controller
 *      that verified once and then trusted forever would report a confirmed limit
 *      on an inverter running wide open.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "inverter_prerequisite.h"

/* A device that needs a prerequisite, describes a readable one, and has a fresh
 * confirming sample. Everything below is a perturbation of this. */
static inverter_prerequisite_evidence_t healthy(void)
{
    inverter_prerequisite_evidence_t evidence;
    memset(&evidence, 0, sizeof(evidence));
    evidence.populated = true;
    evidence.required = true;
    evidence.write_described = true;
    evidence.readback_described = true;
    evidence.have_sample = true;
    evidence.sample_holds = true;
    evidence.sample_after_write = true;
    evidence.write_issued = false;
    evidence.sample_age_ms = 0U;
    evidence.recheck_ms = 5000U;
    evidence.expiry_ms = 15000U;
    return evidence;
}

/* Property 2, the sharpest form of it: a struct nobody filled in must not read as
 * permission. NONE for the action too -- an unpopulated struct must not provoke
 * Modbus traffic either. */
static void test_zeroed_state_is_not_satisfied(void)
{
    inverter_prerequisite_evidence_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&zeroed);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);
    assert(verdict.unverifiable);

    /* And the verdict struct itself: zero must mean not satisfied and no I/O. */
    inverter_prerequisite_verdict_t zeroed_verdict;
    memset(&zeroed_verdict, 0, sizeof(zeroed_verdict));
    assert(!zeroed_verdict.satisfied);
    assert(zeroed_verdict.action == INVERTER_PREREQ_ACTION_NONE);
    assert((int)INVERTER_PREREQ_ACTION_NONE == 0);
}

static void test_null_evidence_fails_closed(void)
{
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(NULL);
    assert(!verdict.satisfied);
    assert(verdict.unverifiable);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);
}

/* A device with no prerequisite is satisfied by definition and generates no
 * traffic. This is the majority case and it must not cost a transaction. */
static void test_no_prerequisite_needed_is_satisfied(void)
{
    inverter_prerequisite_evidence_t evidence = healthy();
    evidence.required = false;
    /* Even with nothing else described and no sample ever taken. */
    evidence.write_described = false;
    evidence.readback_described = false;
    evidence.have_sample = false;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(verdict.satisfied);
    assert(!verdict.unverifiable);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);
}

/* Property 3. An unreadable prerequisite is unusable, and crucially it must ask
 * for NO I/O: retrying forever would look like a comms problem and hide the fact
 * that the profile is the problem. */
static void test_unreadable_prerequisite_is_unverifiable(void)
{
    inverter_prerequisite_evidence_t evidence = healthy();
    evidence.readback_described = false;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.unverifiable);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);

    /* Nor is a describable readback any use without a way to set the register. */
    evidence = healthy();
    evidence.write_described = false;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.unverifiable);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);

    /* Neither half described: same answer. */
    evidence.readback_described = false;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.unverifiable);

    /* The same rule, stated over the gate predicate the permission gate uses. */
    assert(inverter_prerequisite_write_blocked(true, true, false));
    assert(inverter_prerequisite_write_blocked(true, false, true));
    assert(inverter_prerequisite_write_blocked(true, false, false));
    assert(!inverter_prerequisite_write_blocked(true, true, true));
    /* A device that needs no prerequisite is not blocked by one. */
    assert(!inverter_prerequisite_write_blocked(false, false, false));
}

/* An inverter that has never had its enable register read is not commandable.
 * This is the state every inverter is in immediately after boot. */
static void test_no_sample_yet_blocks_and_asks_for_a_read(void)
{
    inverter_prerequisite_evidence_t evidence = healthy();
    evidence.have_sample = false;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(!verdict.unverifiable); /* recoverable: it just has not been read */
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);
}

/* Property 1, the core of the whole design. A write, however accepted, never
 * satisfies anything. The evidence struct carries no "write accepted" field for
 * exactly this reason, and the state right after a write must be a re-read. */
static void test_only_a_read_can_satisfy(void)
{
    /* The register did not hold, so it was written; our only sample predates that
     * write. The answer must be READ -- the mandatory re-read -- and not
     * satisfied. */
    inverter_prerequisite_evidence_t evidence = healthy();
    evidence.sample_holds = false;
    evidence.write_issued = true;
    evidence.sample_after_write = false;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    /* Even a sample that HELD but predates our write cannot satisfy: it describes
     * a state the write has since replaced. Fail closed, read again. */
    evidence.sample_holds = true;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    /* Only when the confirming sample is taken AFTER the write does it count. */
    evidence.sample_after_write = true;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);
}

/* Read, then write, then re-read: the sequence, walked. */
static void test_the_full_sequence(void)
{
    /* 1. Boot. Nothing read. */
    inverter_prerequisite_evidence_t evidence = healthy();
    evidence.have_sample = false;
    assert(inverter_prerequisite_evaluate(&evidence).action == INVERTER_PREREQ_ACTION_READ);

    /* 2. Read says it does not hold, and we have never written. Write it. */
    evidence.have_sample = true;
    evidence.sample_holds = false;
    evidence.write_issued = false;
    evidence.sample_after_write = true;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_WRITE);

    /* 3. Written. The sample now predates the write, so re-read. Still not
     *    satisfied, and the inverter is still out of the fleet. */
    evidence.write_issued = true;
    evidence.sample_after_write = false;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    /* 4. The re-read confirms. Only now is the inverter commandable. */
    evidence.sample_holds = true;
    evidence.sample_after_write = true;
    evidence.sample_age_ms = 0U;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);

    /* 5. The re-read says it STILL does not hold: the write did not take. Write
     *    again rather than sitting satisfied or spinning on reads. */
    evidence.sample_holds = false;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_WRITE);
}

/* Property 4. The register is a switch a human or another Modbus master can turn
 * off, and Solis explicitly returns the machine to 100 % when it goes off. */
static void test_a_prerequisite_that_stops_holding_removes_the_inverter(void)
{
    inverter_prerequisite_evidence_t evidence = healthy();
    assert(inverter_prerequisite_evaluate(&evidence).satisfied);

    /* Somebody switched it off, and the next read sees that. */
    evidence.sample_holds = false;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(!verdict.unverifiable);
    assert(verdict.action == INVERTER_PREREQ_ACTION_WRITE);
}

/* Re-verification must actually happen, and an expired sample must stop counting.
 * Verifying once and trusting forever is the failure mode; so is expiring a
 * sample before its own re-read is due, which would make the fleet flap. */
static void test_periodic_reverification(void)
{
    inverter_prerequisite_evidence_t evidence = healthy();

    /* Inside the recheck period: satisfied, no traffic. */
    evidence.sample_age_ms = 4999U;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_NONE);

    /* Past the recheck period but inside expiry: still satisfied, but a read is
     * now due. This gap is what stops the inverter leaving the commandable fleet
     * on every single re-verification. */
    evidence.sample_age_ms = 5001U;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    evidence.sample_age_ms = 15000U;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    /* Past expiry the sample is not evidence of anything. This is what catches a
     * switch turned off during an outage: the controller never saw it change, so
     * it must stop claiming to know. */
    evidence.sample_age_ms = 15001U;
    verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(!verdict.unverifiable);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    /* A very old sample is no better than a slightly old one. */
    evidence.sample_age_ms = 3600000U;
    assert(!inverter_prerequisite_evaluate(&evidence).satisfied);
}

/* A nonsensical schedule must not be able to produce a verdict. Zero expiry, or a
 * recheck period at or past expiry, would mean a sample expires before it is due
 * to be refreshed -- the inverter would join and leave the fleet continuously and
 * the control engine would see its capacity oscillate. */
static void test_a_nonsensical_schedule_is_never_satisfied(void)
{
    inverter_prerequisite_evidence_t evidence = healthy();
    evidence.expiry_ms = 0U;
    inverter_prerequisite_verdict_t verdict = inverter_prerequisite_evaluate(&evidence);
    assert(!verdict.satisfied);
    assert(verdict.action == INVERTER_PREREQ_ACTION_READ);

    evidence = healthy();
    evidence.recheck_ms = 15000U; /* equal to expiry */
    assert(!inverter_prerequisite_evaluate(&evidence).satisfied);

    evidence.recheck_ms = 20000U; /* past expiry */
    assert(!inverter_prerequisite_evaluate(&evidence).satisfied);
}

/* Function codes. A wrong code here is a frame on the wire that either cannot do
 * the job or does something else entirely: 0x05 writes a coil, and using a write
 * code to "verify" would mean writing the register again instead of reading it. */
static void test_function_codes(void)
{
    assert(inverter_prerequisite_write_function_supported(6));
    assert(inverter_prerequisite_write_function_supported(16));
    assert(!inverter_prerequisite_write_function_supported(3));
    assert(!inverter_prerequisite_write_function_supported(4));
    assert(!inverter_prerequisite_write_function_supported(5));
    assert(!inverter_prerequisite_write_function_supported(0));

    assert(inverter_prerequisite_read_function_supported(3));
    assert(inverter_prerequisite_read_function_supported(4));
    assert(!inverter_prerequisite_read_function_supported(6));
    assert(!inverter_prerequisite_read_function_supported(16));
    assert(!inverter_prerequisite_read_function_supported(0));
}

static void test_action_names(void)
{
    assert(strcmp(inverter_prerequisite_action_name(INVERTER_PREREQ_ACTION_NONE), "none") == 0);
    assert(strcmp(inverter_prerequisite_action_name(INVERTER_PREREQ_ACTION_READ), "read") == 0);
    assert(strcmp(inverter_prerequisite_action_name(INVERTER_PREREQ_ACTION_WRITE), "write") == 0);
    assert(strcmp(inverter_prerequisite_action_name((inverter_prerequisite_action_t)99),
                  "none") == 0);
}

int main(void)
{
    test_zeroed_state_is_not_satisfied();
    test_null_evidence_fails_closed();
    test_no_prerequisite_needed_is_satisfied();
    test_unreadable_prerequisite_is_unverifiable();
    test_no_sample_yet_blocks_and_asks_for_a_read();
    test_only_a_read_can_satisfy();
    test_the_full_sequence();
    test_a_prerequisite_that_stops_holding_removes_the_inverter();
    test_periodic_reverification();
    test_a_nonsensical_schedule_is_never_satisfied();
    test_function_codes();
    test_action_names();
    printf("inverter prerequisite enable sequencing unit tests passed\n");
    return 0;
}
