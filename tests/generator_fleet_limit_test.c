/* Host-compiled unit test for the aggregate generator limit.
 *
 * This executes the real decision; it does not grep source. The property it pins
 * down is the one the single-rating model got wrong: the minimum-loading floor
 * must be computed against the aggregate rating of the engines ACTUALLY ONLINE,
 * and every uncertainty about which those are must yield zero PV rather than a
 * guessed denominator.
 *
 * The failure mode this guards is asymmetric. Too small a denominator permits
 * more PV than the plant can carry, under-loads the engines and drives the plant
 * towards the reverse-power trip. Too large a denominator merely curtails PV. So
 * every ambiguous case below asserts the conservative answer, not merely "an
 * answer".
 *
 * THE FLOOR IS CHECKED AGAINST A BRUTE-FORCE REFERENCE, NOT AGAINST ITSELF.
 * ------------------------------------------------------------------------
 * For every sharing mode, brute_force_floor_kw() sweeps the total generator load
 * upward and returns the first total at which every online engine independently
 * satisfies its own minimum loading. That is the DEFINITION of the floor, computed
 * from first principles with no reference to the closed-form expressions in the
 * module under test. A closed form that disagrees with it is wrong, whichever one
 * looks more plausible. The same technique is what established that the isochronous
 * floor is sum(rated) x max(percent) / 100 and not the intuitive per-engine sum.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "generator_fleet_limit.h"

#define CLOSE_ENOUGH 0.001f

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) <= CLOSE_ENOUGH;
}

/* A metered engine: commissioned, attributed to a meter, and reporting a fresh
 * sample. A fresh sample is what counts an engine as being on the bus. */
static generator_engine_input_t metered(float rated_kw, float percent,
                                        float reserve_kw, float margin_kw,
                                        float measured_kw)
{
    generator_engine_input_t engine;
    memset(&engine, 0, sizeof(engine));
    engine.configured = true;
    engine.rated_kw = rated_kw;
    engine.minimum_loading_percent = percent;
    engine.reserve_kw = reserve_kw;
    engine.reverse_power_margin_kw = margin_kw;
    engine.metered = true;
    engine.sample_fresh = true;
    engine.measured_kw = measured_kw;
    return engine;
}

/* A base-loaded engine: held at a fixed kW setpoint by its own governor. */
static generator_engine_input_t base_loaded(float rated_kw, float percent,
                                            float reserve_kw, float margin_kw,
                                            float measured_kw, float setpoint_kw)
{
    generator_engine_input_t engine =
        metered(rated_kw, percent, reserve_kw, margin_kw, measured_kw);
    engine.role = GENERATOR_ENGINE_ROLE_BASE_LOAD;
    engine.base_load_kw = setpoint_kw;
    return engine;
}

/* A swing engine: absorbs whatever the base-loaded engines do not carry. */
static generator_engine_input_t swing(float rated_kw, float percent,
                                      float reserve_kw, float margin_kw,
                                      float measured_kw)
{
    generator_engine_input_t engine =
        metered(rated_kw, percent, reserve_kw, margin_kw, measured_kw);
    engine.role = GENERATOR_ENGINE_ROLE_SWING;
    return engine;
}

/* The isochronous fleet, which is what every pre-existing case here describes. The
 * mode is stated explicitly because it now has to be: an assumed sharing law is the
 * thing this change removed. */
static generator_fleet_input_t fleet(float load_kw)
{
    generator_fleet_input_t input;
    memset(&input, 0, sizeof(input));
    input.evidence_fresh = true;
    input.facility_load_kw = load_kw;
    input.sharing_mode = GENERATOR_SHARING_ISOCHRONOUS;
    input.engine_count = GENERATOR_FLEET_MAX_ENGINES;
    return input;
}

/*
 * THE TOLERANCE USED THROUGHOUT THIS TEST IS A TEST FIXTURE, NOT A PRODUCT DEFAULT.
 *
 * No manual, nameplate or site document in this repository states a base-load setpoint
 * agreement tolerance, so the firmware ships none and refuses base-load sharing until
 * an engineer commissions one. This file therefore has to state a figure in order to
 * exercise the arithmetic at all, and 5 kW is chosen for one reason only: every
 * base-load fixture below sets measured power exactly equal to the setpoint, so the
 * value merely has to be positive. Nothing about it is a recommendation, and
 * test_base_load_tolerance_unset_fails_closed() pins down that the firmware itself
 * supplies no such number.
 */
#define TEST_TOLERANCE_KW 5.0f

static generator_fleet_input_t base_load_fleet(float load_kw)
{
    generator_fleet_input_t input = fleet(load_kw);
    input.sharing_mode = GENERATOR_SHARING_BASE_LOAD;
    input.base_load_tolerance_kw = TEST_TOLERANCE_KW;
    return input;
}

/* The same fleet with nothing commissioned about setpoint agreement, which is the state
 * every upgraded unit arrives in. */
static generator_fleet_input_t base_load_fleet_untoleranced(float load_kw)
{
    generator_fleet_input_t input = base_load_fleet(load_kw);
    input.base_load_tolerance_kw = 0.0f;
    input.base_load_tolerance_percent_of_rating = 0.0f;
    return input;
}

/* ----------------------------------------------------- brute-force reference */

/* Every engine in these fleets is metered and fresh, so "online" is exactly
 * "configured". The brute force asserts that rather than reimplementing the
 * running-set rules, which are tested separately below. */
static bool engine_is_online(const generator_engine_input_t *engine)
{
    return engine->configured && engine->metered && engine->sample_fresh &&
           isfinite(engine->measured_kw);
}

/* Does every online engine independently satisfy its own minimum loading, when the
 * generators together carry total_kw?
 *
 * This is written from the physics of each sharing law, not from the module:
 *   - a base-loaded engine carries its setpoint, whatever the total;
 *   - the swing engines carry the remainder, divided in proportion to their ratings
 *     (they are isochronous with each other);
 *   - under isochronous sharing every engine is a swing engine.
 */
static bool minimums_all_met(const generator_fleet_input_t *in, float total_kw)
{
    const bool base_load = in->sharing_mode == GENERATOR_SHARING_BASE_LOAD;
    float base_total_kw = 0.0f;
    float swing_rated_kw = 0.0f;
    uint8_t swing_count = 0U;

    for (uint8_t i = 0; i < in->engine_count && i < GENERATOR_FLEET_MAX_ENGINES; ++i) {
        const generator_engine_input_t *engine = &in->engines[i];
        if (!engine_is_online(engine)) continue;
        if (base_load && engine->role == GENERATOR_ENGINE_ROLE_BASE_LOAD) {
            base_total_kw += engine->base_load_kw;
        } else {
            swing_count++;
            swing_rated_kw += engine->rated_kw;
        }
    }

    /* With every online engine pinned to a fixed kW, the total generator load is not
     * a free variable at all -- it is stuck at the sum of the setpoints. "The
     * smallest total at which every engine is satisfied" then has no referent as a
     * control limit, so no total qualifies. */
    if (swing_count == 0U) return false;

    for (uint8_t i = 0; i < in->engine_count && i < GENERATOR_FLEET_MAX_ENGINES; ++i) {
        const generator_engine_input_t *engine = &in->engines[i];
        if (!engine_is_online(engine)) continue;
        const float own_minimum_kw = engine->rated_kw * engine->minimum_loading_percent / 100.0f;

        float carried_kw;
        if (base_load && engine->role == GENERATOR_ENGINE_ROLE_BASE_LOAD) {
            carried_kw = engine->base_load_kw;
        } else if (swing_rated_kw > 0.0f) {
            carried_kw = (total_kw - base_total_kw) * engine->rated_kw / swing_rated_kw;
        } else {
            /* Nothing absorbs the swing. No total describes this plant. */
            return false;
        }
        if (carried_kw < own_minimum_kw - 1e-3f) return false;
    }
    return true;
}

/*
 * IS THE PREMISE THE BASE-LOAD MODEL RESTS ON ACTUALLY SUPPORTED BY THE EVIDENCE?
 *
 * minimums_all_met() above says "a base-loaded engine carries its setpoint, whatever the
 * total". That is not an arithmetic fact, it is a claim about a governor, and the whole
 * base-load floor stands on it. Written from first principles, the claim is supported
 * only when:
 *
 *   - a tolerance has been commissioned saying how much disagreement is normal. Without
 *     one there is no proposition to test, so the claim is unsupported. It is NOT
 *     vacuously true;
 *   - a fresh, finite measurement of that engine exists. Silence is not evidence;
 *   - that measurement agrees with the setpoint within the tolerance.
 *
 * When the premise fails, no total generator load satisfies the definition of the floor,
 * because the floor was derived from a sharing law the plant is not obeying. So the
 * reference answer is "no such total", exactly as it is for an engine commissioned below
 * its own minimum. This is computed here without reference to the module, which is what
 * makes it a check on the module rather than a restatement of it.
 */
