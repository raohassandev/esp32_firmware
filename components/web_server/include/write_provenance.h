#pragma once

/*
 * Fleet roll-up of write-confirmation PROVENANCE.
 *
 * WHY THIS EXISTS
 * ---------------
 * inverter_write_confirmation.h defines two kinds of evidence that can produce a
 * CONFIRMED verdict, and they are not equally strong:
 *
 *   INVERTER_WRITE_PROOF_SETPOINT_READBACK  the setpoint read back matching. On
 *       some devices that is an applied value; on the Huawei SmartLogger plant
 *       interface it is an ECHO OF A STORED COMMAND and proves acceptance only.
 *   INVERTER_WRITE_PROOF_MEASURED_POWER     measured output was ABOVE the new
 *       limit before the command and at or below it after. The limit is
 *       genuinely demonstrated.
 *   INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM output is at or below the limit but
 *       was ALREADY at or below it. Equally consistent with the limit working
 *       and with the sun going in. The verdict is UNVERIFIED and
 *       limit_demonstrated is false.
 *
 * The per-inverter fields exist in inverter_data_t. What did not exist was any
 * FLEET answer, and the fleet answer is the one the interface leads with. A
 * fleet verdict of "confirmed" with no statement of what confirmed it lets an
 * operator believe a plant is limited when only an echo was observed - the same
 * false confirmation the confirmation core works hard to refuse, reintroduced at
 * the last layer.
 *
 * THE ROLL-UP RULE, AND WHY IT IS WEAKEST-FIRST
 * ---------------------------------------------
 * inverter_write_state_worst() takes a fleet's WORST verdict, so a fleet is only
 * ever as trustworthy as its least trustworthy member. Provenance is rolled up
 * the same way and for the same reason: the fleet's proof is the WEAKEST proof
 * held by any inverter that has been written to. Ranked by how much they claim:
 *
 *     none (0) < ambiguous_headroom (1) < setpoint_readback (2) < measured_power (3)
 *
 * ambiguous_headroom ranks above none only because it records that a measurement
 * was taken and refused; neither one demonstrates anything.
 *
 * limit_demonstrated for the fleet is true ONLY when at least one inverter has
 * been written to and EVERY written inverter demonstrated its limit by
 * measurement. One echo among eleven demonstrations is still a plant whose limit
 * is not fully demonstrated, and reporting otherwise would be the defect.
 *
 * An EMPTY fleet, or a fleet nothing has been written to, rolls up to
 * INVERTER_WRITE_PROOF_NONE with limit_demonstrated false. There is no
 * outstanding write to doubt, but there is also nothing demonstrated, and "none"
 * is the value that claims the least.
 *
 * PURE. No ESP-IDF, no cJSON, no locks, no I/O, no allocation, no logging, so
 * the rule is EXECUTED by tests/write_provenance_test.c rather than asserted
 * about. The cJSON emitter that publishes it lives in write_provenance_api.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include "inverter_types.h"
#include "inverter_write_confirmation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Inverters that have had a write issued at all. Nothing below counts an
     * inverter that has never been commanded: it has no proof to be weakest. */
    uint8_t written_count;
    /* Of those, the ones whose limit is DEMONSTRATED by measurement. */
    uint8_t limit_demonstrated_count;
    /* Of those, the ones reported CONFIRMED on a setpoint readback alone. On a
     * stored-command echo that is acceptance and nothing more, which is why it
     * is counted apart from the demonstrated ones rather than added to them. */
    uint8_t setpoint_echo_count;
    /* Of those, the ones whose CURRENT verdict rests on ambiguous headroom:
     * below the limit, but already below it before the command. */
    uint8_t ambiguous_now_count;

    /* Historical counters, summed over every inverter reported. These are
     * cumulative in the firmware and are summed rather than maxed so a fleet
     * total is a fleet total. */
    uint32_t ambiguous_total;
    uint32_t authority_lost_total;
    /* Inverters whose authority_lost_count is non-zero: another master took the
     * command target over after this controller commanded it. Reported as a
     * count of MACHINES as well as a count of EVENTS, because one inverter
     * losing authority forty times and forty inverters losing it once are
     * different findings. */
    uint8_t authority_lost_inverters;

    /* The weakest proof held by any written inverter. NONE when nothing has been
     * written. */
    inverter_write_proof_t weakest_proof;
} write_provenance_rollup_t;

/* Zeroes the accumulator into the fail-closed starting point: nothing written,
 * nothing demonstrated, weakest proof NONE. */
void write_provenance_reset(write_provenance_rollup_t *rollup);

/* Folds one inverter's already-acquired state in. NULL for either argument is a
 * no-op rather than a crash: a handler that cannot read an inverter must not be
 * able to raise the fleet's claim. */
void write_provenance_accumulate(write_provenance_rollup_t *rollup,
                                 const inverter_data_t *data);

/* How much a proof claims. Higher claims more. An unrecognised value ranks 0,
 * the rank that claims the least. */
uint8_t write_provenance_proof_rank(inverter_write_proof_t proof);

/* True only when at least one inverter has been written to and EVERY written
 * inverter demonstrated its limit by measurement. NULL is false. */
bool write_provenance_limit_demonstrated(const write_provenance_rollup_t *rollup);

/* True when at least one written inverter rests on a setpoint echo rather than a
 * demonstrated limit. This is the condition under which the word "confirmed"
 * must not be shown on its own. NULL is false. */
bool write_provenance_echo_only(const write_provenance_rollup_t *rollup);

#ifdef __cplusplus
}
#endif
