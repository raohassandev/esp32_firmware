#include "inverter_write_confirmation.h"

#include <math.h>

static bool finite_percent(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 100.0f;
}

static inverter_write_verdict_t verdict_with_proof(inverter_write_state_t state,
                                                   bool requires_safe_zero,
                                                   bool settled,
                                                   bool limit_demonstrated,
                                                   inverter_write_proof_t proof)
{
    return (inverter_write_verdict_t){
        .state = state,
        .requires_safe_zero = requires_safe_zero,
        .settled = settled,
        .limit_demonstrated = limit_demonstrated,
        .proof = proof,
    };
}

static inverter_write_verdict_t verdict(inverter_write_state_t state,
                                        bool requires_safe_zero,
                                        bool settled)
{
    /* The narrow constructor, used by every branch whose evidence is a setpoint
     * readback or the absence of one. A CONFIRMED verdict built here therefore
     * rests on that readback and on nothing else, which is why the proof is
     * filled in from the state rather than passed: there is no other thing it
     * could be resting on.
     *
     * It never claims a demonstrated limit. A readback reports what a device says
     * its setpoint is; it does not report what the plant did. Only measured-power
     * evidence can demonstrate a limit, and it says so through
     * verdict_with_proof(). */
    return verdict_with_proof(state, requires_safe_zero, settled, false,
                              state == INVERTER_WRITE_CONFIRMED
                                  ? INVERTER_WRITE_PROOF_SETPOINT_READBACK
                                  : INVERTER_WRITE_PROOF_NONE);
}

/* The tolerance band on the measured comparison, in kW. Zero or negative means
 * the profile did not state a usable one, and the caller refuses the evidence
 * rather than comparing against a zero-width band.
 *
 * The wider of the two stated forms wins; see the header for why that is the
 * conservative direction. */