static bool base_load_premise_supported(const generator_fleet_input_t *in)
{
    if (in->sharing_mode != GENERATOR_SHARING_BASE_LOAD) return true;

    for (uint8_t i = 0; i < in->engine_count && i < GENERATOR_FLEET_MAX_ENGINES; ++i) {
        const generator_engine_input_t *engine = &in->engines[i];
        if (!engine_is_online(engine)) continue;
        if (engine->role != GENERATOR_ENGINE_ROLE_BASE_LOAD) continue;

        /* The band, recomputed here from the two commissioned figures rather than
         * borrowed from the module: absolute if positive, percent-of-rating if in
         * range, and the NARROWER when both are stated, because this band sits on one
         * side of one comparison and widening it can only hide drift. */
        float band_kw = -1.0f;
        if (isfinite(in->base_load_tolerance_kw) && in->base_load_tolerance_kw > 0.0f) {
            band_kw = in->base_load_tolerance_kw;
        }
        const float percent = in->base_load_tolerance_percent_of_rating;
        if (isfinite(percent) && percent > 0.0f && percent <= 100.0f &&
            isfinite(engine->rated_kw) && engine->rated_kw > 0.0f) {
            const float from_percent = engine->rated_kw * percent / 100.0f;
            if (from_percent > 0.0f && (band_kw < 0.0f || from_percent < band_kw)) {
                band_kw = from_percent;
            }
        }
        if (band_kw < 0.0f) return false;

        if (!engine->metered || !engine->sample_fresh || !isfinite(engine->measured_kw)) {
            return false;
        }
        if (fabsf(engine->measured_kw - engine->base_load_kw) > band_kw) return false;
    }
    return true;
}

#define BRUTE_STEP_KW 0.005f
#define BRUTE_MAX_STEPS 800000L /* 4000 kW of search space */

/* The smallest total generator load at which every online engine independently
 * satisfies its own minimum. NAN when no total does, which is the honest answer for
 * a plant configured to under-load one of its machines, and equally for a plant whose
 * base-load premise the evidence does not support. */
static float brute_force_floor_kw(const generator_fleet_input_t *in)
{
    if (!base_load_premise_supported(in)) return NAN;
    for (long step = 0; step <= BRUTE_MAX_STEPS; ++step) {
        const float total_kw = (float)step * BRUTE_STEP_KW;
        if (minimums_all_met(in, total_kw)) return total_kw;
    }
    return NAN;
}

/* Compares the module's closed-form floor against the brute-force definition and
 * prints both, so the agreement is visible in the test output rather than merely
 * asserted. */
static void expect_floor_matches_brute_force(const char *label,
                                             const generator_fleet_input_t *in)
{
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(in);
    const float reference_kw = brute_force_floor_kw(in);

    if (!isfinite(reference_kw)) {
        /* No total plant load satisfies every engine. The module must refuse rather
         * than report a floor, because there is no floor to report. */
        printf("  %-46s brute force: no such total   module: %s\n", label,
               result.known ? "REPORTED A FLOOR" : generator_fleet_reason_id(result.reason));
        assert(!result.known);
        assert(near(result.safe_pv_kw, 0.0f));
        return;
    }

    printf("  %-46s brute force: %9.3f kW  module: %9.3f kW  (%s)\n", label,
           (double)reference_kw, (double)result.minimum_loading_kw,
           generator_sharing_mode_id(result.sharing_mode));
    assert(result.known);
    /* The brute force is a search on a BRUTE_STEP_KW grid, so it can overshoot the
     * true infimum by at most one step. */
    assert(fabsf(result.minimum_loading_kw - reference_kw) <= BRUTE_STEP_KW + CLOSE_ENOUGH);
    /* The closed form must never be BELOW the reference by more than the grid, which
     * is the direction that would permit too much PV. */
    assert(result.minimum_loading_kw >= reference_kw - BRUTE_STEP_KW - CLOSE_ENOUGH);
}

/* ------------------------------------------------------------------ fail closed */

static void test_null_and_zeroed_input_yield_no_pv(void)
{
    generator_fleet_limit_t result = generator_fleet_limit_evaluate(NULL);
    assert(!result.known);
    assert(near(result.safe_pv_kw, 0.0f));
    assert(result.online_count == 0U);
    assert(near(result.online_rated_kw, 0.0f));

    generator_fleet_input_t input;
    memset(&input, 0, sizeof(input));
    result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(near(result.safe_pv_kw, 0.0f));
}

static void test_stale_or_impossible_load_yields_no_pv(void)
{
    generator_fleet_input_t input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 400.0f);

    input.evidence_fresh = false;
    generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_LOAD_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));

    const float impossible[] = {NAN, INFINITY, -1.0f};
    for (size_t i = 0; i < sizeof(impossible) / sizeof(impossible[0]); ++i) {
        input = fleet(impossible[i]);
        input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 400.0f);
        result = generator_fleet_limit_evaluate(&input);
        assert(!result.known);
        assert(result.reason == GENERATOR_FLEET_LOAD_UNKNOWN);
        assert(near(result.safe_pv_kw, 0.0f));
    }
}

/* ----------------------------------------------------------------- one engine */

/* One metered engine: floor is rated x percent, plus reserve and margin. */
static void test_one_engine(void)
{
    generator_fleet_input_t input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 400.0f);

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(result.reason == GENERATOR_FLEET_OK);
    assert(result.online_count == 1U);
    assert(near(result.online_rated_kw, 500.0f));
    assert(near(result.minimum_loading_kw, 150.0f));
    assert(near(result.required_generator_kw, 165.0f));
    assert(near(result.safe_pv_kw, 235.0f));
}

/* THE REGRESSION THIS WHOLE CHANGE EXISTS TO PREVENT.
 *
 * Two identical engines online carry twice the minimum load of one. Computing
 * the floor from a single rating would allow 250 kW of PV where only 100 kW is
 * safe -- the plant would be under-loaded by 150 kW, which is the reverse-power
 * condition. */
static void test_two_engines_double_the_floor(void)
{
    generator_fleet_input_t input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 200.0f);
    input.engines[1] = metered(500.0f, 30.0f, 0.0f, 0.0f, 200.0f);

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(result.online_count == 2U);
    assert(near(result.online_rated_kw, 1000.0f));
    assert(near(result.minimum_loading_kw, 300.0f));
    assert(near(result.safe_pv_kw, 100.0f));

    /* The single-rating answer, for comparison: 400 - 150 = 250 kW. The aggregate
     * limit must be strictly and substantially more conservative. */
    assert(result.safe_pv_kw < 250.0f - CLOSE_ENOUGH);
}

static void test_three_engines(void)
{
    generator_fleet_input_t input = fleet(900.0f);
    input.engines[0] = metered(400.0f, 30.0f, 5.0f, 2.0f, 300.0f);
    input.engines[1] = metered(300.0f, 30.0f, 5.0f, 2.0f, 300.0f);
    input.engines[2] = metered(300.0f, 30.0f, 5.0f, 2.0f, 300.0f);

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(result.online_count == 3U);
    assert(near(result.online_rated_kw, 1000.0f));
    assert(near(result.minimum_loading_kw, 300.0f));
    /* Reserve and reverse-power margin are per-engine kW and are summed. */
    assert(near(result.required_generator_kw, 300.0f + 15.0f + 6.0f));
    assert(near(result.safe_pv_kw, 900.0f - 321.0f));
}

/* Unequal minimum-loading figures: the binding constraint is the WORST
 * percentage applied to the aggregate rating, not the sum of the per-engine
 * minima. Under proportional load sharing every engine sits at the same
 * percentage, so the sum would permit more PV than the most-constrained engine
 * tolerates. */
static void test_worst_percentage_binds_not_the_per_engine_sum(void)
{
    generator_fleet_input_t input = fleet(1000.0f);
    input.engines[0] = metered(500.0f, 40.0f, 0.0f, 0.0f, 500.0f);
    input.engines[1] = metered(500.0f, 20.0f, 0.0f, 0.0f, 500.0f);

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(near(result.online_rated_kw, 1000.0f));
    /* Worst percentage: 1000 x 40 % = 400 kW. */
    assert(near(result.minimum_loading_kw, 400.0f));
    /* The per-engine sum would have been 200 + 100 = 300 kW, permitting 100 kW
     * more PV than the 40 % engine can tolerate. */
    assert(result.minimum_loading_kw > 300.0f + CLOSE_ENOUGH);
    assert(near(result.safe_pv_kw, 600.0f));
}

/* Equal figures make the worst-percentage rule and the per-engine sum identical,
 * which is why an upgraded uniform-fleet site sees no change. */
