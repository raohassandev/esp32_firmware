#include "generator_fleet_limit.h"

#include <math.h>

#include "source_mode.h"

_Static_assert(GENERATOR_FLEET_MAX_ENGINES == SOURCE_MAX_GENERATORS,
               "the fleet limit and the source-mode generator channels must agree");

static generator_fleet_limit_t fail_closed(generator_fleet_reason_t reason)
{
    generator_fleet_limit_t result = {
        .known = false,
        .online_count = 0U,
        .online_rated_kw = 0.0f,
        .minimum_loading_kw = 0.0f,
        .required_generator_kw = 0.0f,
        /* Zero PV. The most conservative reading of an unknown running set is not
         * a guessed denominator, it is no command at all. */
        .safe_pv_kw = 0.0f,
        .reason = (uint8_t)reason,
    };
    return result;
}

static bool limits_usable(const generator_engine_input_t *engine)
{
    return isfinite(engine->rated_kw) && engine->rated_kw > 0.0f &&
           isfinite(engine->minimum_loading_percent) &&
           engine->minimum_loading_percent >= 0.0f &&
           engine->minimum_loading_percent <= 100.0f &&
           isfinite(engine->reserve_kw) && engine->reserve_kw >= 0.0f &&
           isfinite(engine->reverse_power_margin_kw) &&
           engine->reverse_power_margin_kw >= 0.0f;
}