static float measured_band_kw(const inverter_write_evidence_t *evidence)
{
    float absolute = evidence->measured_tolerance_kw;
    if (!isfinite(absolute) || absolute < 0.0f) return -1.0f;

    float relative = 0.0f;
    if (isfinite(evidence->measured_tolerance_percent_of_capacity) &&
        evidence->measured_tolerance_percent_of_capacity > 0.0f) {
        relative = evidence->capacity_kw *
                   evidence->measured_tolerance_percent_of_capacity / 100.0f;
        if (!isfinite(relative)) return -1.0f;
    }
    return absolute > relative ? absolute : relative;
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

    const bool measured_required =
        evidence->measured_mode == INVERTER_MEASURED_CONFIRM_REQUIRED;
    const bool measured_used =
        measured_required || evidence->measured_mode == INVERTER_MEASURED_CONFIRM_CORROBORATING;

    /* A command target with no manual-verified confirmation source can never
     * report confirmed. This firmware does not invent a register to make it look
     * confirmed, and it does not treat the absence of one as success.
     *
     * A setpoint readback is one such source; a required measured-power
     * confirmation is another, and is accepted INSTEAD of a readback because it
     * is the stronger witness of the two, not a weaker substitute for it. */
    if (!evidence->readback_supported && !measured_required) {
        return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
    }

    /* Nonsensical inputs are unknown, not confirmed. */
    if (!finite_percent(evidence->commanded_percent) ||
        !isfinite(evidence->tolerance_percent) || evidence->tolerance_percent < 0.0f ||
        evidence->deadline_ms < evidence->settle_ms) {
        return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
    }

    /* Measured evidence that is incompletely described is refused, not ignored.
     * Ignoring it would silently fall back to confirming on the setpoint echo -
     * exactly the false confirmation the measured path exists to prevent. */
    float band_kw = 0.0f;
    float limit_kw = 0.0f;
    if (measured_used) {
        if (!isfinite(evidence->capacity_kw) || evidence->capacity_kw <= 0.0f) {
            return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
        }
        band_kw = measured_band_kw(evidence);
        if (!(band_kw > 0.0f)) return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
        limit_kw = evidence->capacity_kw * evidence->commanded_percent / 100.0f;
    }

    /*
     * Scheduling-authority assertion, checked before anything else can confirm.
     * If a different master owns plant scheduling then whatever the setpoint or
     * the meter says about our command is beside the point: we are not the
     * authority and our limit can be replaced at any moment.
     */
    if (evidence->authority_checked) {
        if (!evidence->authority_valid || !evidence->authority_after_write) {
            /* Not read yet. Transient up to the deadline, unknown after it. */
            if (evidence->age_since_write_ms <= evidence->deadline_ms) {
                return verdict(INVERTER_WRITE_PENDING, false, false);
            }
            return verdict(INVERTER_WRITE_UNVERIFIED, true, true);
        }
        if (!evidence->authority_holds) {
            /* The equipment is documented to adopt remote scheduling on RECEIPT
             * of a command, so inside the settle window a foreign mode is still
             * timing rather than contention. */
            if (evidence->age_since_write_ms <= evidence->settle_ms) {
                return verdict(INVERTER_WRITE_PENDING, false, false);
            }
            return verdict(INVERTER_WRITE_MISMATCHED, true, true);
        }
    }

    const bool usable_sample = evidence->readback_supported &&
                               evidence->readback_valid &&
                               evidence->readback_after_write &&
                               finite_percent(evidence->readback_percent);
    const bool setpoint_agrees =
        usable_sample &&
        fabsf(evidence->readback_percent - evidence->commanded_percent) <=
            evidence->tolerance_percent;

    /* A setpoint readback that DISAGREES is a real fault in every mode, measured
     * or not: whatever else is true, the device did not take the value sent. */
    if (usable_sample && !setpoint_agrees) {
        /* A disagreeing sample taken inside the settle window is not yet
         * evidence of a mismatch: the device is allowed that long to apply the
         * new setpoint. Calling it a mismatch here would trip a healthy fleet
         * on nothing more than acquisition timing. */
        if (evidence->age_since_write_ms <= evidence->settle_ms) {
            return verdict(INVERTER_WRITE_PENDING, false, false);
        }
        return verdict(INVERTER_WRITE_MISMATCHED, true, true);
    }

    if (!measured_used) {
        if (usable_sample) {
            return verdict(INVERTER_WRITE_CONFIRMED, false, true);
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

    /*
     * Measured-power confirmation. See the header for the full argument; the
     * short version is that output below a limit is equally consistent with the
     * limit being honoured and with the sun going in, so only a fall from ABOVE
     * the limit to at-or-below it demonstrates anything.
     */
    const bool measured_sample = evidence->measured_valid &&
                                 evidence->measured_after_write &&
                                 isfinite(evidence->measured_kw);

    if (measured_sample) {
        if (evidence->measured_kw > limit_kw + band_kw) {
            /* Output ABOVE the commanded limit. This direction is unambiguous:
             * no change in irradiance can lift a plant above a limit that is in
             * force. Inside the settle window the plant is still allowed to be
             * ramping down. */
            if (evidence->age_since_write_ms <= evidence->settle_ms) {
                return verdict(INVERTER_WRITE_PENDING, false, false);
            }
            return verdict(INVERTER_WRITE_MISMATCHED, true, true);
        }

        const bool baseline_sample = evidence->baseline_valid &&
                                     evidence->baseline_before_write &&
                                     isfinite(evidence->baseline_kw);

        if (baseline_sample && evidence->baseline_kw > limit_kw + band_kw) {
            /* Above the new limit before the command, at or below it after. This
             * is the only evidence in this module that DEMONSTRATES a limit. */
            return verdict_with_proof(INVERTER_WRITE_CONFIRMED, false, true, true,
                                      INVERTER_WRITE_PROOF_MEASURED_POWER);
        }

        /* Consistent with the limit, and equally consistent with falling
         * irradiance. A CORROBORATING profile may fall back to its setpoint
         * readback here; a REQUIRED one may not, and says so. */
        if (!measured_required && setpoint_agrees) {
            return verdict(INVERTER_WRITE_CONFIRMED, false, true);
        }
        /* No safe-zero: driving PV to zero whenever irradiance falls below the
         * commanded limit would be a worse outcome than the ambiguity. The next
         * sample escalates if output ever rises above the limit. */
        return verdict_with_proof(INVERTER_WRITE_UNVERIFIED, false, true, false,
                                  INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM);
    }

    /* No usable measurement yet. */
    if (evidence->age_since_write_ms <= evidence->deadline_ms) {
        return verdict(INVERTER_WRITE_PENDING, false, false);
    }
    if (!measured_required && setpoint_agrees) {
        return verdict(INVERTER_WRITE_CONFIRMED, false, true);
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

const char *inverter_write_proof_name(inverter_write_proof_t proof)
{
    switch (proof) {
    case INVERTER_WRITE_PROOF_SETPOINT_READBACK: return "setpoint_readback";
    case INVERTER_WRITE_PROOF_MEASURED_POWER: return "measured_power";
    case INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM: return "ambiguous_headroom";
    case INVERTER_WRITE_PROOF_NONE:
    default: return "none";
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

bool inverter_command_rate_limited(uint32_t minimum_interval_ms,
                                   bool has_previous_command,
                                   uint32_t last_command_ms,
                                   float previous_percent,
                                   float requested_percent,
                                   uint32_t now_ms)
{
    if (minimum_interval_ms == 0U) return false;
    if (!has_previous_command) return false;

    /* A reduction is never withheld. Also treated as "allow" when either value is
     * not finite: an unknown setpoint must not be able to block a reduction. */
    if (!isfinite(previous_percent) || !isfinite(requested_percent)) return false;
    if (requested_percent < previous_percent) return false;

    /* Unsigned subtraction wraps correctly, so the 32-bit millisecond rollover
     * cannot make an old command appear recent. */
    return (uint32_t)(now_ms - last_command_ms) < minimum_interval_ms;
}
