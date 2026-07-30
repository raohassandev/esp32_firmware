/*
 * Executes the fleet roll-up of write-confirmation PROVENANCE.
 *
 * components/web_server/write_provenance.c decides what a FLEET may claim about
 * the evidence behind its confirmations. The rule matters because the two kinds
 * of evidence are not equally strong (see
 * components/inverter_manager/include/inverter_write_confirmation.h):
 *
 *   measured_power      output was ABOVE the new limit before the command and at
 *                       or below it after. The limit is demonstrated.
 *   setpoint_readback   the setpoint read back matching. On a plant-level logger
 *                       that is an echo of a STORED COMMAND and proves only that
 *                       the command was accepted.
 *   ambiguous_headroom  output at or below the limit that was ALREADY at or below
 *                       it. Proves nothing.
 *
 * The roll-up is weakest-first, mirroring inverter_write_state_worst(): a fleet
 * is only ever as well evidenced as its least well evidenced member. The
 * property with teeth is that ONE echo among eleven demonstrated limits makes the
 * fleet's limit_demonstrated FALSE. A roll-up that reported otherwise would let
 * an interface print a demonstrated limit for a plant where a stored command was
 * echoed back, which is the false confirmation the whole confirmation core exists
 * to refuse.
 *
 * The module is pure, so this is an EXECUTED test rather than a source assertion.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "inverter_types.h"
#include "inverter_write_confirmation.h"
#include "write_provenance.h"

static inverter_data_t written(inverter_write_proof_t proof, bool demonstrated)
{
    inverter_data_t data = {0};
    data.write_issued = true;
    data.write_proof = (uint8_t)proof;
    data.limit_demonstrated = demonstrated;
    return data;
}

/* Ranking. Higher claims more, and an unrecognised value must claim the least or
 * it could out-rank a real one and raise the fleet's claim. */
static void test_rank_order(void)
{
    assert(write_provenance_proof_rank(INVERTER_WRITE_PROOF_MEASURED_POWER) >
           write_provenance_proof_rank(INVERTER_WRITE_PROOF_SETPOINT_READBACK));
    assert(write_provenance_proof_rank(INVERTER_WRITE_PROOF_SETPOINT_READBACK) >
           write_provenance_proof_rank(INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM));
    assert(write_provenance_proof_rank(INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM) >
           write_provenance_proof_rank(INVERTER_WRITE_PROOF_NONE));
    assert(write_provenance_proof_rank(INVERTER_WRITE_PROOF_NONE) == 0U);
    assert(write_provenance_proof_rank((inverter_write_proof_t)91) == 0U);
}

/* An empty fleet claims nothing. Not "confirmed", not "demonstrated". */
static void test_empty_fleet(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    assert(roll.written_count == 0U);
    assert(roll.limit_demonstrated_count == 0U);
    assert(roll.setpoint_echo_count == 0U);
    assert(roll.ambiguous_now_count == 0U);
    assert(roll.ambiguous_total == 0U);
    assert(roll.authority_lost_total == 0U);
    assert(roll.authority_lost_inverters == 0U);
    assert(roll.weakest_proof == INVERTER_WRITE_PROOF_NONE);
    assert(!write_provenance_limit_demonstrated(&roll));
    assert(!write_provenance_echo_only(&roll));
    assert(strcmp(inverter_write_proof_name(roll.weakest_proof), "none") == 0);
}

/* A single demonstrated limit is the only shape in which a fleet may report
 * limit_demonstrated. */
static void test_single_demonstrated(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    inverter_data_t data = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
    write_provenance_accumulate(&roll, &data);
    assert(roll.written_count == 1U);
    assert(roll.limit_demonstrated_count == 1U);
    assert(roll.setpoint_echo_count == 0U);
    assert(roll.weakest_proof == INVERTER_WRITE_PROOF_MEASURED_POWER);
    assert(write_provenance_limit_demonstrated(&roll));
    assert(!write_provenance_echo_only(&roll));
}