static void test_equal_percentages_match_the_per_engine_sum(void)
{
    generator_fleet_input_t input = fleet(1000.0f);
    input.engines[0] = metered(400.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    input.engines[1] = metered(600.0f, 30.0f, 0.0f, 0.0f, 500.0f);

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    const float per_engine_sum = 400.0f * 0.30f + 600.0f * 0.30f;
    assert(near(result.minimum_loading_kw, per_engine_sum));
}

/* ------------------------------------------------------ holes in the evidence */

/* An engine online with no rating. Excluding it would shrink the denominator and
 * permit MORE PV, so the whole evaluation must fail closed instead. */
static void test_online_engine_without_a_rating_yields_no_pv(void)
{
    const float unusable_rating[] = {0.0f, -1.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(unusable_rating) / sizeof(unusable_rating[0]); ++i) {
        generator_fleet_input_t input = fleet(400.0f);
        input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 200.0f);
        input.engines[1] = metered(unusable_rating[i], 30.0f, 0.0f, 0.0f, 200.0f);
        const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
        assert(!result.known);
        assert(result.reason == GENERATOR_FLEET_RATING_UNKNOWN);
        assert(near(result.safe_pv_kw, 0.0f));
        assert(near(result.online_rated_kw, 0.0f));
    }

    /* An out-of-range or non-finite minimum-loading figure is the same hole. */
    const float unusable_percent[] = {-1.0f, 101.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(unusable_percent) / sizeof(unusable_percent[0]); ++i) {
        generator_fleet_input_t input = fleet(400.0f);
        input.engines[0] = metered(500.0f, unusable_percent[i], 0.0f, 0.0f, 200.0f);
        const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
        assert(!result.known);
        assert(result.reason == GENERATOR_FLEET_RATING_UNKNOWN);
        assert(near(result.safe_pv_kw, 0.0f));
    }
}

/* A rating with no engine online. Two shapes: the slot has a meter whose sample
 * is not usable, and the site has no meter at all for a multi-engine policy. */
static void test_rating_with_no_engine_online_yields_no_pv(void)
{
    /* Metered but the sample is not fresh: says nothing about the engine. */
    generator_fleet_input_t input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 400.0f);
    input.engines[0].sample_fresh = false;
    generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));

    /* Metered, fresh, but the measurement itself is not a number. */
    input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, NAN);
    result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);

    /* Two commissioned engines, no meter attributed to either. Nothing can say
     * which is running, so the aggregate rating is not knowable. */
    input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 200.0f);
    input.engines[1] = metered(500.0f, 30.0f, 0.0f, 0.0f, 200.0f);
    input.engines[0].metered = false;
    input.engines[0].sample_fresh = false;
    input.engines[1].metered = false;
    input.engines[1].sample_fresh = false;
    input.allow_unmetered_single_engine = true; /* does not apply: two engines */
    result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));
}

/* No engine commissioned at all: the policy is not commissioned, which is
 * distinct from "commissioned but nothing running". */
static void test_no_engine_configured_yields_no_pv(void)
{
    generator_fleet_input_t input = fleet(400.0f);
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_NO_ENGINE_CONFIGURED);
    assert(near(result.safe_pv_kw, 0.0f));
}

/* A meter attributed to a slot the policy does not describe. The site can run an
 * engine of unknown rating, so no configuration's denominator is trustworthy --
 * including the ones that appear complete. */
static void test_metered_but_unconfigured_slot_yields_no_pv(void)
{
    generator_fleet_input_t input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 400.0f);
    input.engines[1] = metered(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    input.engines[1].configured = false; /* metered, but no commissioned rating */

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));
}

/* ------------------------------------------------------------------- legacy */

/* A single-generator legacy configuration must behave exactly as before: no
 * generator-role meter exists on the site, one engine is commissioned, and the
 * limit is that engine's floor subtracted from the plant load.
 *
 * The expected numbers here are computed the way the pre-existing
 * single-generator code computed them, so a divergence fails this test. */
static void test_legacy_single_generator_is_unchanged(void)
{
    const float load_kw = 400.0f;
    const float rated_kw = 500.0f;
    const float percent = 30.0f;
    const float reserve_kw = 10.0f;
    const float margin_kw = 5.0f;

    generator_fleet_input_t input = fleet(load_kw);
    input.engines[0] = metered(rated_kw, percent, reserve_kw, margin_kw, 0.0f);
    input.engines[0].metered = false;      /* no generator-role meter exists */
    input.engines[0].sample_fresh = false;
    input.allow_unmetered_single_engine = true;

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(result.reason == GENERATOR_FLEET_OK);
    assert(result.online_count == 1U);
    assert(near(result.online_rated_kw, rated_kw));

    const float legacy_required = rated_kw * percent / 100.0f + reserve_kw + margin_kw;
    const float legacy_safe = load_kw - legacy_required;
    assert(near(result.required_generator_kw, legacy_required));
    assert(near(result.safe_pv_kw, legacy_safe));

    /* Without the explicit permission the same input fails closed: the fallback is
     * a deliberate legacy allowance, not a default. */
    input.allow_unmetered_single_engine = false;
    const generator_fleet_limit_t refused = generator_fleet_limit_evaluate(&input);
    assert(!refused.known);
    assert(refused.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    assert(near(refused.safe_pv_kw, 0.0f));
}

/* A load below the required generator floor must clamp to zero, never to a
 * negative command. */
static void test_load_below_the_floor_clamps_to_zero(void)
{
    generator_fleet_input_t input = fleet(100.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 100.0f);
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(near(result.safe_pv_kw, 0.0f));
    assert(result.safe_pv_kw >= 0.0f);
}

/* Adding an engine may only ever reduce the permitted PV. Stated as a property
 * over a sweep, because it is the invariant that makes the aggregate safe. */
static void test_adding_an_engine_never_increases_permitted_pv(void)
{
    for (float load_kw = 0.0f; load_kw <= 2000.0f; load_kw += 50.0f) {
        generator_fleet_input_t one = fleet(load_kw);
        one.engines[0] = metered(500.0f, 30.0f, 5.0f, 2.0f, load_kw);
        const generator_fleet_limit_t single = generator_fleet_limit_evaluate(&one);

        generator_fleet_input_t two = one;
        two.engines[1] = metered(300.0f, 35.0f, 5.0f, 2.0f, load_kw);
        const generator_fleet_limit_t pair = generator_fleet_limit_evaluate(&two);

        assert(single.known && pair.known);
        assert(pair.safe_pv_kw <= single.safe_pv_kw + CLOSE_ENOUGH);
        assert(pair.online_rated_kw > single.online_rated_kw);
    }
}

/* An engine_count larger than the array must not read past it. */
static void test_engine_count_is_bounded(void)
{
    generator_fleet_input_t input = fleet(400.0f);
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 400.0f);
    input.engine_count = 250U;
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.known);
    assert(result.online_count == 1U);
}

/* ------------------------------------------------------------- load sharing */

/* ISOCHRONOUS, against the brute-force definition, over fleets of one, two and
 * three engines with unequal ratings and unequal minimum-loading figures. */
static void test_isochronous_floor_matches_brute_force(void)
{
    printf("isochronous sharing:\n");

    generator_fleet_input_t one = fleet(400.0f);
    one.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 400.0f);
    expect_floor_matches_brute_force("one engine 500 kW @ 30 %", &one);

    generator_fleet_input_t equal = fleet(1000.0f);
    equal.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    equal.engines[1] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    expect_floor_matches_brute_force("two identical 500 kW @ 30 %", &equal);

    /* The case that proves the per-engine sum wrong: unequal percentages. */
    generator_fleet_input_t unequal = fleet(1000.0f);
    unequal.engines[0] = metered(500.0f, 40.0f, 0.0f, 0.0f, 500.0f);
    unequal.engines[1] = metered(500.0f, 20.0f, 0.0f, 0.0f, 500.0f);
    expect_floor_matches_brute_force("500 @ 40 % + 500 @ 20 %", &unequal);
    const generator_fleet_limit_t unequal_result = generator_fleet_limit_evaluate(&unequal);
    const float per_engine_sum = 500.0f * 0.40f + 500.0f * 0.20f;
    assert(unequal_result.minimum_loading_kw > per_engine_sum + CLOSE_ENOUGH);

    generator_fleet_input_t three = fleet(1200.0f);
    three.engines[0] = metered(400.0f, 25.0f, 5.0f, 2.0f, 400.0f);
    three.engines[1] = metered(300.0f, 45.0f, 5.0f, 2.0f, 400.0f);
    three.engines[2] = metered(250.0f, 15.0f, 5.0f, 2.0f, 400.0f);
    expect_floor_matches_brute_force("400 @ 25 % + 300 @ 45 % + 250 @ 15 %", &three);

    /* A sweep, so the agreement is not three lucky points. */
    for (float percent = 0.0f; percent <= 100.0f; percent += 5.0f) {
        for (float rated = 100.0f; rated <= 700.0f; rated += 150.0f) {
            generator_fleet_input_t input = fleet(2000.0f);
            input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
            input.engines[1] = metered(rated, percent, 0.0f, 0.0f, 500.0f);
            const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
            const float reference_kw = brute_force_floor_kw(&input);
            assert(result.known);
            assert(isfinite(reference_kw));
            assert(fabsf(result.minimum_loading_kw - reference_kw) <=
                   BRUTE_STEP_KW + CLOSE_ENOUGH);
        }
    }
}

/* BASE LOAD, against the same brute-force definition. The binding constraint here is
 * a different one: the base-loaded engines' setpoints are added to a proportional
 * term computed over the SWING engines only. */
