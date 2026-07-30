#include "inverter_prerequisite.h"

static inverter_prerequisite_verdict_t verdict(bool satisfied,
                                              inverter_prerequisite_action_t action,
                                              bool unverifiable)
{
    return (inverter_prerequisite_verdict_t){
        .satisfied = satisfied,
        .action = action,
        .unverifiable = unverifiable,
    };
}

inverter_prerequisite_verdict_t inverter_prerequisite_evaluate(
    const inverter_prerequisite_evidence_t *evidence)
{
    /* Absent or unpopulated evidence is unknown, and unknown is never satisfied.
     * Marked unverifiable so a caller that ignores `satisfied` and looks only at
     * this field still fails closed. */
    if (!evidence || !evidence->populated) {
        return verdict(false, INVERTER_PREREQ_ACTION_NONE, true);
    }

    /* This device does not need a prerequisite, so there is nothing to hold and
     * nothing to verify. Not unverifiable - there is simply no prerequisite. */
    if (!evidence->required) {
        return verdict(true, INVERTER_PREREQ_ACTION_NONE, false);
    }

    /* Needs one, but the profile cannot describe a readable one. No sequence of
     * transactions can resolve this, so request no I/O and say so plainly. */
    if (inverter_prerequisite_write_blocked(evidence->required,
                                            evidence->write_described,
                                            evidence->readback_described)) {
        return verdict(false, INVERTER_PREREQ_ACTION_NONE, true);
    }

    /* A recheck period that is not strictly inside the expiry window would make
     * the sample expire before it was due to be refreshed, so the inverter would
     * leave the commandable fleet on every cycle. Treat that as unknown and ask
     * for a read rather than pretending to a verdict. */
    if (evidence->expiry_ms == 0U || evidence->recheck_ms >= evidence->expiry_ms) {
        return verdict(false, INVERTER_PREREQ_ACTION_READ, false);
    }

    /* Never read it: unknown. Read it. */
    if (!evidence->have_sample) {
        return verdict(false, INVERTER_PREREQ_ACTION_READ, false);
    }

    /* The sample is too old to be evidence. This is the case that catches a
     * switch turned off by a human or by another master: the limit was verified
     * once, the sample aged out, and until a fresh read says otherwise the
     * inverter is not commandable. */
    if (evidence->sample_age_ms > evidence->expiry_ms) {
        return verdict(false, INVERTER_PREREQ_ACTION_READ, false);
    }

    if (evidence->sample_holds) {
        /* Due for a refresh, but still satisfied in the meantime - that is what
         * the gap between recheck and expiry is for. */
        const inverter_prerequisite_action_t action =
            evidence->sample_age_ms > evidence->recheck_ms
                ? INVERTER_PREREQ_ACTION_READ
                : INVERTER_PREREQ_ACTION_NONE;
        /* A sample that predates our own most recent write describes a state we
         * have since overwritten. Fail closed and re-read. */
        if (!evidence->sample_after_write) {
            return verdict(false, INVERTER_PREREQ_ACTION_READ, false);
        }
        return verdict(true, action, false);
    }

    /* It does not hold. Two distinct situations, and conflating them is how a
     * controller ends up hammering a register it has already written:
     *
     *   - the reading is current with respect to our writes (or we have never
     *     written): the register genuinely needs writing;
     *   - we wrote after this reading was taken: the write may well have landed,
     *     and the only thing that can establish that is another READ. This is
     *     the mandatory re-read, and it is the only path to satisfied. */
    if (!evidence->write_issued || evidence->sample_after_write) {
        return verdict(false, INVERTER_PREREQ_ACTION_WRITE, false);
    }
    return verdict(false, INVERTER_PREREQ_ACTION_READ, false);
}

const char *inverter_prerequisite_action_name(inverter_prerequisite_action_t action)
{
    switch (action) {
    case INVERTER_PREREQ_ACTION_READ: return "read";
    case INVERTER_PREREQ_ACTION_WRITE: return "write";
    case INVERTER_PREREQ_ACTION_NONE:
    default: return "none";
    }
}
