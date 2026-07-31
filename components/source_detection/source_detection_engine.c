#include "source_detection_engine.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static source_detection_observation_t unknown(source_reason_t reason)
{
    source_detection_observation_t result = {
        .candidate_state = SOURCE_STATE_UNKNOWN,
        .reason = reason,
        .evidence_fresh = false,
        .conflict = reason == SOURCE_REASON_CONFLICT,
    };
    return result;
}

static bool common_policy_valid(const source_detection_policy_t *policy)
{
    return policy && policy->debounce_ms > 0U && policy->stale_timeout_ms > 0U;
}

source_detection_observation_t source_detection_observe(
    const source_detection_policy_t *policy,
    const source_detection_evidence_t *evidence)
{
    if (!policy || !evidence) return unknown(SOURCE_REASON_INVALID_CONFIG);
    if (policy->mode == SOURCE_DETECTION_MODE_DISABLED) {
        return unknown(SOURCE_REASON_NOT_CONFIGURED);
    }
    if (!common_policy_valid(policy)) return unknown(SOURCE_REASON_INVALID_CONFIG);

    if (policy->mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        if (policy->single_grid_value == policy->single_generator_value) {
            return unknown(SOURCE_REASON_INVALID_CONFIG);
        }
        if (!evidence->single_has_sample) {
            return unknown(SOURCE_REASON_EVIDENCE_UNAVAILABLE);
        }
        if (evidence->single_age_ms > policy->stale_timeout_ms) {
            return unknown(SOURCE_REASON_EVIDENCE_STALE);
        }
        source_detection_observation_t result = {
            .candidate_state = SOURCE_STATE_UNKNOWN,
            .reason = SOURCE_REASON_NONE,
            .evidence_fresh = true,
            .conflict = false,
        };
        /* EM500/DMG610 register 0x2100 is the documented "OR of all digital
         * inputs" -- a BITMASK, not an enumeration. The site rule is therefore
         * "zero means grid, any energised input means generator", and that is
         * how it is evaluated whenever the commissioned grid value is zero: any
         * non-zero word is generator, whichever bit carries it.
         *
         * Exact equality alone was wrong here in the dangerous direction. With
         * generator_value commissioned as 1, a second input wired on the same
         * meter makes the register read 2 or 3 while the genset carries the
         * plant. That matched neither configured value, so detection reported
         * UNKNOWN_INPUT_VALUE, control stayed fail-closed, no curtailment
         * command was issued, and PV kept exporting into a generator that was
         * never protected. Treating every non-zero word as generator moves that
         * case from "silently uncurtailed" to "curtailed", which is the safe
         * direction and is what the register's own documented meaning says.
         *
         * A site that commissions the mapping the other way round -- a non-zero
         * grid value -- is NOT a bitmask site and keeps strict equality, so an
         * inverted or bespoke wiring is never reinterpreted by this rule. */
        if (policy->single_grid_value == 0U) {
            result.candidate_state = evidence->single_raw_value == 0U
                                         ? SOURCE_STATE_GRID
                                         : SOURCE_STATE_GENERATOR;
            return result;
        }

        if (evidence->single_raw_value == policy->single_grid_value) {
            result.candidate_state = SOURCE_STATE_GRID;
        } else if (evidence->single_raw_value == policy->single_generator_value) {
            result.candidate_state = SOURCE_STATE_GENERATOR;
        } else {
            result.reason = SOURCE_REASON_UNKNOWN_INPUT_VALUE;
        }
        return result;
    }

    if (policy->mode == SOURCE_DETECTION_MODE_DUAL_METER) {
        if (!isfinite(policy->grid_threshold_kw) ||
            !isfinite(policy->generator_threshold_kw) ||
            policy->grid_threshold_kw <= 0.0f ||
            policy->generator_threshold_kw <= 0.0f) {
            return unknown(SOURCE_REASON_INVALID_CONFIG);
        }
        if (!evidence->grid_has_sample || !evidence->generator_has_sample) {
            return unknown(SOURCE_REASON_EVIDENCE_UNAVAILABLE);
        }
        if (evidence->grid_age_ms > policy->stale_timeout_ms ||
            evidence->generator_age_ms > policy->stale_timeout_ms) {
            return unknown(SOURCE_REASON_EVIDENCE_STALE);
        }
        if (!isfinite(evidence->grid_power_kw) ||
            !isfinite(evidence->generator_power_kw)) {
            return unknown(SOURCE_REASON_NON_FINITE);
        }

        const bool grid_active = evidence->grid_power_kw > policy->grid_threshold_kw;
        const bool generator_active = evidence->generator_power_kw > policy->generator_threshold_kw;
        if (grid_active && generator_active) return unknown(SOURCE_REASON_CONFLICT);
        if (!grid_active && !generator_active) return unknown(SOURCE_REASON_NO_SOURCE);

        source_detection_observation_t result = {
            .candidate_state = grid_active ? SOURCE_STATE_GRID : SOURCE_STATE_GENERATOR,
            .reason = SOURCE_REASON_NONE,
            .evidence_fresh = true,
            .conflict = false,
        };
        return result;
    }

    return unknown(SOURCE_REASON_INVALID_CONFIG);
}