static void test_base_load_floor_matches_brute_force(void)
{
    printf("base-load sharing:\n");

    /* One engine pinned at 300 kW, one swinging. The swing engine's own minimum is
     * 300 x 35 % = 105 kW, so the floor is 300 + 105 = 405 kW. */
    generator_fleet_input_t pair = base_load_fleet(900.0f);
    pair.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    pair.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
    expect_floor_matches_brute_force("base 500 @ 300 kW + swing 300 @ 35 %", &pair);
    const generator_fleet_limit_t pair_result = generator_fleet_limit_evaluate(&pair);
    assert(pair_result.base_loaded_count == 1U);
    assert(near(pair_result.base_load_total_kw, 300.0f));
    assert(pair_result.sharing_mode == GENERATOR_SHARING_BASE_LOAD);
    /* The whole online rating is still reported: it describes the machines, not the
     * floor's denominator. */
    assert(near(pair_result.online_rated_kw, 800.0f));

    /* Two base-loaded engines and one swing engine. */
    generator_fleet_input_t two_base = base_load_fleet(1400.0f);
    two_base.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 400.0f, 400.0f);
    two_base.engines[1] = base_loaded(400.0f, 25.0f, 0.0f, 0.0f, 250.0f, 250.0f);
    two_base.engines[2] = swing(300.0f, 40.0f, 0.0f, 0.0f, 500.0f);
    expect_floor_matches_brute_force("base 400 + base 250 + swing 300 @ 40 %", &two_base);

    /* One base-loaded engine and two swing engines with unequal percentages: the
     * worst SWING percentage binds, and the base-loaded engine's own 30 % does not
     * enter the proportional term at all. */
    generator_fleet_input_t two_swing = base_load_fleet(1400.0f);
    two_swing.engines[0] = base_loaded(600.0f, 30.0f, 0.0f, 0.0f, 400.0f, 400.0f);
    two_swing.engines[1] = swing(300.0f, 45.0f, 0.0f, 0.0f, 300.0f);
    two_swing.engines[2] = swing(200.0f, 20.0f, 0.0f, 0.0f, 200.0f);
    expect_floor_matches_brute_force("base 400 + swing 300 @ 45 % + 200 @ 20 %", &two_swing);

    /* A sweep over setpoints and swing percentages. */
    for (float setpoint = 200.0f; setpoint <= 500.0f; setpoint += 50.0f) {
        for (float percent = 0.0f; percent <= 100.0f; percent += 10.0f) {
            generator_fleet_input_t input = base_load_fleet(2500.0f);
            input.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, setpoint, setpoint);
            input.engines[1] = swing(400.0f, percent, 0.0f, 0.0f, 400.0f);
            const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
            const float reference_kw = brute_force_floor_kw(&input);
            /* setpoint >= 500 x 30 % = 150 kW throughout, so every case is
             * commissionable and the brute force always finds a total. */
            assert(result.known);
            assert(isfinite(reference_kw));
            assert(fabsf(result.minimum_loading_kw - reference_kw) <=
                   BRUTE_STEP_KW + CLOSE_ENOUGH);
        }
    }
}

/* A mixed fleet: three engines, unequal ratings, unequal minimum loadings, reserve
 * and reverse-power margin on each, one of them base-loaded. Checked end to end
 * against the brute force AND against the reserve/margin arithmetic, because those
 * are summed independently of the sharing law. */
static void test_mixed_fleet_end_to_end(void)
{
    printf("mixed fleet:\n");
    generator_fleet_input_t input = base_load_fleet(1500.0f);
    input.engines[0] = base_loaded(600.0f, 30.0f, 8.0f, 3.0f, 400.0f, 400.0f);
    input.engines[1] = swing(400.0f, 45.0f, 6.0f, 2.0f, 600.0f);
    input.engines[2] = swing(300.0f, 20.0f, 4.0f, 1.0f, 500.0f);
    expect_floor_matches_brute_force("base 600 @ 400 kW + swing 400/300", &input);

    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    /* Floor: 400 + (400 + 300) x 45 % = 400 + 315 = 715 kW. */
    assert(near(result.minimum_loading_kw, 715.0f));
    /* Reserve and margin are per-engine absolute kW and are summed in every mode. */
    const float reserve_total = 8.0f + 6.0f + 4.0f;
    const float margin_total = 3.0f + 2.0f + 1.0f;
    assert(near(result.required_generator_kw, 715.0f + reserve_total + margin_total));
    assert(near(result.safe_pv_kw, 1500.0f - (715.0f + reserve_total + margin_total)));
    assert(result.online_count == 3U);
    assert(near(result.online_rated_kw, 1300.0f));
}

/* NO SHARING MODE COMMISSIONED. Two engines on the bus and nothing says how they
 * divide the load, so which engine binds the floor is unknown and no PV may be
 * commanded. */
static void test_sharing_mode_unset_fails_closed(void)
{
    generator_fleet_input_t input = fleet(1000.0f);
    input.sharing_mode = GENERATOR_SHARING_UNSET;
    input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    input.engines[1] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_SHARING_MODE_UNSET);
    assert(near(result.safe_pv_kw, 0.0f));
    assert(result.sharing_mode == GENERATOR_SHARING_UNSET);

    /* Three engines, same answer. */
    input.engines[2] = metered(300.0f, 30.0f, 0.0f, 0.0f, 300.0f);
    const generator_fleet_limit_t three = generator_fleet_limit_evaluate(&input);
    assert(!three.known);
    assert(three.reason == GENERATOR_FLEET_SHARING_MODE_UNSET);
}

/* THE SINGLE-ENGINE EXEMPTION, which is what keeps a commissioned single-generator
 * unit working after an upgrade that leaves the new field at zero. One engine shares
 * load with nothing, so the floor is the same under every sharing law -- and the
 * numbers must be bit-for-bit those the module produced before the mode existed. */
static void test_single_online_engine_needs_no_sharing_mode(void)
{
    generator_fleet_input_t unset = fleet(400.0f);
    unset.sharing_mode = GENERATOR_SHARING_UNSET;
    unset.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 400.0f);
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&unset);
    assert(result.known);
    assert(result.reason == GENERATOR_FLEET_OK);
    /* Exactly the numbers test_one_engine() pins down. */
    assert(near(result.minimum_loading_kw, 150.0f));
    assert(near(result.required_generator_kw, 165.0f));
    assert(near(result.safe_pv_kw, 235.0f));
    /* It is reported as what it was computed as, not as "unset". */
    assert(result.sharing_mode == GENERATOR_SHARING_ISOCHRONOUS);
    expect_floor_matches_brute_force("one engine, no mode stated", &unset);

    /* Identical to the same engine with the mode stated: the exemption changes
     * nothing except whether the value had to be supplied. */
    generator_fleet_input_t stated = unset;
    stated.sharing_mode = GENERATOR_SHARING_ISOCHRONOUS;
    const generator_fleet_limit_t explicit_mode = generator_fleet_limit_evaluate(&stated);
    /* Field by field with exact equality, not memcmp: the struct carries alignment
     * padding whose contents are not defined, and not near(), because "bit-for-bit"
     * is the claim being made. */
    assert(result.known == explicit_mode.known);
    assert(result.online_count == explicit_mode.online_count);
    assert(result.online_rated_kw == explicit_mode.online_rated_kw);
    assert(result.minimum_loading_kw == explicit_mode.minimum_loading_kw);
    assert(result.sharing_mode == explicit_mode.sharing_mode);
    assert(result.base_loaded_count == explicit_mode.base_loaded_count);
    assert(result.base_load_total_kw == explicit_mode.base_load_total_kw);
    assert(result.required_generator_kw == explicit_mode.required_generator_kw);
    assert(result.safe_pv_kw == explicit_mode.safe_pv_kw);
    assert(result.reason == explicit_mode.reason);

    /* The legacy unmetered single-generator site, likewise. */
    generator_fleet_input_t legacy = fleet(400.0f);
    legacy.sharing_mode = GENERATOR_SHARING_UNSET;
    legacy.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 0.0f);
    legacy.engines[0].metered = false;
    legacy.engines[0].sample_fresh = false;
    legacy.allow_unmetered_single_engine = true;
    const generator_fleet_limit_t legacy_result = generator_fleet_limit_evaluate(&legacy);
    assert(legacy_result.known);
    assert(near(legacy_result.safe_pv_kw, 400.0f - 165.0f));
}

/* DROOP IS REFUSED, and so is any mode value this build does not recognise.
 *
 * Refusal is the conservative answer, not a missing feature: under an unknown
 * sharing law an engine may receive an arbitrarily small share of the load, which
 * makes the floor unbounded and the permitted PV zero -- exactly what refusing
 * produces. Droop is refused even with a single engine online, because it is an
 * affirmative statement that the plant's law is unmodelled and which engines are on
 * the bus is a runtime fact that will change within the hour. */