/* THE PROPERTY WITH TEETH. Eleven demonstrated limits and one echo is a fleet
 * whose limit is NOT demonstrated. */
static void test_one_echo_spoils_the_fleet(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    for (int i = 0; i < 11; ++i) {
        inverter_data_t good = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
        write_provenance_accumulate(&roll, &good);
    }
    assert(write_provenance_limit_demonstrated(&roll));

    inverter_data_t echo = written(INVERTER_WRITE_PROOF_SETPOINT_READBACK, false);
    write_provenance_accumulate(&roll, &echo);
    assert(roll.written_count == 12U);
    assert(roll.limit_demonstrated_count == 11U);
    assert(roll.setpoint_echo_count == 1U);
    /* The verdict a caller must not be able to get wrong. */
    assert(!write_provenance_limit_demonstrated(&roll));
    assert(write_provenance_echo_only(&roll));
    /* And the weakest evidence is named, not averaged away. */
    assert(roll.weakest_proof == INVERTER_WRITE_PROOF_SETPOINT_READBACK);
    assert(strcmp(inverter_write_proof_name(roll.weakest_proof), "setpoint_readback") == 0);
}

/* Ambiguous headroom is weaker than an echo and drags the fleet down to itself,
 * whatever order the inverters arrive in. */
static void test_ambiguous_is_weakest_in_any_order(void)
{
    const inverter_write_proof_t order_a[] = {
        INVERTER_WRITE_PROOF_MEASURED_POWER,
        INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM,
        INVERTER_WRITE_PROOF_SETPOINT_READBACK
    };
    const inverter_write_proof_t order_b[] = {
        INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM,
        INVERTER_WRITE_PROOF_SETPOINT_READBACK,
        INVERTER_WRITE_PROOF_MEASURED_POWER
    };
    const inverter_write_proof_t *orders[] = {order_a, order_b};
    for (int o = 0; o < 2; ++o) {
        write_provenance_rollup_t roll;
        write_provenance_reset(&roll);
        for (int i = 0; i < 3; ++i) {
            inverter_data_t data = written(orders[o][i],
                                           orders[o][i] == INVERTER_WRITE_PROOF_MEASURED_POWER);
            write_provenance_accumulate(&roll, &data);
        }
        assert(roll.written_count == 3U);
        assert(roll.weakest_proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);
        assert(roll.ambiguous_now_count == 1U);
        assert(roll.setpoint_echo_count == 1U);
        assert(roll.limit_demonstrated_count == 1U);
        assert(!write_provenance_limit_demonstrated(&roll));
        assert(strcmp(inverter_write_proof_name(roll.weakest_proof),
                      "ambiguous_headroom") == 0);
    }
}

/* An inverter that has never been written to has no proof to be weakest, so it
 * must neither drag the fleet down nor lift it up. */
static void test_unwritten_inverters_are_not_evidence(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    inverter_data_t good = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
    write_provenance_accumulate(&roll, &good);

    /* Zeroed except for a proof value it has no business claiming. */
    inverter_data_t idle = {0};
    idle.write_issued = false;
    idle.write_proof = (uint8_t)INVERTER_WRITE_PROOF_NONE;
    idle.limit_demonstrated = true;   /* would be a firmware bug; must not count */
    write_provenance_accumulate(&roll, &idle);

    assert(roll.written_count == 1U);
    assert(roll.limit_demonstrated_count == 1U);
    assert(roll.weakest_proof == INVERTER_WRITE_PROOF_MEASURED_POWER);
    assert(write_provenance_limit_demonstrated(&roll));
}

/* A fleet made only of never-written inverters demonstrates nothing. */
static void test_only_unwritten(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    for (int i = 0; i < 4; ++i) {
        inverter_data_t idle = {0};
        idle.limit_demonstrated = true;
        write_provenance_accumulate(&roll, &idle);
    }
    assert(roll.written_count == 0U);
    assert(roll.limit_demonstrated_count == 0U);
    assert(roll.weakest_proof == INVERTER_WRITE_PROOF_NONE);
    assert(!write_provenance_limit_demonstrated(&roll));
}