source_tariff_t source_detection_tariff(source_state_t state)
{
    if (state == SOURCE_STATE_GRID) return SOURCE_TARIFF_1;
    if (state == SOURCE_STATE_GENERATOR) return SOURCE_TARIFF_2;
    return SOURCE_TARIFF_NONE;
}

void source_detection_reset(source_detection_memory_t *memory)
{
    if (memory) memset(memory, 0, sizeof(*memory));
}

source_detection_result_t source_detection_step(
    source_detection_memory_t *memory,
    const source_detection_policy_t *policy,
    const source_detection_evidence_t *evidence,
    uint32_t now_ms)
{
    source_detection_result_t result = {0};
    source_detection_observation_t observation = source_detection_observe(policy, evidence);
    result.candidate_state = observation.candidate_state;
    result.evidence_fresh = observation.evidence_fresh;
    result.conflict = observation.conflict;

    if (!memory || !policy) {
        result.state = SOURCE_STATE_UNKNOWN;
        result.reason = SOURCE_REASON_INVALID_CONFIG;
        result.tariff = SOURCE_TARIFF_NONE;
        result.fail_closed = true;
        return result;
    }

    if (observation.candidate_state == SOURCE_STATE_UNKNOWN) {
        source_detection_reset(memory);
        result.state = SOURCE_STATE_UNKNOWN;
        result.reason = observation.reason;
        result.tariff = SOURCE_TARIFF_NONE;
        result.fail_closed = true;
        return result;
    }

    if (!memory->pending_active && memory->stable_state == observation.candidate_state) {
        result.state = memory->stable_state;
        result.reason = SOURCE_REASON_NONE;
    } else {
        if (!memory->pending_active || memory->pending_state != observation.candidate_state) {
            memory->pending_active = true;
            memory->pending_state = observation.candidate_state;
            memory->pending_since_ms = now_ms;
            memory->stable_state = SOURCE_STATE_UNKNOWN;
        }

        if ((uint32_t)(now_ms - memory->pending_since_ms) >= policy->debounce_ms) {
            memory->stable_state = observation.candidate_state;
            memory->pending_state = SOURCE_STATE_UNKNOWN;
            memory->pending_since_ms = 0U;
            memory->pending_active = false;
            result.state = memory->stable_state;
            result.reason = SOURCE_REASON_NONE;
        } else {
            result.state = SOURCE_STATE_UNKNOWN;
            result.reason = SOURCE_REASON_DEBOUNCE_PENDING;
            result.transition_pending = true;
        }
    }

    result.tariff = source_detection_tariff(result.state);
    result.control_allowed = result.state != SOURCE_STATE_UNKNOWN;
    result.fail_closed = !result.control_allowed;
    return result;
}

const char *source_detection_state_name(source_state_t state)
{
    switch (state) {
    case SOURCE_STATE_GRID: return "grid";
    case SOURCE_STATE_GENERATOR: return "generator";
    case SOURCE_STATE_UNKNOWN:
    default: return "unknown";
    }
}

const char *source_detection_reason_name(source_reason_t reason)
{
    switch (reason) {
    case SOURCE_REASON_NONE: return "none";
    case SOURCE_REASON_NOT_CONFIGURED: return "not_configured";
    case SOURCE_REASON_INVALID_CONFIG: return "invalid_config";
    case SOURCE_REASON_EVIDENCE_UNAVAILABLE: return "evidence_unavailable";
    case SOURCE_REASON_EVIDENCE_STALE: return "evidence_stale";
    case SOURCE_REASON_NON_FINITE: return "non_finite";
    case SOURCE_REASON_UNKNOWN_INPUT_VALUE: return "unknown_input_value";
    case SOURCE_REASON_CONFLICT: return "conflict";
    case SOURCE_REASON_NO_SOURCE: return "no_source";
    case SOURCE_REASON_DEBOUNCE_PENDING: return "debounce_pending";
    default: return "invalid_reason";
    }
}