static void test_droop_and_unknown_modes_are_refused(void)
{
    const uint8_t refused[] = {GENERATOR_SHARING_DROOP, GENERATOR_SHARING_MODE_COUNT, 200U};
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        for (uint8_t engines = 1U; engines <= 2U; ++engines) {
            generator_fleet_input_t input = fleet(1000.0f);
            input.sharing_mode = refused[i];
            input.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
            if (engines == 2U) {
                input.engines[1] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
            }
            const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
            assert(!result.known);
            assert(result.reason == GENERATOR_FLEET_SHARING_MODE_UNSUPPORTED);
            assert(near(result.safe_pv_kw, 0.0f));
        }
    }
    assert(!generator_sharing_mode_supported(GENERATOR_SHARING_DROOP));
    assert(!generator_sharing_mode_supported(GENERATOR_SHARING_UNSET));
    assert(generator_sharing_mode_supported(GENERATOR_SHARING_ISOCHRONOUS));
    assert(generator_sharing_mode_supported(GENERATOR_SHARING_BASE_LOAD));
}

/* A MODE MISSING A VALUE IT REQUIRES. Base-load sharing needs a role for every
 * online engine and a setpoint for every base-loaded one; each hole fails closed. */
static void test_base_load_missing_values_fail_closed(void)
{
    /* An online engine with no declared role. Guessing is unavailable: calling it
     * swing or base-loaded moves the floor in either direction depending on the
     * setpoints, so one of the two guesses is permissive. */
    generator_fleet_input_t no_role = base_load_fleet(900.0f);
    no_role.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    no_role.engines[1] = metered(300.0f, 35.0f, 0.0f, 0.0f, 600.0f); /* role UNSET */
    generator_fleet_limit_t result = generator_fleet_limit_evaluate(&no_role);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));

    /* An unrecognised role value is the same hole. */
    generator_fleet_input_t bad_role = no_role;
    bad_role.engines[1].role = 200U;
    result = generator_fleet_limit_evaluate(&bad_role);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_UNKNOWN);

    /* A base-loaded engine with no usable setpoint, including one above its rating. */
    const float unusable[] = {0.0f, -1.0f, NAN, INFINITY, 501.0f};
    for (size_t i = 0; i < sizeof(unusable) / sizeof(unusable[0]); ++i) {
        generator_fleet_input_t input = base_load_fleet(900.0f);
        input.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, unusable[i]);
        input.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
        result = generator_fleet_limit_evaluate(&input);
        assert(!result.known);
        assert(result.reason == GENERATOR_FLEET_BASE_LOAD_UNKNOWN);
        assert(near(result.safe_pv_kw, 0.0f));
    }

    /* Every online engine pinned at a fixed kW: nothing absorbs the swing, so there
     * is no total plant load for the controller to shape. The brute force agrees --
     * no total satisfies the fleet, because a swing-less bus is not describable. */
    generator_fleet_input_t no_swing = base_load_fleet(900.0f);
    no_swing.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    no_swing.engines[1] = base_loaded(300.0f, 35.0f, 0.0f, 0.0f, 200.0f, 200.0f);
    result = generator_fleet_limit_evaluate(&no_swing);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_NO_SWING_ENGINE);
    assert(near(result.safe_pv_kw, 0.0f));
    printf("base-load holes:\n");
    expect_floor_matches_brute_force("every online engine base-loaded", &no_swing);

    /* A single online engine declared base-loaded is the same refusal: with nothing
     * else on the bus, a governor holding fixed kW leaves the plant's swing nowhere
     * to go. */
    generator_fleet_input_t alone = base_load_fleet(900.0f);
    alone.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    result = generator_fleet_limit_evaluate(&alone);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_NO_SWING_ENGINE);
}

/* A BASE-LOADED ENGINE HELD BELOW ITS OWN MINIMUM IS A COMMISSIONING FAULT.
 *
 * Its load does not follow the plant total, so no amount of PV curtailment ever
 * brings it up to its minimum. The brute force confirms this independently: there is
 * NO total generator load at which every engine is satisfied. A module that reported
 * a floor here would be computing around a plant that has been set up to wet-stack
 * one of its engines. */
static void test_base_load_below_own_minimum_is_refused(void)
{
    printf("base-load commissioning fault:\n");
    generator_fleet_input_t input = base_load_fleet(900.0f);
    /* 500 kW at 30 % needs 150 kW; the setpoint is 149 kW. */
    input.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 149.0f, 149.0f);
    input.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
    assert(!isfinite(brute_force_floor_kw(&input)));
    expect_floor_matches_brute_force("base setpoint 149 kW under a 150 kW minimum", &input);
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_BELOW_MINIMUM);

    /* Exactly at its own minimum is acceptable, and the floor is then the setpoint
     * plus the swing engine's own requirement. The measurement moves with the setpoint:
     * leaving it at 149 kW would be a governor holding 149 while commissioned for 150,
     * which is a different finding and is tested on its own below. */
    input.engines[0].base_load_kw = 150.0f;
    input.engines[0].measured_kw = 150.0f;
    expect_floor_matches_brute_force("base setpoint exactly at the 150 kW minimum", &input);
    const generator_fleet_limit_t at_minimum = generator_fleet_limit_evaluate(&input);
    assert(at_minimum.known);
    assert(near(at_minimum.minimum_loading_kw, 150.0f + 300.0f * 0.35f));
}

/* Base-load sharing with no base-loaded engine is isochronous sharing, and the two
 * modes must agree exactly on such a fleet -- the base-load expression reduces to
 * the isochronous one when the setpoint sum is zero. */
static void test_base_load_without_a_base_loaded_engine_equals_isochronous(void)
{
    generator_fleet_input_t iso = fleet(1000.0f);
    iso.engines[0] = metered(500.0f, 40.0f, 3.0f, 1.0f, 500.0f);
    iso.engines[1] = metered(300.0f, 20.0f, 3.0f, 1.0f, 500.0f);
    const generator_fleet_limit_t iso_result = generator_fleet_limit_evaluate(&iso);

    generator_fleet_input_t all_swing = iso;
    all_swing.sharing_mode = GENERATOR_SHARING_BASE_LOAD;
    all_swing.engines[0].role = GENERATOR_ENGINE_ROLE_SWING;
    all_swing.engines[1].role = GENERATOR_ENGINE_ROLE_SWING;
    const generator_fleet_limit_t swing_result = generator_fleet_limit_evaluate(&all_swing);

    assert(iso_result.known && swing_result.known);
    assert(near(iso_result.minimum_loading_kw, swing_result.minimum_loading_kw));
    assert(near(iso_result.required_generator_kw, swing_result.required_generator_kw));
    assert(near(iso_result.safe_pv_kw, swing_result.safe_pv_kw));
    assert(swing_result.base_loaded_count == 0U);
    assert(near(swing_result.base_load_total_kw, 0.0f));
}

/* WHY NO MODE MAY BE DEFAULTED, demonstrated rather than asserted in a comment.
 *
 * The base-load floor is not ordered against the isochronous floor: a large setpoint
 * raises it above, and moving a big engine out of the proportional term drops it
 * below. So there is no "conservative mode" to default to -- only a commissioned
 * fact, or no PV. */
static void test_neither_mode_is_conservative_for_every_plant(void)
{
    printf("neither mode dominates:\n");

    /* Base-load ABOVE isochronous: a big setpoint on top of the swing floor. */
    generator_fleet_input_t higher = fleet(2000.0f);
    higher.engines[0] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    higher.engines[1] = metered(500.0f, 30.0f, 0.0f, 0.0f, 500.0f);
    const float iso_higher = generator_fleet_limit_evaluate(&higher).minimum_loading_kw;
    generator_fleet_input_t higher_base = higher;
    higher_base.sharing_mode = GENERATOR_SHARING_BASE_LOAD;
    higher_base.base_load_tolerance_kw = TEST_TOLERANCE_KW;
    higher_base.engines[0].role = GENERATOR_ENGINE_ROLE_BASE_LOAD;
    higher_base.engines[0].base_load_kw = 450.0f;
    /* The engine is holding what it was commissioned to hold; this test is about which
     * mode gives the higher floor, not about drift. */
    higher_base.engines[0].measured_kw = 450.0f;
    higher_base.engines[1].role = GENERATOR_ENGINE_ROLE_SWING;
    const float base_higher = generator_fleet_limit_evaluate(&higher_base).minimum_loading_kw;
    printf("  isochronous %8.3f kW vs base-load %8.3f kW (base-load higher)\n",
           (double)iso_higher, (double)base_higher);
    assert(base_higher > iso_higher + CLOSE_ENOUGH);
    expect_floor_matches_brute_force("base-load-higher fleet", &higher_base);

    /* Base-load BELOW isochronous: the engine carrying the worst percentage is the
     * one taken out of the proportional term. */
    generator_fleet_input_t lower = fleet(2000.0f);
    lower.engines[0] = metered(600.0f, 50.0f, 0.0f, 0.0f, 500.0f);
    lower.engines[1] = metered(200.0f, 10.0f, 0.0f, 0.0f, 500.0f);
    const float iso_lower = generator_fleet_limit_evaluate(&lower).minimum_loading_kw;
    generator_fleet_input_t lower_base = lower;
    lower_base.sharing_mode = GENERATOR_SHARING_BASE_LOAD;
    lower_base.base_load_tolerance_kw = TEST_TOLERANCE_KW;
    lower_base.engines[0].role = GENERATOR_ENGINE_ROLE_BASE_LOAD;
    lower_base.engines[0].base_load_kw = 300.0f; /* its own minimum is 300 kW */
    lower_base.engines[0].measured_kw = 300.0f;  /* and it is holding it */
    lower_base.engines[1].role = GENERATOR_ENGINE_ROLE_SWING;
    const float base_lower = generator_fleet_limit_evaluate(&lower_base).minimum_loading_kw;
    printf("  isochronous %8.3f kW vs base-load %8.3f kW (base-load lower)\n",
           (double)iso_lower, (double)base_lower);
    assert(base_lower < iso_lower - CLOSE_ENOUGH);
    expect_floor_matches_brute_force("base-load-lower fleet", &lower_base);
}

