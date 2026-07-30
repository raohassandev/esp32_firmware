#include "write_provenance.h"

/* See write_provenance.h for the rule and the reasoning. Nothing in this file
 * allocates, logs, locks or performs I/O. */

void write_provenance_reset(write_provenance_rollup_t *rollup)
{
    if (!rollup) return;
    rollup->written_count = 0U;
    rollup->limit_demonstrated_count = 0U;
    rollup->setpoint_echo_count = 0U;
    rollup->ambiguous_now_count = 0U;
    rollup->ambiguous_total = 0U;
    rollup->authority_lost_total = 0U;
    rollup->authority_lost_inverters = 0U;
    /* NONE claims the least, and it is the answer for a fleet nothing has been
     * written to. */
    rollup->weakest_proof = INVERTER_WRITE_PROOF_NONE;
}

uint8_t write_provenance_proof_rank(inverter_write_proof_t proof)
{
    switch (proof) {
    case INVERTER_WRITE_PROOF_MEASURED_POWER: return 3U;
    case INVERTER_WRITE_PROOF_SETPOINT_READBACK: return 2U;
    case INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM: return 1U;
    case INVERTER_WRITE_PROOF_NONE: return 0U;
    default: break;
    }
    /* An unrecognised value must not be able to out-rank a real one. */
    return 0U;
}

void write_provenance_accumulate(write_provenance_rollup_t *rollup,
                                 const inverter_data_t *data)
{
    if (!rollup || !data) return;

    /* The cumulative counters are summed for EVERY inverter reported, written or
     * not. An inverter that has never been commanded carries zero in both, so
     * this cannot inflate the totals, and it means a fleet total stays correct
     * across an inverter that was commanded earlier in the session and is not
     * being commanded now. */
    rollup->ambiguous_total += data->ambiguous_count;
    rollup->authority_lost_total += data->authority_lost_count;
    if (data->authority_lost_count > 0U && rollup->authority_lost_inverters < 0xFFU) {
        rollup->authority_lost_inverters++;
    }

    /* Provenance only means something about a write that happened. */
    if (!data->write_issued) return;

    const inverter_write_proof_t proof = (inverter_write_proof_t)data->write_proof;
    if (rollup->written_count == 0U) {
        rollup->weakest_proof = proof;
    } else if (write_provenance_proof_rank(proof) <
               write_provenance_proof_rank(rollup->weakest_proof)) {
        rollup->weakest_proof = proof;
    }
    if (rollup->written_count < 0xFFU) rollup->written_count++;

    /* limit_demonstrated is the firmware's own flag and is true only for a limit
     * proved by measurement. It is read rather than re-derived from the proof, so
     * this roll-up cannot disagree with the verdict that produced it. */
    if (data->limit_demonstrated) {
        if (rollup->limit_demonstrated_count < 0xFFU) rollup->limit_demonstrated_count++;
    } else if (proof == INVERTER_WRITE_PROOF_SETPOINT_READBACK) {
        /* Confirmed, but on a readback that may be an echo of a stored command.
         * Counted apart from the demonstrated ones, never added to them. */
        if (rollup->setpoint_echo_count < 0xFFU) rollup->setpoint_echo_count++;
    } else if (proof == INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM) {
        if (rollup->ambiguous_now_count < 0xFFU) rollup->ambiguous_now_count++;
    }
}

bool write_provenance_limit_demonstrated(const write_provenance_rollup_t *rollup)
{
    if (!rollup) return false;
    if (rollup->written_count == 0U) return false;
    return rollup->limit_demonstrated_count == rollup->written_count;
}

bool write_provenance_echo_only(const write_provenance_rollup_t *rollup)
{
    if (!rollup) return false;
    return rollup->setpoint_echo_count > 0U;
}
