#include "source_mode.h"

#include <math.h>

source_mode_result_t source_mode_evaluate(const source_evidence_t *evidence)
{
    source_mode_result_t result = {
        .mode = SOURCE_MODE_UNKNOWN,
        .control_allowed = false,
        .transition_active = false,
        .evidence_conflict = false,
    };
    if (!evidence || !evidence->evidence_fresh) return result;

    if (evidence->transfer_active) {
        result.mode = SOURCE_MODE_TRANSFER;
        result.transition_active = true;
        return result;
    }

    if (evidence->generator_breaker_closed && !evidence->generator_running) {
        result.mode = SOURCE_MODE_CONFLICT;
        result.evidence_conflict = true;
        return result;
    }

    if (evidence->grid_breaker_closed && !evidence->grid_available) {
        result.mode = SOURCE_MODE_CONFLICT;
        result.evidence_conflict = true;
        return result;
    }

    if (evidence->grid_breaker_closed && evidence->generator_breaker_closed) {
        if (!evidence->grid_generator_synchronized) {
            result.mode = SOURCE_MODE_CONFLICT;
            result.evidence_conflict = true;
            return result;
        }
        result.mode = SOURCE_MODE_GRID_GENERATOR_SYNC;
        result.control_allowed = true;
        return result;
    }

    if (evidence->grid_breaker_closed) {
        result.mode = SOURCE_MODE_GRID_ONLY;
        result.control_allowed = true;
        return result;
    }

    if (evidence->generator_breaker_closed && evidence->generator_running) {
        result.mode = evidence->grid_available ? SOURCE_MODE_GENERATOR_ONLY : SOURCE_MODE_ISLAND;
        result.control_allowed = true;
        return result;
    }

    if (!evidence->grid_breaker_closed && !evidence->generator_breaker_closed) {
        result.mode = SOURCE_MODE_NO_SOURCE;
        return result;
    }

    return result;
}

float source_mode_generator_safe_pv_kw(const generator_limit_input_t *input)
{
    if (!input || !input->evidence_fresh ||
        !isfinite(input->facility_load_kw) || input->facility_load_kw < 0.0f ||
        !isfinite(input->running_generator_rated_kw) || input->running_generator_rated_kw <= 0.0f ||
        !isfinite(input->minimum_loading_percent) || input->minimum_loading_percent < 0.0f ||
        input->minimum_loading_percent > 100.0f ||
        !isfinite(input->reserve_kw) || input->reserve_kw < 0.0f ||
        !isfinite(input->reverse_power_margin_kw) || input->reverse_power_margin_kw < 0.0f) {
        return 0.0f;
    }

    const float minimum_generator_kw =
        input->running_generator_rated_kw * input->minimum_loading_percent / 100.0f;
    const float required_generator_kw = minimum_generator_kw + input->reserve_kw +
                                        input->reverse_power_margin_kw;
    const float safe_pv_kw = input->facility_load_kw - required_generator_kw;
    return isfinite(safe_pv_kw) && safe_pv_kw > 0.0f ? safe_pv_kw : 0.0f;
}

const char *source_mode_name(source_mode_t mode)
{
    switch (mode) {
    case SOURCE_MODE_NO_SOURCE: return "no_source";
    case SOURCE_MODE_GRID_ONLY: return "grid_only";
    case SOURCE_MODE_GENERATOR_ONLY: return "generator_only";
    case SOURCE_MODE_GRID_GENERATOR_SYNC: return "grid_generator_synchronized";
    case SOURCE_MODE_TRANSFER: return "transfer";
    case SOURCE_MODE_ISLAND: return "island";
    case SOURCE_MODE_CONFLICT: return "conflict";
    case SOURCE_MODE_UNKNOWN:
    default: return "unknown";
    }
}