/* ------------------------------------------- base-load setpoint agreement (drift) */

/* THE BAND RULE, EXECUTED RATHER THAN DESCRIBED.
 *
 * A negative return means "no tolerance commissioned", which is deliberately
 * distinguishable from a commissioned band of zero -- and a zero band is itself refused,
 * because a zero band on a physical measurement is not a tolerance, it is a bug. */
static void test_tolerance_band_rule(void)
{
    /* Nothing commissioned. */
    assert(generator_base_load_tolerance_band_kw(0.0f, 0.0f, 500.0f) < 0.0f);
    /* Values that are not tolerances read as nothing commissioned, never as a band. */
    const float rubbish[] = {-1.0f, NAN, INFINITY, -INFINITY, 0.0f};
    for (size_t i = 0; i < sizeof(rubbish) / sizeof(rubbish[0]); ++i) {
        assert(generator_base_load_tolerance_band_kw(rubbish[i], 0.0f, 500.0f) < 0.0f);
        assert(generator_base_load_tolerance_band_kw(0.0f, rubbish[i], 500.0f) < 0.0f);
    }
    /* A percentage above the whole rating is not a tolerance either. */
    assert(generator_base_load_tolerance_band_kw(0.0f, 101.0f, 500.0f) < 0.0f);

    /* Either alone. */
    assert(near(generator_base_load_tolerance_band_kw(7.0f, 0.0f, 500.0f), 7.0f));
    assert(near(generator_base_load_tolerance_band_kw(0.0f, 2.0f, 500.0f), 10.0f));
    /* The percentage is of THAT engine's rating, so it scales with the machine. */
    assert(near(generator_base_load_tolerance_band_kw(0.0f, 2.0f, 200.0f), 4.0f));
    /* No usable rating leaves the percentage unusable, and the kW figure still stands. */
    assert(generator_base_load_tolerance_band_kw(0.0f, 2.0f, 0.0f) < 0.0f);
    assert(near(generator_base_load_tolerance_band_kw(7.0f, 2.0f, NAN), 7.0f));

    /* BOTH: the NARROWER wins, either way round. This is the opposite of
     * inverter_write_confirmation.c's "wider" rule, and the reason is structural: that
     * band appears on both sides of its test, so widening it also demands a larger
     * pre-command excursion. This band appears on one side of one comparison, so
     * widening it is purely permissive. */
    assert(near(generator_base_load_tolerance_band_kw(7.0f, 2.0f, 500.0f), 7.0f));
    assert(near(generator_base_load_tolerance_band_kw(20.0f, 2.0f, 500.0f), 10.0f));
    printf("tolerance band: kW 7 with 2 %% of 500 kW -> %.3f kW (the narrower)\n",
           (double)generator_base_load_tolerance_band_kw(7.0f, 2.0f, 500.0f));
}

/* NO TOLERANCE COMMISSIONED. The plant is otherwise perfectly described and the engine
 * is measured holding its setpoint EXACTLY -- and it still fails closed, because with no
 * tolerance there is no proposition to test and the floor would rest on an assumption
 * nothing can check. This is the decision the change turns on: an unavailable check on a
 * running base-load plant fails permissively, so base-load sharing is refused until the
 * number exists. Exact agreement is used on purpose: it shows the refusal is about the
 * missing tolerance and not about the measurement. */
static void test_base_load_tolerance_unset_fails_closed(void)
{
    generator_fleet_input_t input = base_load_fleet_untoleranced(900.0f);
    input.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    input.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);

    generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);
    assert(near(result.safe_pv_kw, 0.0f));
    assert(near(result.minimum_loading_kw, 0.0f));
    assert(result.sharing_mode == GENERATOR_SHARING_UNSET);
    /* The reference agrees for its own reasons: the premise the base-load floor rests on
     * is not supported, so no total generator load satisfies the definition. */
    printf("base-load setpoint agreement:\n");
    expect_floor_matches_brute_force("no tolerance commissioned", &input);

    /* A stored value that is not a tolerance is not a tolerance. Each of these is the
     * uncommissioned state, never a band. */
    const float rubbish[] = {-1.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(rubbish) / sizeof(rubbish[0]); ++i) {
        generator_fleet_input_t absolute = input;
        absolute.base_load_tolerance_kw = rubbish[i];
        result = generator_fleet_limit_evaluate(&absolute);
        assert(!result.known);
        assert(result.reason == GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);

        generator_fleet_input_t percent = input;
        percent.base_load_tolerance_percent_of_rating = rubbish[i];
        result = generator_fleet_limit_evaluate(&percent);
        assert(!result.known);
        assert(result.reason == GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);
    }
    /* A percentage above the whole rating is refused rather than clamped to 100 %. */
    generator_fleet_input_t over = input;
    over.base_load_tolerance_percent_of_rating = 150.0f;
    result = generator_fleet_limit_evaluate(&over);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);

    /* The refusal is reported AFTER a broken setpoint, so an engineer is told the most
     * specific true thing rather than being sent after the tolerance first. */
    generator_fleet_input_t bad_setpoint = input;
    bad_setpoint.engines[0].base_load_kw = 0.0f;
    assert(generator_fleet_limit_evaluate(&bad_setpoint).reason ==
           GENERATOR_FLEET_BASE_LOAD_UNKNOWN);
    generator_fleet_input_t under_minimum = input;
    under_minimum.engines[0].base_load_kw = 149.0f; /* its own minimum is 150 kW */
    under_minimum.engines[0].measured_kw = 149.0f;
    assert(generator_fleet_limit_evaluate(&under_minimum).reason ==
           GENERATOR_FLEET_BASE_LOAD_BELOW_MINIMUM);
}

/* A TOLERANCE IS COMMISSIONED AND THE ENGINE AGREES. The floor is computed exactly as it
 * was, so commissioning the tolerance costs a compliant plant nothing. */
static void test_base_load_agreement_inside_the_band(void)
{
    /* Right at the edge of the band, on both sides. The comparison is inclusive: the
     * band is what an engineer said is normal, so its boundary is normal. */
    const float offsets[] = {0.0f, TEST_TOLERANCE_KW, -TEST_TOLERANCE_KW,
                             TEST_TOLERANCE_KW / 2.0f, -TEST_TOLERANCE_KW / 2.0f};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        generator_fleet_input_t input = base_load_fleet(900.0f);
        input.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f + offsets[i], 300.0f);
        input.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
        const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&input);
        assert(result.known);
        assert(result.reason == GENERATOR_FLEET_OK);
        /* The floor uses the COMMISSIONED setpoint, not the measurement. The measurement
         * is evidence that the setpoint is being held; it is not a substitute for it,
         * and letting a drifting-but-tolerable reading move the floor would make the
         * floor follow the very quantity it is meant to police. */
        assert(near(result.base_load_total_kw, 300.0f));
        assert(near(result.minimum_loading_kw, 300.0f + 300.0f * 0.35f));
        assert(result.base_loaded_count == 1U);
    }

    /* Percent-of-rating alone commissions the check just as well: 2 % of 500 kW is a
     * 10 kW band, so an 8 kW disagreement is inside it. */
    generator_fleet_input_t by_percent = base_load_fleet_untoleranced(900.0f);
    by_percent.base_load_tolerance_percent_of_rating = 2.0f;
    by_percent.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 308.0f, 300.0f);
    by_percent.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
    const generator_fleet_limit_t percent_result = generator_fleet_limit_evaluate(&by_percent);
    assert(percent_result.known);
    expect_floor_matches_brute_force("8 kW inside a 2 %-of-500 kW band", &by_percent);

    /* BOTH STATED: the NARROWER binds. The same 8 kW disagreement sits inside the 10 kW
     * percentage band above but outside a 5 kW absolute band, and the fleet is refused.
     * Taking the wider band would have called this agreement. */
    generator_fleet_input_t both = by_percent;
    both.base_load_tolerance_kw = TEST_TOLERANCE_KW;
    const generator_fleet_limit_t both_result = generator_fleet_limit_evaluate(&both);
    assert(!both_result.known);
    assert(both_result.reason == GENERATOR_FLEET_BASE_LOAD_DRIFT);
    assert(near(both_result.safe_pv_kw, 0.0f));
    expect_floor_matches_brute_force("8 kW outside the narrower of two bands", &both);
}