/* The cumulative counters are summed over every inverter reported, and the
 * authority contention is counted as machines as well as events. */
static void test_cumulative_counters(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);

    inverter_data_t a = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
    a.ambiguous_count = 7U;
    a.authority_lost_count = 40U;
    write_provenance_accumulate(&roll, &a);

    inverter_data_t b = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
    b.ambiguous_count = 3U;
    b.authority_lost_count = 0U;
    write_provenance_accumulate(&roll, &b);

    /* Never written, but it carries history from earlier in the session. */
    inverter_data_t c = {0};
    c.ambiguous_count = 5U;
    c.authority_lost_count = 1U;
    write_provenance_accumulate(&roll, &c);

    assert(roll.ambiguous_total == 15U);
    assert(roll.authority_lost_total == 41U);
    /* Two machines, forty-one events: different findings, reported separately. */
    assert(roll.authority_lost_inverters == 2U);
    assert(roll.written_count == 2U);
}

/* An unrecognised proof value must not out-rank a real one. */
static void test_unknown_proof_cannot_outrank(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    inverter_data_t odd = written((inverter_write_proof_t)91, false);
    write_provenance_accumulate(&roll, &odd);
    assert(roll.written_count == 1U);
    assert(write_provenance_proof_rank(roll.weakest_proof) == 0U);
    /* It degrades to the value that claims the least when named. */
    assert(strcmp(inverter_write_proof_name(roll.weakest_proof), "none") == 0);

    inverter_data_t good = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
    write_provenance_accumulate(&roll, &good);
    assert(write_provenance_proof_rank(roll.weakest_proof) == 0U);
    assert(!write_provenance_limit_demonstrated(&roll));
}

/* NULL must be a no-op and must never produce a claim. */
static void test_null_is_fail_closed(void)
{
    write_provenance_reset(NULL);
    write_provenance_accumulate(NULL, NULL);
    assert(!write_provenance_limit_demonstrated(NULL));
    assert(!write_provenance_echo_only(NULL));

    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    inverter_data_t good = written(INVERTER_WRITE_PROOF_MEASURED_POWER, true);
    write_provenance_accumulate(&roll, &good);
    /* A NULL inverter must not be able to change an existing answer. */
    write_provenance_accumulate(&roll, NULL);
    assert(roll.written_count == 1U);
    assert(write_provenance_limit_demonstrated(&roll));
    write_provenance_accumulate(NULL, &good);
    assert(roll.written_count == 1U);
}

/* The counts are uint8_t. They must saturate rather than wrap: a wrapped
 * written_count could make an all-echo fleet compare equal to zero demonstrated
 * limits and report a demonstrated limit. */
static void test_counts_saturate(void)
{
    write_provenance_rollup_t roll;
    write_provenance_reset(&roll);
    for (int i = 0; i < 400; ++i) {
        inverter_data_t echo = written(INVERTER_WRITE_PROOF_SETPOINT_READBACK, false);
        write_provenance_accumulate(&roll, &echo);
    }
    assert(roll.written_count == 255U);
    assert(roll.setpoint_echo_count == 255U);
    assert(roll.limit_demonstrated_count == 0U);
    assert(!write_provenance_limit_demonstrated(&roll));
    assert(write_provenance_echo_only(&roll));
}

int main(void)
{
    test_rank_order();
    test_empty_fleet();
    test_single_demonstrated();
    test_one_echo_spoils_the_fleet();
    test_ambiguous_is_weakest_in_any_order();
    test_unwritten_inverters_are_not_evidence();
    test_only_unwritten();
    test_cumulative_counters();
    test_unknown_proof_cannot_outrank();
    test_null_is_fail_closed();
    test_counts_saturate();
    printf("write provenance roll-up unit tests passed\n");
    return 0;
}
