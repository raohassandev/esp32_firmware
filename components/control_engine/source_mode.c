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

generator_fleet_result_t source_mode_aggregate_generators(
    const generator_channel_evidence_t channels[SOURCE_MAX_GENERATORS])
{
    generator_fleet_result_t result = {0};
    if (!channels) return result;

    bool any_configured = false;
    for (uint8_t i = 0; i < SOURCE_MAX_GENERATORS; ++i) {
        const generator_channel_evidence_t *channel = &channels[i];
        if (!channel->configured) continue;
        any_configured = true;

        /* Run/breaker evidence is required for every commissioned channel, but
         * a stopped/open genset does not need a fresh power sample. Power is
         * required only once the breaker is closed and the machine is carrying
         * the plant. */
        if (!channel->evidence_fresh ||
            !isfinite(channel->rated_kw) || channel->rated_kw <= 0.0f ||
            !isfinite(channel->minimum_loading_percent) ||
            channel->minimum_loading_percent < 0.0f ||
            channel->minimum_loading_percent > 100.0f ||
            !isfinite(channel->reserve_kw) || channel->reserve_kw < 0.0f ||
            !isfinite(channel->reverse_power_margin_kw) ||
            channel->reverse_power_margin_kw < 0.0f) {
            result.conflict = true;
            return result;
        }

        if (channel->breaker_closed && !channel->running) {
            result.conflict = true;
            return result;
        }
        if (!channel->breaker_closed) continue;

        /* meter_config.active_power_scale is the commissioning point for sign
         * normalization. Runtime therefore requires generator contribution to
         * arrive as non-negative kW and never takes fabs() to hide a wrong sign. */
        if (!channel->measurement_fresh || !isfinite(channel->measured_kw) ||
            channel->measured_kw < 0.0f) {
            result.conflict = true;
            return result;
        }

        result.running_count++;
        result.running_rated_kw += channel->rated_kw;
        result.measured_total_kw += channel->measured_kw;
        result.required_minimum_kw +=
            channel->rated_kw * channel->minimum_loading_percent / 100.0f +
            channel->reserve_kw + channel->reverse_power_margin_kw;
    }

    result.valid = any_configured && !result.conflict && result.running_count > 0U &&
                   isfinite(result.running_rated_kw) &&
                   isfinite(result.measured_total_kw) &&
                   isfinite(result.required_minimum_kw);
    if (!result.valid) {
        result.running_count = 0U;
        result.running_rated_kw = 0.0f;
        result.measured_total_kw = 0.0f;
        result.required_minimum_kw = 0.0f;
    }
    return result;
}

float source_mode_generator_fleet_safe_pv_kw(float facility_load_kw,
                                             const generator_fleet_result_t *fleet)
{
    if (!fleet || !fleet->valid || fleet->conflict || fleet->running_count == 0U ||
        !isfinite(facility_load_kw) || facility_load_kw < 0.0f ||
        !isfinite(fleet->running_rated_kw) || fleet->running_rated_kw <= 0.0f ||
        !isfinite(fleet->required_minimum_kw) || fleet->required_minimum_kw < 0.0f) {
        return 0.0f;
    }
    const float safe_pv_kw = facility_load_kw - fleet->required_minimum_kw;
    return isfinite(safe_pv_kw) && safe_pv_kw > 0.0f ? safe_pv_kw : 0.0f;
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

source_mode_result_t source_mode_from_measured_source(measured_source_t source,
                                                      bool evidence_fresh)
{
    source_mode_result_t result = {
        .mode = SOURCE_MODE_UNKNOWN,
        .control_allowed = false,
        .transition_active = false,
        .evidence_conflict = false,
    };
    if (!evidence_fresh) return result;

    switch (source) {
    case MEASURED_SOURCE_GRID:
        result.mode = SOURCE_MODE_GRID_ONLY;
        result.control_allowed = true;
        break;
    case MEASURED_SOURCE_GENERATOR:
        result.mode = SOURCE_MODE_GENERATOR_ONLY;
        result.control_allowed = true;
        break;
    case MEASURED_SOURCE_UNKNOWN:
    default:
        /* Left fail-closed. Measurement cannot distinguish "no source" from
         * "source not yet identified", and guessing either way is unsafe. */
        break;
    }
    return result;
}
