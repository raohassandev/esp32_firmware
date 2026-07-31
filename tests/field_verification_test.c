/*
 * The "verified" badge.
 *
 * A badge somebody can tick is a second door into the write gate. This one is a
 * predicate over evidence the firmware collected itself, and these are the cases
 * that decide whether it can be talked into saying yes.
 *
 * Note what a simulator does here: it can never be VERIFIED and can never be
 * FAILED. That is the guard, and it is the only outcome observable without real
 * equipment attached -- which is why the VERIFIED path is exercised here rather
 * than on a bench.
 */
#include <assert.h>
#include <stdio.h>

#include "field_verification.h"

/* Clean real equipment, read-only profile: the simplest thing that should pass. */
static field_verification_input_t good_read_only(void)
{
    field_verification_input_t input = {
        .lab_target = false,
        .identity_supported = true,
        .identity_verified = true,
        .read_successes = FIELD_VERIFICATION_MIN_READS,
        .consecutive_read_failures = 0,
        .telemetry_valid = true,
        .telemetry_stale = false,
        .profile_commands = false,
        .write_attempted = false,
        .write_confirmed = false,
    };
    return input;
}

static void test_clean_read_only_endpoint_verifies(void)
{
    field_verification_input_t input = good_read_only();
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_VERIFIED);
}

/* THE GUARD. A simulator answers exactly what it was written to answer, so no
 * amount of clean communication with one is evidence about equipment. */
static void test_simulator_is_never_verified(void)
{
    field_verification_input_t input = good_read_only();
    input.lab_target = true;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_SIMULATOR);

    /* And it cannot be FAILED either: a misbehaving simulator scenario is a test
     * passing, not a machine disagreeing with its manual. */
    input.identity_verified = false;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_SIMULATOR);
    input.profile_commands = true;
    input.write_attempted = true;
    input.write_confirmed = false;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_SIMULATOR);
}

/* A profile that commands is not verified on reads alone: reading a machine
 * correctly says nothing about whether it accepts what this profile writes, and
 * that is the half that moves a plant. */
static void test_commanding_profile_needs_a_confirmed_write(void)
{
    field_verification_input_t input = good_read_only();
    input.profile_commands = true;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_PENDING);

    input.write_attempted = true;
    input.write_confirmed = true;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_VERIFIED);
}

/* Positive contradictions beat elapsed time: neither improves by polling longer,
 * so neither may sit in PENDING accumulating reads until it looks like progress. */
static void test_identity_mismatch_fails_rather_than_pends(void)
{
    field_verification_input_t input = good_read_only();
    input.identity_verified = false;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_FAILED);

    /* Even with far more than the required reads. */
    input.read_successes = 100000;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_FAILED);
}

static void test_unconfirmed_write_fails_rather_than_pends(void)
{
    field_verification_input_t input = good_read_only();
    input.profile_commands = true;
    input.write_attempted = true;
    input.write_confirmed = false;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_FAILED);
}

/* A profile with no identity probe is not penalised for it -- it is verified on
 * the evidence it can produce. */
static void test_profile_without_identity_probe_can_verify(void)
{
    field_verification_input_t input = good_read_only();
    input.identity_supported = false;
    input.identity_verified = false;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_VERIFIED);
}

static void test_insufficient_or_broken_link_is_pending(void)
{
    field_verification_input_t input = good_read_only();
    input.read_successes = FIELD_VERIFICATION_MIN_READS - 1U;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_PENDING);

    input = good_read_only();
    input.consecutive_read_failures = 1;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_PENDING);

    input = good_read_only();
    input.telemetry_stale = true;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_PENDING);

    input = good_read_only();
    input.telemetry_valid = false;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_PENDING);
}

/* A single lucky reply must not earn it. */
static void test_one_read_is_not_enough(void)
{
    field_verification_input_t input = good_read_only();
    input.read_successes = 1;
    assert(field_verification_evaluate(&input) == FIELD_VERIFICATION_PENDING);
}

static void test_null_is_pending_not_verified(void)
{
    assert(field_verification_evaluate(NULL) == FIELD_VERIFICATION_PENDING);
}

static void test_names_are_stable(void)
{
    assert(field_verification_name(FIELD_VERIFICATION_VERIFIED)[0] == 'v');
    assert(field_verification_name(FIELD_VERIFICATION_PENDING)[0] == 'p');
    assert(field_verification_name(FIELD_VERIFICATION_FAILED)[0] == 'f');
    assert(field_verification_name(FIELD_VERIFICATION_SIMULATOR)[0] == 's');
}

int main(void)
{
    test_clean_read_only_endpoint_verifies();
    test_simulator_is_never_verified();
    test_commanding_profile_needs_a_confirmed_write();
    test_identity_mismatch_fails_rather_than_pends();
    test_unconfirmed_write_fails_rather_than_pends();
    test_profile_without_identity_probe_can_verify();
    test_insufficient_or_broken_link_is_pending();
    test_one_read_is_not_enough();
    test_null_is_pending_not_verified();
    test_names_are_stable();
    printf("field verification tests passed\n");
    return 0;
}