/* DISAGREEMENT BEYOND THE BAND. The engine is not holding kW, so the floor's first term
 * is not supported and the whole evaluation fails closed with its own reason. */
static void test_base_load_drift_beyond_the_band_fails_closed(void)
{
    /* Below the setpoint. This is the permissive direction and the one that matters
     * most: the controller would credit the base engine with 300 kW while it carries
     * 250 kW, and permit 50 kW more PV than the plant can lose. */
    generator_fleet_input_t low = base_load_fleet(900.0f);
    low.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 250.0f, 300.0f);
    low.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 650.0f);
    generator_fleet_limit_t result = generator_fleet_limit_evaluate(&low);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_DRIFT);
    assert(near(result.safe_pv_kw, 0.0f));
    assert(near(result.minimum_loading_kw, 0.0f));
    assert(near(result.base_load_total_kw, 0.0f));
    assert(result.base_loaded_count == 0U);
    assert(result.online_count == 0U);
    expect_floor_matches_brute_force("measured 250 kW against a 300 kW setpoint", &low);

    /* Above the setpoint is equally not agreement: a governor sitting above the kW it
     * was told to hold is a governor that is not holding kW. */
    generator_fleet_input_t high = base_load_fleet(900.0f);
    high.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 360.0f, 300.0f);
    high.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 540.0f);
    result = generator_fleet_limit_evaluate(&high);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_DRIFT);
    expect_floor_matches_brute_force("measured 360 kW against a 300 kW setpoint", &high);

    /*
     * IT MUST NOT RECLASSIFY THE ENGINE AS SWING AND CARRY ON.
     *
     * That would be the tempting repair, and it is unavailable: the module knows the
     * governor is not doing what it was commissioned to do and knows nothing whatever
     * about what it is doing instead. Droop, isochronous and manual are three different
     * sharing laws with three different floors. Here is the floor a swing
     * reclassification would have produced, computed by actually asking for it -- and
     * the drifting fleet must NOT report it.
     */
    generator_fleet_input_t reclassified = low;
    reclassified.engines[0].role = GENERATOR_ENGINE_ROLE_SWING;
    const generator_fleet_limit_t as_swing = generator_fleet_limit_evaluate(&reclassified);
    assert(as_swing.known);
    printf("  a swing reclassification would have permitted %.3f kW of PV; "
           "the drifting fleet permits %.3f kW\n",
           (double)as_swing.safe_pv_kw, (double)result.safe_pv_kw);
    assert(as_swing.safe_pv_kw > 0.0f);
    const generator_fleet_limit_t refused = generator_fleet_limit_evaluate(&low);
    assert(!refused.known);
    assert(refused.safe_pv_kw < as_swing.safe_pv_kw);
    assert(near(refused.safe_pv_kw, 0.0f));
}

/* A STALE OR MISSING MEASUREMENT MUST NOT READ AS AGREEMENT.
 *
 * Unknown is not confirmation. That error has been made elsewhere in this product and
 * cost real work to find, so each shape of "no usable observation" is pinned separately.
 * Note the measured value in every case below is EXACTLY the setpoint: if staleness were
 * ignored the fleet would sail through, which is precisely the bug being excluded.
 *
 * The brute-force reference is deliberately NOT used here. Its engine_is_online() drops
 * an unmeasured engine from the fleet and computes a floor over what remains, which is
 * the one thing the module must never do -- dropping an engine shrinks the denominator
 * and permits more PV. The reference exists to check the floor's arithmetic, not the
 * running-set rules, and those are asserted directly, as the pre-existing running-set
 * tests in this file already do. */
static void test_base_load_stale_or_missing_measurement_is_not_agreement(void)
{
    /* STALE. A metered engine whose sample is outside the freshness window says nothing
     * about its engine at all -- not whether it is on the bus, and not what it is
     * carrying -- so the running set is refused before the setpoint is ever compared.
     * That is the stronger refusal of the two and it is reported. */
    generator_fleet_input_t stale = base_load_fleet(900.0f);
    stale.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    stale.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
    stale.engines[0].sample_fresh = false;
    generator_fleet_limit_t result = generator_fleet_limit_evaluate(&stale);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));

    /* NON-FINITE. A decoded sample that is not a number is the same absence. */
    generator_fleet_input_t not_a_number = base_load_fleet(900.0f);
    not_a_number.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, NAN, 300.0f);
    not_a_number.engines[1] = swing(300.0f, 35.0f, 0.0f, 0.0f, 600.0f);
    result = generator_fleet_limit_evaluate(&not_a_number);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_RUNNING_SET_UNKNOWN);
    assert(near(result.safe_pv_kw, 0.0f));

    /* MISSING ENTIRELY. The one path that counts an engine online without measuring it
     * is the legacy unmetered single-engine allowance. A base-loaded engine reached that
     * way has a setpoint and no observation whatever, and it is refused with the reason
     * that says so -- not treated as agreeing, and not silently allowed through to the
     * no-swing-engine verdict it would otherwise reach. */
    generator_fleet_input_t unmetered = base_load_fleet(900.0f);
    unmetered.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 300.0f, 300.0f);
    unmetered.engines[0].metered = false;
    unmetered.engines[0].sample_fresh = false;
    unmetered.engines[0].measured_kw = 0.0f;
    unmetered.allow_unmetered_single_engine = true;
    result = generator_fleet_limit_evaluate(&unmetered);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_UNMEASURED);
    assert(near(result.safe_pv_kw, 0.0f));

    /* And with no tolerance commissioned the tolerance is still reported first: an
     * engineer who has supplied nothing needs to be told that, not sent to look at a
     * meter. */
    generator_fleet_input_t neither = unmetered;
    neither.base_load_tolerance_kw = 0.0f;
    neither.base_load_tolerance_percent_of_rating = 0.0f;
    assert(generator_fleet_limit_evaluate(&neither).reason ==
           GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);
}

/* DISAGREEMENT ON ONE OF SEVERAL BASE-LOADED ENGINES. One drifting governor out of two
 * refuses the whole fleet: the floor is a single number computed from the sum of the
 * setpoints, so one unsupported term makes the sum unsupported. Excluding the drifting
 * engine and carrying on would shrink the floor and permit MORE PV, which is the
 * direction that damages the machines. */
static void test_base_load_drift_on_one_of_several_base_engines(void)
{
    /* The healthy fleet first, so the difference is attributable to the drift alone. */
    generator_fleet_input_t healthy = base_load_fleet(1400.0f);
    healthy.engines[0] = base_loaded(500.0f, 30.0f, 0.0f, 0.0f, 400.0f, 400.0f);
    healthy.engines[1] = base_loaded(400.0f, 25.0f, 0.0f, 0.0f, 250.0f, 250.0f);
    healthy.engines[2] = swing(300.0f, 40.0f, 0.0f, 0.0f, 750.0f);
    const generator_fleet_limit_t healthy_result = generator_fleet_limit_evaluate(&healthy);
    assert(healthy_result.known);
    assert(healthy_result.base_loaded_count == 2U);
    assert(near(healthy_result.base_load_total_kw, 650.0f));

    /* Now drift the SECOND base-loaded engine only, by 40 kW. */
    generator_fleet_input_t drifting = healthy;
    drifting.engines[1].measured_kw = 210.0f;
    const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&drifting);
    assert(!result.known);
    assert(result.reason == GENERATOR_FLEET_BASE_LOAD_DRIFT);
    assert(near(result.safe_pv_kw, 0.0f));
    assert(result.safe_pv_kw < healthy_result.safe_pv_kw);
    printf("  one drifting governor of two: healthy fleet permitted %.3f kW, "
           "drifting fleet permits %.3f kW\n",
           (double)healthy_result.safe_pv_kw, (double)result.safe_pv_kw);
    expect_floor_matches_brute_force("engine 1 of 2 base-loaded drifting", &drifting);

    /* The first engine drifting is the same verdict; the check is not order-dependent. */
    generator_fleet_input_t first = healthy;
    first.engines[0].measured_kw = 340.0f;
    assert(generator_fleet_limit_evaluate(&first).reason == GENERATOR_FLEET_BASE_LOAD_DRIFT);

    /* Both drifting: still one refusal, still zero PV. */
    generator_fleet_input_t both = healthy;
    both.engines[0].measured_kw = 340.0f;
    both.engines[1].measured_kw = 210.0f;
    const generator_fleet_limit_t both_result = generator_fleet_limit_evaluate(&both);
    assert(!both_result.known);
    assert(both_result.reason == GENERATOR_FLEET_BASE_LOAD_DRIFT);
    assert(near(both_result.safe_pv_kw, 0.0f));
}

/*
 * ISOCHRONOUS AND SINGLE-ENGINE RESULTS ARE BIT-FOR-BIT UNCHANGED.
 *
 * This is the promise the whole change is allowed to be judged on: no plant that is not
 * base-loaded may notice. Proved by evaluating the same fleet with the tolerance fields
 * at every value they could hold -- uncommissioned, absolute, percentage, both, and
 * outright rubbish -- and comparing field for field with exact equality against the
 * uncommissioned answer. If any isochronous or single-engine path read either field,
 * one of these would differ.
 *
 * Field by field rather than memcmp: the struct carries alignment padding whose contents
 * are not defined. Exact equality rather than near(): "bit-for-bit" is the claim.
 */