generator_fleet_limit_t generator_fleet_limit_evaluate(const generator_fleet_input_t *input)
{
    if (!input) return fail_closed(GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    if (!input->evidence_fresh || !isfinite(input->facility_load_kw) ||
        input->facility_load_kw < 0.0f) {
        return fail_closed(GENERATOR_FLEET_LOAD_UNKNOWN);
    }

    uint8_t count = input->engine_count;
    if (count > (uint8_t)GENERATOR_FLEET_MAX_ENGINES) count = (uint8_t)GENERATOR_FLEET_MAX_ENGINES;

    uint8_t configured_count = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (input->engines[index].configured) configured_count++;
    }
    if (configured_count == 0U) return fail_closed(GENERATOR_FLEET_NO_ENGINE_CONFIGURED);

    /* A meter attributed to a slot the policy does not describe means the site can
     * run an engine of unknown rating. Every running configuration that includes it
     * has an unknown denominator, so no limit may be computed at all -- not even
     * for the configurations that happen not to include it, because this module
     * cannot tell whether that engine is on the bus. */
    for (uint8_t index = 0U; index < count; ++index) {
        if (input->engines[index].metered && !input->engines[index].configured) {
            return fail_closed(GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
        }
    }

    uint8_t online_count = 0U;
    float online_rated_kw = 0.0f;
    float worst_minimum_loading_percent = 0.0f;
    float reserve_total_kw = 0.0f;
    float margin_total_kw = 0.0f;

    for (uint8_t index = 0U; index < count; ++index) {
        const generator_engine_input_t *engine = &input->engines[index];
        if (!engine->configured) continue;

        /* An engine slot in service with no usable rating cannot be part of any
         * denominator. Refuse the whole evaluation rather than the slot: excluding
         * it would shrink the floor and permit more PV, which is exactly backwards. */
        if (!limits_usable(engine)) return fail_closed(GENERATOR_FLEET_RATING_UNKNOWN);

        bool online;
        if (engine->metered) {
            /* Attributed to a meter: the sample decides. A stale, offline or
             * degraded generator meter says nothing about its engine, and guessing
             * either way is unsafe. */
            if (!engine->sample_fresh) return fail_closed(GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
            if (!isfinite(engine->measured_kw)) {
                return fail_closed(GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
            }
            online = true;
        } else if (configured_count == 1U && input->allow_unmetered_single_engine) {
            /* The legacy single-generator site: no generator-role meter exists
             * anywhere, and exactly one engine is described. There is no ambiguity
             * to resolve, and this reproduces the pre-existing behaviour exactly. */
            online = true;
        } else {
            /* Two or more engines and no way to attribute power to them. The
             * running set is genuinely unknown. */
            return fail_closed(GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
        }

        if (!online) continue;
        online_count++;
        online_rated_kw += engine->rated_kw;
        reserve_total_kw += engine->reserve_kw;
        margin_total_kw += engine->reverse_power_margin_kw;
        if (engine->minimum_loading_percent > worst_minimum_loading_percent) {
            worst_minimum_loading_percent = engine->minimum_loading_percent;
        }
    }

    if (online_count == 0U) return fail_closed(GENERATOR_FLEET_NO_ENGINE_ONLINE);
    if (!isfinite(online_rated_kw) || online_rated_kw <= 0.0f ||
        !isfinite(reserve_total_kw) || !isfinite(margin_total_kw)) {
        return fail_closed(GENERATOR_FLEET_RATING_UNKNOWN);
    }

    /*
     * HOW MINIMUM LOADING AGGREGATES ACROSS ENGINES -- and why it is the worst
     * percentage, not the per-engine sum.
     *
     * Gensets in parallel under isochronous kW load sharing carry load in
     * proportion to their ratings, so every engine sits at the SAME percentage
     * loading. Total load P leaves engine i at P / sum(rated) percent. The binding
     * constraint is therefore the engine with the HIGHEST minimum-loading figure:
     *
     *     P >= sum(rated_i) * max(percent_i) / 100
     *
     * Summing the per-engine minima instead gives sum(rated_i * percent_i) / 100,
     * which is SMALLER whenever the figures differ -- it permits more PV than the
     * most-constrained engine tolerates, and the engine that gets under-loaded is
     * the one whose own protection relay trips. The sum is the intuitive formula
     * and it is the unsafe one. This module uses the worst percentage. Where every
     * engine carries the same figure -- the normal case, and every single-engine
     * case -- the two are identical, so nothing changes for an upgraded unit.
     *
     * Reserve and reverse-power margin are absolute per-engine kW quantities and
     * are SUMMED: each engine needs its own headroom, and summing is also the
     * conservative direction.
     *
     * OWNER DECISION. This assumes proportional (isochronous) kW load sharing. A
     * plant running base-load or droop sharing, where one engine is deliberately
     * held at a fixed kW, does not distribute load proportionally and its binding
     * constraint is different. That case is NOT modelled here and no figure for it
     * has been invented.
     */
    const float minimum_loading_kw =
        online_rated_kw * worst_minimum_loading_percent / 100.0f;

    /* Reduced to one equivalent machine and handed to the already-tested policy
     * function, so the safe-PV arithmetic and its fail-closed guards exist in
     * exactly one place. */
    const generator_limit_input_t equivalent = {
        .evidence_fresh = true,
        .facility_load_kw = input->facility_load_kw,
        .running_generator_rated_kw = online_rated_kw,
        .minimum_loading_percent = worst_minimum_loading_percent,
        .reserve_kw = reserve_total_kw,
        .reverse_power_margin_kw = margin_total_kw,
    };
    const float safe_pv_kw = source_mode_generator_safe_pv_kw(&equivalent);

    const float required_kw = minimum_loading_kw + reserve_total_kw + margin_total_kw;
    if (!isfinite(minimum_loading_kw) || !isfinite(required_kw) || !isfinite(safe_pv_kw)) {
        return fail_closed(GENERATOR_FLEET_RATING_UNKNOWN);
    }

    generator_fleet_limit_t result = {
        .known = true,
        .online_count = online_count,
        .online_rated_kw = online_rated_kw,
        .minimum_loading_kw = minimum_loading_kw,
        .required_generator_kw = required_kw,
        .safe_pv_kw = safe_pv_kw,
        .reason = (uint8_t)GENERATOR_FLEET_OK,
    };
    return result;
}

static const char *const REASON_IDS[GENERATOR_FLEET_REASON_COUNT] = {
    "ok",
    "no_engine_configured",
    "rating_unknown",
    "running_set_unknown",
    "no_engine_online",
    "load_unknown",
};

const char *generator_fleet_reason_id(uint8_t reason)
{
    return reason < (uint8_t)GENERATOR_FLEET_REASON_COUNT ? REASON_IDS[reason]
                                                          : "running_set_unknown";
}
