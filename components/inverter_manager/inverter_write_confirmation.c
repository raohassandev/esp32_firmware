#include "inverter_write_confirmation.h"

#include <math.h>

static bool finite_percent(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 100.0f;
}

static inverter_write_verdict_t verdict(inverter_write_state_t state,
                                        bool requires_safe_zero,
                                        bool settled)
{
    return (inverter_write_verdict_t){
        .state = state,
        .requires_safe_zero = requires_safe_zero,
        .settled = settled,
    };
}

inverter_write_verdict_t inverter_write_confirmation_evaluate(
    const inverter_write_evidence_t *evidence)
{
    /* No evidence at all is not "fine", it is unknown - and unknown is
     * unverified with a demand to go safe. */
    if (!evidence) return verdict(INVERTER_WRITE_UNVERIFIED, true, true);

    /* Nothing has been commanded, so there is nothing to confirm and nothing to
     * roll back. */
    if (!evidence->write_issued) return verdict(INVERTER_WRITE_UNVERIFIED, false, true);

    /* The write did not reach the device. The caller's transport error path
     * owns the retry; from a confirmation standpoint the setpoint in the
     * inverter is unknown, so it must be driven safe. */
    if (!evidence->write_accepted) return verdict(INVERTER_WRITE_UNVERIFIED, true, true);

    /* A manufacturer with no manual-verified readback register can never report
     * confirmed. This firmware does not invent a register to make it look
     * confirmed, and it does not treat the absence of one as success. */
    if (!evidence->readback_supported) {
        return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
    }

    /* Nonsensical inputs are unknown, not confirmed. */
    if (!finite_percent(evidence->commanded_percent) ||
        !isfinite(evidence->tolerance_percent) || evidence->tolerance_percent < 0.0f ||
        evidence->deadline_ms < evidence->settle_ms) {
        return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
    }

    const bool usable_sample = evidence->readback_valid &&
                               evidence->readback_after_write &&
                               finite_percent(evidence->readback_percent);

    if (usable_sample) {
        float difference = fabsf(evidence->readback_percent - evidence->commanded_percent);
        if (difference <= evidence->tolerance_percent) {
            return verdict(INVERTER_WRITE_CONFIRMED, false, true);
        }
        /* A disagreeing sample taken inside the settle window is not yet
         * evidence of a mismatch: the device is allowed that long to apply the
         * new setpoint. Calling it a mismatch here would trip a healthy fleet
         * on nothing more than acquisition timing. */
        if (evidence->age_since_write_ms <= evidence->settle_ms) {
            return verdict(INVERTER_WRITE_PENDING, false, false);
        }
        return verdict(INVERTER_WRITE_MISMATCHED, true, true);
    }

    /* No usable post-write sample yet. Up to the deadline this is normal and
     * transient - the background poll has simply not come round yet. Past the
     * deadline it is a failure to verify, and the setpoint actually in the
     * inverter is unknown. */
    if (evidence->age_since_write_ms <= evidence->deadline_ms) {
        return verdict(INVERTER_WRITE_PENDING, false, false);
    }
    return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
}

const char *inverter_write_state_name(inverter_write_state_t state)
{
    switch (state) {
    case INVERTER_WRITE_PENDING: return "pending";
    case INVERTER_WRITE_CONFIRMED: return "confirmed";
    case INVERTER_WRITE_MISMATCHED: return "mismatched";
    case INVERTER_WRITE_UNVERIFIED:
    default: return "unverified";
    }
}

static uint8_t severity(inverter_write_state_t state)
{
    switch (state) {
    case INVERTER_WRITE_CONFIRMED: return 0U;
    case INVERTER_WRITE_PENDING: return 1U;
    case INVERTER_WRITE_MISMATCHED: return 3U;
    case INVERTER_WRITE_UNVERIFIED:
    default: return 2U;
    }
}

inverter_write_state_t inverter_write_state_worst(const inverter_write_state_t *states,
                                                  uint8_t count)
{
    /* An empty or absent fleet is unverified. Reporting "confirmed" for a fleet
     * that contains nothing would be the exact silent success this module
     * exists to prevent. */
    if (!states || count == 0U) return INVERTER_WRITE_UNVERIFIED;

    inverter_write_state_t worst = INVERTER_WRITE_CONFIRMED;
    for (uint8_t i = 0; i < count; ++i) {
        if (severity(states[i]) > severity(worst)) worst = states[i];
    }
    return worst;
}
