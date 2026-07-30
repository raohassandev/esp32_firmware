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
        /* Never echoed as a supported mode when nothing was computed. */
        .sharing_mode = (uint8_t)GENERATOR_SHARING_UNSET,
        .base_loaded_count = 0U,
        .base_load_total_kw = 0.0f,
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

/* The commissioned setpoint-agreement band for one engine, or a negative value when
 * no tolerance is commissioned. See generator_fleet_input_t for the whole argument;
 * the two rules executed here are:
 *
 *   - a value that is non-finite, negative, zero, or (for the percentage) above 100 is
 *     NOT a commissioned tolerance and contributes no band. A zero band on a physical
 *     measurement is not a tolerance, it is a bug;
 *   - when BOTH are commissioned the NARROWER band wins. That is the opposite of
 *     inverter_write_confirmation.c's "wider" rule, and deliberately so: that band
 *     appears on both sides of its test, so widening it is partly conservative. This
 *     band appears on exactly one side, |measured - setpoint| <= band, so widening it
 *     is purely permissive.
 */
float generator_base_load_tolerance_band_kw(float tolerance_kw,
                                            float tolerance_percent_of_rating,
                                            float rated_kw)
{
    float band_kw = -1.0f;

    if (isfinite(tolerance_kw) && tolerance_kw > 0.0f) band_kw = tolerance_kw;

    if (isfinite(tolerance_percent_of_rating) && tolerance_percent_of_rating > 0.0f &&
        tolerance_percent_of_rating <= 100.0f && isfinite(rated_kw) && rated_kw > 0.0f) {
        const float percent_band_kw = rated_kw * tolerance_percent_of_rating / 100.0f;
        if (isfinite(percent_band_kw) && percent_band_kw > 0.0f) {
            /* The narrower of the two, which is the conservative reading. */
            band_kw = (band_kw < 0.0f || percent_band_kw < band_kw) ? percent_band_kw : band_kw;
        }
    }

    return band_kw;
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
    bool online_slot[GENERATOR_FLEET_MAX_ENGINES] = {false};

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
        online_slot[index] = true;
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
     * WHICH ENGINE BINDS THE FLOOR DEPENDS ON HOW THE PLANT SHARES LOAD.
     *
     * The floor this module reports is defined, in every mode, as the SMALLEST TOTAL
     * generator load P at which every online engine independently satisfies its own
     * minimum loading. Nothing below is a heuristic; each expression is that
     * definition solved for the mode's own sharing law. Write m_i = rated_i *
     * percent_i / 100 for engine i's own minimum in kW.
     *
     * Reserve and reverse-power margin are absolute per-engine kW quantities and are
     * SUMMED in every mode: each engine needs its own headroom, and summing is also
     * the conservative direction.
     *
     * The mode itself is commissioned, never inferred. See generator_sharing_mode_t
     * for why the default is "no mode" rather than the isochronous law this module
     * used to assume silently.
     */
    uint8_t mode = input->sharing_mode;

    /*
     * THE SINGLE-ENGINE EXEMPTION, and it is the only one.
     *
     * With exactly one engine on the bus there is no load to share. Every sharing
     * law -- isochronous, base-load, droop, or one nobody has named -- gives that
     * engine the entire plant load, so the floor is rated * percent / 100 under all
     * of them and the mode changes nothing. Demanding a commissioned sharing mode
     * here would demand a quantity that has no referent, and it would break every
     * single-generator site that is already commissioned and running: those units
     * have no such field stored, so they would migrate to UNSET and stop commanding
     * PV. The exemption is therefore what keeps existing behaviour bit-for-bit.
     *
     * It applies ONLY to UNSET. DROOP is refused even with one engine online,
     * because it is an affirmative statement that the plant's sharing law is one
     * this module does not model, and which engines are on the bus is a runtime fact
     * -- the same site will have two engines online later in the day, and honouring
     * droop now would mean the controller commands PV on a droop-shared plant
     * whenever it happens to be running one machine. An out-of-range value is
     * refused for the same reason: it is not evidence of anything.
     */
    if (online_count == 1U && mode == (uint8_t)GENERATOR_SHARING_UNSET) {
        mode = (uint8_t)GENERATOR_SHARING_ISOCHRONOUS;
    }
    if (mode == (uint8_t)GENERATOR_SHARING_UNSET) {
        return fail_closed(GENERATOR_FLEET_SHARING_MODE_UNSET);
    }
    if (!generator_sharing_mode_supported(mode)) {
        return fail_closed(GENERATOR_FLEET_SHARING_MODE_UNSUPPORTED);
    }

    /* The floor is always reduced to ONE equivalent machine and handed to the
     * already-tested policy function, so the safe-PV arithmetic and its fail-closed
     * guards exist in exactly one place. `swing_rated_kw` and `worst_swing_percent`
     * are that machine's rating and minimum-loading percentage;
     * `base_load_total_kw` enters as additional absolute kW that must stay on the
     * generators, which is exactly what the reserve term already means. */
    float swing_rated_kw = 0.0f;
    float worst_swing_percent = 0.0f;
    float base_load_total_kw = 0.0f;
    uint8_t base_loaded_count = 0U;
    uint8_t swing_count = 0U;

    if (mode == (uint8_t)GENERATOR_SHARING_ISOCHRONOUS) {
        /*
         * ISOCHRONOUS / PROPORTIONAL. Every engine sits at the SAME percentage of
         * its own rating, so total load P leaves engine i carrying
         * P * rated_i / sum(rated). Requiring that to be at least m_i for every i:
         *
         *     P * rated_i / sum(rated) >= rated_i * percent_i / 100
         *     P                        >= sum(rated) * percent_i / 100   for all i
         *     P                        >= sum(rated) * max(percent_i) / 100
         *
         * The binding constraint is the engine with the HIGHEST minimum-loading
         * percentage. Summing the per-engine minima instead gives
         * sum(rated_i * percent_i) / 100, which is SMALLER whenever the figures
         * differ -- it permits more PV than the most-constrained engine tolerates,
         * and the engine that gets under-loaded is the one whose own protection
         * relay trips. The sum is the intuitive formula and it is the unsafe one.
         * Where every engine carries the same figure -- the normal case, and every
         * single-engine case -- the two are identical.
         */
        swing_rated_kw = online_rated_kw;
        worst_swing_percent = worst_minimum_loading_percent;
    } else {
        /*
         * BASE LOAD (fixed kW). Engines with role BASE_LOAD are held at a
         * commissioned setpoint s_i by their own governors, independent of the
         * total. The swing engines absorb the remainder P - sum(s_i) and share THAT
         * isochronously with each other, in proportion to their ratings.
         *
         * The two constraint families are genuinely different, which is the whole
         * point of separating the modes:
         *
         *  - A base-loaded engine satisfies s_i >= m_i or it does not, and no total
         *    plant load changes the answer, because its load does not follow the
         *    total. s_i < m_i is a COMMISSIONING FAULT: the plant has been set up to
         *    run an engine below its own minimum loading, and the controller must
         *    say so rather than compute a floor that pretends otherwise.
         *  - A swing engine j carries (P - sum(s_i)) * rated_j / sum_swing(rated).
         *    Requiring that to be at least m_j for every swing engine gives
         *
         *      P >= sum(s_i) + sum_swing(rated) * max_swing(percent_j) / 100
         *
         * which is the isochronous result applied to the swing subset, offset by the
         * base-loaded setpoints. With no base-loaded engine the expression reduces
         * to the isochronous one exactly, which is why the two modes agree on a
         * plant that has no base-loaded machine.
         *
         * Note this floor is NOT ordered against the isochronous floor in general:
         * a large setpoint raises it, and taking a big engine out of the
         * proportional term lowers it. That is precisely why the mode cannot be
         * defaulted -- there is no safe guess, only a commissioned fact.
         */
        for (uint8_t index = 0U; index < count; ++index) {
            if (!online_slot[index]) continue;
            const generator_engine_input_t *engine = &input->engines[index];

            if (engine->role == (uint8_t)GENERATOR_ENGINE_ROLE_BASE_LOAD) {
                /* A setpoint that is missing, non-positive, non-finite or above the
                 * machine's own rating is not a commissioned setpoint. */
                if (!isfinite(engine->base_load_kw) || engine->base_load_kw <= 0.0f ||
                    engine->base_load_kw > engine->rated_kw) {
                    return fail_closed(GENERATOR_FLEET_BASE_LOAD_UNKNOWN);
                }
                const float own_minimum_kw =
                    engine->rated_kw * engine->minimum_loading_percent / 100.0f;
                if (engine->base_load_kw < own_minimum_kw) {
                    return fail_closed(GENERATOR_FLEET_BASE_LOAD_BELOW_MINIMUM);
                }

                /*
                 * IS THE ENGINE ACTUALLY HOLDING THAT SETPOINT?
                 *
                 * Everything above this point checked the CONFIGURATION. The term
                 * base_load_total_kw is about to be added to the floor as kW the
                 * generators are absorbing, and that is a claim about the PLANT, not
                 * about the configuration. A governor that has dropped out of kW
                 * control makes the claim false in the permissive direction: the
                 * controller credits the base engines with load they are not carrying
                 * and permits more PV than the bus can lose.
                 *
                 * The three refusals below are ordered so an engineer is told the most
                 * specific true thing:
                 *
                 *  1. NO TOLERANCE COMMISSIONED. The check is impossible, so
                 *     base-load sharing is refused rather than performed blind. This
                 *     is the deliberate choice between the two available positions and
                 *     the argument for it is at generator_fleet_input_t. It is placed
                 *     AFTER the setpoint and minimum-loading checks so that an
                 *     already-broken setpoint still reports its own reason and not
                 *     this one.
                 *  2. NO USABLE MEASUREMENT. Unknown is not confirmation. A missing,
                 *     stale or non-finite sample says nothing about the governor, and
                 *     reading silence as agreement is the error this whole check
                 *     exists to remove. (A METERED engine whose sample is stale has
                 *     already failed closed as RUNNING_SET_UNKNOWN far above; what
                 *     reaches here is an engine counted online with no measurement at
                 *     all, which is only possible on the unmetered legacy path.)
                 *  3. DISAGREEMENT BEYOND THE BAND. The engine is not holding kW. It
                 *     is NOT reclassified as a swing engine and the evaluation is NOT
                 *     continued with it moved into the proportional term: this module
                 *     knows the governor is not doing what it was commissioned to do,
                 *     and knows nothing whatever about what it is doing instead.
                 *     Droop, isochronous and manual all give different floors, and
                 *     picking one would be inventing the very fact that just proved
                 *     itself unavailable.
                 *
                 * Symmetric comparison, both directions. Measuring BELOW the setpoint
                 * is the permissive error described above. Measuring ABOVE it is also
                 * a governor not under kW control, and it moves the swing engines'
                 * share in the other direction; neither is agreement.
                 */
                const float band_kw = generator_base_load_tolerance_band_kw(
                    input->base_load_tolerance_kw,
                    input->base_load_tolerance_percent_of_rating, engine->rated_kw);
                if (band_kw < 0.0f) {
                    return fail_closed(GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);
                }
                if (!engine->metered || !engine->sample_fresh ||
                    !isfinite(engine->measured_kw)) {
                    return fail_closed(GENERATOR_FLEET_BASE_LOAD_UNMEASURED);
                }
                if (fabsf(engine->measured_kw - engine->base_load_kw) > band_kw) {
                    return fail_closed(GENERATOR_FLEET_BASE_LOAD_DRIFT);
                }

                base_load_total_kw += engine->base_load_kw;
                base_loaded_count++;
            } else if (engine->role == (uint8_t)GENERATOR_ENGINE_ROLE_SWING) {
                swing_count++;
                swing_rated_kw += engine->rated_kw;
                if (engine->minimum_loading_percent > worst_swing_percent) {
                    worst_swing_percent = engine->minimum_loading_percent;
                }
            } else {
                /* No role declared, or a value this build does not recognise.
                 * Guessing is unavailable: calling a base-loaded engine "swing", or
                 * the reverse, moves the floor in either direction depending on the
                 * setpoints, so one of the two guesses is permissive on any given
                 * plant and nothing here can tell which. */
                return fail_closed(GENERATOR_FLEET_BASE_LOAD_UNKNOWN);
            }
        }

        /* Every online engine is pinned to a fixed kW. Nothing on the bus absorbs
         * the swing, so there is no total plant load for the controller to shape and
         * no floor that describes one. This is not an arithmetic edge case to round
         * away -- a plant in that state cannot hold frequency, and PV must not be
         * commanded into it. */
        if (swing_count == 0U) return fail_closed(GENERATOR_FLEET_NO_SWING_ENGINE);
        if (!isfinite(swing_rated_kw) || swing_rated_kw <= 0.0f ||
            !isfinite(base_load_total_kw)) {
            return fail_closed(GENERATOR_FLEET_RATING_UNKNOWN);
        }
    }

    const float minimum_loading_kw =
        swing_rated_kw * worst_swing_percent / 100.0f + base_load_total_kw;

    const generator_limit_input_t equivalent = {
        .evidence_fresh = true,
        .facility_load_kw = input->facility_load_kw,
        .running_generator_rated_kw = swing_rated_kw,
        .minimum_loading_percent = worst_swing_percent,
        /* The base-loaded setpoints are absolute kW that must remain on the
         * generators, which is what `reserve_kw` already means to this function. */
        .reserve_kw = reserve_total_kw + base_load_total_kw,
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
        .sharing_mode = mode,
        .base_loaded_count = base_loaded_count,
        .base_load_total_kw = base_load_total_kw,
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
    "sharing_mode_unset",
    "sharing_mode_unsupported",
    "base_load_setpoint_unknown",
    "base_load_below_minimum",
    "no_swing_engine",
    "base_load_tolerance_unset",
    "base_load_unmeasured",
    "base_load_setpoint_drift",
};

const char *generator_fleet_reason_id(uint8_t reason)
{
    return reason < (uint8_t)GENERATOR_FLEET_REASON_COUNT ? REASON_IDS[reason]
                                                          : "running_set_unknown";
}

static const char *const SHARING_MODE_IDS[GENERATOR_SHARING_MODE_COUNT] = {
    "unset",
    "isochronous",
    "base_load",
    "droop",
};

const char *generator_sharing_mode_id(uint8_t mode)
{
    /* An unrecognised value must never read as a supported mode. */
    return mode < (uint8_t)GENERATOR_SHARING_MODE_COUNT ? SHARING_MODE_IDS[mode] : "unset";
}

static const char *const ENGINE_ROLE_IDS[GENERATOR_ENGINE_ROLE_COUNT] = {
    "unset",
    "swing",
    "base_load",
};

const char *generator_engine_role_id(uint8_t role)
{
    return role < (uint8_t)GENERATOR_ENGINE_ROLE_COUNT ? ENGINE_ROLE_IDS[role] : "unset";
}

/* The supported set is enumerated positively, so a mode added to the enum without
 * a floor derivation is refused by default rather than silently accepted. */
bool generator_sharing_mode_supported(uint8_t mode)
{
    return mode == (uint8_t)GENERATOR_SHARING_ISOCHRONOUS ||
           mode == (uint8_t)GENERATOR_SHARING_BASE_LOAD;
}
