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

static generator_fleet_input_t fleet(float load_kw)
{
    generator_fleet_input_t input;
    memset(&input, 0, sizeof(input));
    input.evidence_fresh = true;
    input.facility_load_kw = load_kw;
    input.engine_count = GENERATOR_FLEET_MAX_ENGINES;
    return input;
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
    test_reason_slugs_are_complete_and_bounded();
    printf("aggregate generator limit unit tests passed\n");
    return 0;
}