static void test_isochronous_and_single_engine_are_bit_for_bit_unchanged(void)
{
    const float absolute[] = {0.0f, 1.0f, 5.0f, 1000.0f, -3.0f, NAN, INFINITY};
    const float percent[] = {0.0f, 0.5f, 2.0f, 100.0f, 150.0f, -1.0f, NAN};

    /* Every isochronous and single-engine shape this file tests elsewhere. */
    for (int shape = 0; shape < 5; ++shape) {
        generator_fleet_input_t base;
        memset(&base, 0, sizeof(base));
        switch (shape) {
        case 0: /* one metered engine */
            base = fleet(400.0f);
            base.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 400.0f);
            break;
        case 1: /* two engines, unequal minimum loadings */
            base = fleet(1000.0f);
            base.engines[0] = metered(500.0f, 40.0f, 3.0f, 1.0f, 500.0f);
            base.engines[1] = metered(500.0f, 20.0f, 3.0f, 1.0f, 500.0f);
            break;
        case 2: /* three engines */
            base = fleet(1200.0f);
            base.engines[0] = metered(400.0f, 25.0f, 5.0f, 2.0f, 400.0f);
            base.engines[1] = metered(300.0f, 45.0f, 5.0f, 2.0f, 400.0f);
            base.engines[2] = metered(250.0f, 15.0f, 5.0f, 2.0f, 400.0f);
            break;
        case 3: /* the legacy unmetered single-generator site */
            base = fleet(400.0f);
            base.sharing_mode = GENERATOR_SHARING_UNSET;
            base.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 0.0f);
            base.engines[0].metered = false;
            base.engines[0].sample_fresh = false;
            base.allow_unmetered_single_engine = true;
            break;
        default: /* one engine with no sharing mode stated: the exemption */
            base = fleet(400.0f);
            base.sharing_mode = GENERATOR_SHARING_UNSET;
            base.engines[0] = metered(500.0f, 30.0f, 10.0f, 5.0f, 400.0f);
            break;
        }

        const generator_fleet_limit_t reference = generator_fleet_limit_evaluate(&base);
        assert(reference.known);
        assert(reference.reason == GENERATOR_FLEET_OK);

        for (size_t a = 0; a < sizeof(absolute) / sizeof(absolute[0]); ++a) {
            for (size_t p = 0; p < sizeof(percent) / sizeof(percent[0]); ++p) {
                generator_fleet_input_t variant = base;
                variant.base_load_tolerance_kw = absolute[a];
                variant.base_load_tolerance_percent_of_rating = percent[p];
                const generator_fleet_limit_t result = generator_fleet_limit_evaluate(&variant);
                assert(result.known == reference.known);
                assert(result.online_count == reference.online_count);
                assert(result.online_rated_kw == reference.online_rated_kw);
                assert(result.minimum_loading_kw == reference.minimum_loading_kw);
                assert(result.sharing_mode == reference.sharing_mode);
                assert(result.base_loaded_count == reference.base_loaded_count);
                assert(result.base_load_total_kw == reference.base_load_total_kw);
                assert(result.required_generator_kw == reference.required_generator_kw);
                assert(result.safe_pv_kw == reference.safe_pv_kw);
                assert(result.reason == reference.reason);
            }
        }
    }

    /* And base-load sharing over an ALL-SWING fleet needs no tolerance either, because
     * there is no setpoint to check. That is what keeps the two modes identical on a
     * plant with no base-loaded machine, which is asserted separately above; here the
     * point is that the tolerance requirement lands only where it has a referent. */
    generator_fleet_input_t all_swing = base_load_fleet_untoleranced(1000.0f);
    all_swing.engines[0] = swing(500.0f, 40.0f, 3.0f, 1.0f, 500.0f);
    all_swing.engines[1] = swing(300.0f, 20.0f, 3.0f, 1.0f, 500.0f);
    const generator_fleet_limit_t swing_result = generator_fleet_limit_evaluate(&all_swing);
    assert(swing_result.known);
    assert(swing_result.reason == GENERATOR_FLEET_OK);
    assert(swing_result.base_loaded_count == 0U);
}

static void test_mode_and_role_slugs_are_complete_and_bounded(void)
{
    for (uint8_t i = 0; i < GENERATOR_SHARING_MODE_COUNT; ++i) {
        assert(generator_sharing_mode_id(i)[0] != '\0');
    }
    for (uint8_t a = 0; a < GENERATOR_SHARING_MODE_COUNT; ++a) {
        for (uint8_t b = (uint8_t)(a + 1); b < GENERATOR_SHARING_MODE_COUNT; ++b) {
            assert(strcmp(generator_sharing_mode_id(a), generator_sharing_mode_id(b)) != 0);
        }
    }
    /* Out of range must read as "unset", never as a supported mode. */
    assert(strcmp(generator_sharing_mode_id(GENERATOR_SHARING_MODE_COUNT), "unset") == 0);
    assert(strcmp(generator_sharing_mode_id(200), "unset") == 0);

    for (uint8_t i = 0; i < GENERATOR_ENGINE_ROLE_COUNT; ++i) {
        assert(generator_engine_role_id(i)[0] != '\0');
    }
    for (uint8_t a = 0; a < GENERATOR_ENGINE_ROLE_COUNT; ++a) {
        for (uint8_t b = (uint8_t)(a + 1); b < GENERATOR_ENGINE_ROLE_COUNT; ++b) {
            assert(strcmp(generator_engine_role_id(a), generator_engine_role_id(b)) != 0);
        }
    }
    assert(strcmp(generator_engine_role_id(GENERATOR_ENGINE_ROLE_COUNT), "unset") == 0);
}

static void test_reason_slugs_are_complete_and_bounded(void)
{
    for (uint8_t i = 0; i < GENERATOR_FLEET_REASON_COUNT; ++i) {
        assert(generator_fleet_reason_id(i)[0] != '\0');
    }
    /* Out of range must not read out of bounds, and must not read as OK. */
    assert(strcmp(generator_fleet_reason_id(GENERATOR_FLEET_REASON_COUNT), "ok") != 0);
    assert(strcmp(generator_fleet_reason_id(200), "ok") != 0);
    for (uint8_t a = 0; a < GENERATOR_FLEET_REASON_COUNT; ++a) {
        for (uint8_t b = (uint8_t)(a + 1); b < GENERATOR_FLEET_REASON_COUNT; ++b) {
            assert(strcmp(generator_fleet_reason_id(a), generator_fleet_reason_id(b)) != 0);
        }
    }
}

int main(void)
{
    /* Unbuffered, so the brute-force comparison already printed is visible when a
     * later assertion aborts the process. */
    setvbuf(stdout, NULL, _IONBF, 0);
    test_null_and_zeroed_input_yield_no_pv();
    test_stale_or_impossible_load_yields_no_pv();
    test_one_engine();
    test_two_engines_double_the_floor();
    test_three_engines();
    test_worst_percentage_binds_not_the_per_engine_sum();
    test_equal_percentages_match_the_per_engine_sum();
    test_online_engine_without_a_rating_yields_no_pv();
    test_rating_with_no_engine_online_yields_no_pv();
    test_no_engine_configured_yields_no_pv();
    test_metered_but_unconfigured_slot_yields_no_pv();
    test_legacy_single_generator_is_unchanged();
    test_load_below_the_floor_clamps_to_zero();
    test_adding_an_engine_never_increases_permitted_pv();
    test_engine_count_is_bounded();
    test_isochronous_floor_matches_brute_force();
    test_base_load_floor_matches_brute_force();
    test_mixed_fleet_end_to_end();
    test_sharing_mode_unset_fails_closed();
    test_single_online_engine_needs_no_sharing_mode();
    test_droop_and_unknown_modes_are_refused();
    test_base_load_missing_values_fail_closed();
    test_base_load_below_own_minimum_is_refused();
    test_base_load_without_a_base_loaded_engine_equals_isochronous();
    test_neither_mode_is_conservative_for_every_plant();
    test_tolerance_band_rule();
    test_base_load_tolerance_unset_fails_closed();
    test_base_load_agreement_inside_the_band();
    test_base_load_drift_beyond_the_band_fails_closed();
    test_base_load_stale_or_missing_measurement_is_not_agreement();
    test_base_load_drift_on_one_of_several_base_engines();
    test_isochronous_and_single_engine_are_bit_for_bit_unchanged();
    test_mode_and_role_slugs_are_complete_and_bounded();
    test_reason_slugs_are_complete_and_bounded();
    printf("aggregate generator limit unit tests passed "
           "(every mode's floor agrees with a brute-force reference computed from "
           "first principles; droop is refused; a base-loaded engine's setpoint is "
           "believed only when a commissioned tolerance and a fresh measurement say "
           "so)\n");
    return 0;
}
