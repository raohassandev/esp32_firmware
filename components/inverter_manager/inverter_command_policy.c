#include "inverter_command_policy.h"
#include <math.h>

static bool finite_percent(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 100.0f;
}

inverter_command_decision_t inverter_command_decide(const inverter_command_evidence_t *evidence)
{
    inverter_command_decision_t decision = {
        .action = INVERTER_COMMAND_FAILED_SAFE,
        .confirmed = false,
        .mismatch = false,
        .next_percent = 0.0f,
    };

    if (!evidence || !finite_percent(evidence->requested_percent) ||
        !finite_percent(evidence->safe_fallback_percent) ||
        !isfinite(evidence->tolerance_percent) || evidence->tolerance_percent < 0.0f ||
        evidence->maximum_attempts == 0U ||
        evidence->attempts_completed > evidence->maximum_attempts) {
        return decision;
    }

    if (!evidence->write_succeeded) {
        if (evidence->attempts_completed < evidence->maximum_attempts) {
            decision.action = INVERTER_COMMAND_RETRY;
            decision.next_percent = evidence->requested_percent;
        } else {
            decision.action = INVERTER_COMMAND_ROLLBACK;
            decision.next_percent = evidence->safe_fallback_percent;
        }
        return decision;
    }

    if (!evidence->readback_supported) {
        /* A production command cannot be reported as confirmed without a
         * manufacturer-qualified readback path. */
        decision.action = INVERTER_COMMAND_ROLLBACK;
        decision.next_percent = evidence->safe_fallback_percent;
        return decision;
    }

    if (!evidence->readback_succeeded || !finite_percent(evidence->readback_percent)) {
        if (evidence->attempts_completed < evidence->maximum_attempts) {
            decision.action = INVERTER_COMMAND_RETRY;
            decision.next_percent = evidence->requested_percent;
        } else {
            decision.action = INVERTER_COMMAND_ROLLBACK;
            decision.next_percent = evidence->safe_fallback_percent;
        }
        return decision;
    }

    float difference = fabsf(evidence->readback_percent - evidence->requested_percent);
    if (difference <= evidence->tolerance_percent) {
        decision.action = INVERTER_COMMAND_CONFIRMED;
        decision.confirmed = true;
        decision.next_percent = evidence->requested_percent;
        return decision;
    }

    decision.mismatch = true;
    if (evidence->attempts_completed < evidence->maximum_attempts) {
        decision.action = INVERTER_COMMAND_RETRY;
        decision.next_percent = evidence->requested_percent;
        return decision;
    }

    if (evidence->rollback_write_succeeded && evidence->rollback_readback_succeeded) {
        decision.action = INVERTER_COMMAND_FAILED_SAFE;
        decision.next_percent = evidence->safe_fallback_percent;
    } else {
        decision.action = INVERTER_COMMAND_ROLLBACK;
        decision.next_percent = evidence->safe_fallback_percent;
    }
    return decision;
}
