/*
 * The urgency ramp boost: how much faster PV must be reduced when the
 * generators are badly underloaded.
 *
 * Below its minimum loading a diesel generator wet-stacks and heads toward
 * reverse power. The only lever this controller has to raise generator load is
 * to LOWER PV, so the boost applies to the PV ramp-DOWN rate and to nothing
 * else. Applying it upward would raise PV faster while the machine is already
 * starved -- the exact opposite of the intent, and dangerous.
 *
 * The threshold sits BELOW the minimum-loading target (25 percent against a
 * default 30) deliberately: it is not the target, it is the point at which
 * reaching the target stops being routine and becomes urgent.
 *
 * Direction is not something a comment can enforce, so the wiring is asserted
 * in tests/control_arm_source_contract.py's sibling contract; what is executed
 * here is the rule itself.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "generator_fleet_limit.h"

/* The threshold and multiplier are commissioned per plant now, so they travel
 * with the call. These are the firmware defaults, which is what every existing
 * plant migrates to -- the behaviour these cases describe is unchanged. */
#define FRACTION GENERATOR_URGENT_LOADING_FRACTION
#define MULTIPLIER GENERATOR_URGENT_RAMP_MULTIPLIER

#define RATED_KW 500.0f
#define URGENT_BELOW_KW (RATED_KW * GENERATOR_URGENT_LOADING_FRACTION)   /* 125 kW */

static void test_boost_applies_below_the_threshold(void)
{
    assert(generator_urgent_ramp_multiplier(true, true, 0.0f, RATED_KW, FRACTION, MULTIPLIER) ==
           GENERATOR_URGENT_RAMP_MULTIPLIER);
    assert(generator_urgent_ramp_multiplier(true, true, 60.0f, RATED_KW, FRACTION, MULTIPLIER) ==
           GENERATOR_URGENT_RAMP_MULTIPLIER);
    /* One kW below the threshold is still urgent. */
    assert(generator_urgent_ramp_multiplier(true, true, URGENT_BELOW_KW - 1.0f, RATED_KW, FRACTION, MULTIPLIER) ==
           GENERATOR_URGENT_RAMP_MULTIPLIER);
}

static void test_normal_rate_at_and_above_the_threshold(void)
{
    /* AT the threshold the boost is already off: 25 percent is the point the
     * plant was hurrying towards, so arriving there ends the hurry. */
    assert(generator_urgent_ramp_multiplier(true, true, URGENT_BELOW_KW, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, URGENT_BELOW_KW + 1.0f, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
    /* Between the urgency threshold and the 30 percent minimum-loading target
     * the commissioned rate is enough. */
    assert(generator_urgent_ramp_multiplier(true, true, RATED_KW * 0.28f, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, RATED_KW * 0.90f, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
}

/* On the grid there is no minimum loading to protect, so there is nothing to
 * hurry for. */
static void test_no_boost_when_the_grid_is_carrying(void)
{
    assert(generator_urgent_ramp_multiplier(false, true, 0.0f, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
}

/*
 * Everything the module will not vouch for ramps at the COMMISSIONED rate, not
 * an inferred one. An unknown running set means the denominator is unknown, and
 * doubling a rate against an unknown rating is not caution, it is a guess.
 */
static void test_untrusted_inputs_fall_back_to_the_commissioned_rate(void)
{
    assert(generator_urgent_ramp_multiplier(true, false, 0.0f, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, 0.0f, 0.0f, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, 0.0f, -1.0f, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, NAN, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, INFINITY, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, 0.0f, NAN, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, -5.0f, RATED_KW, FRACTION, MULTIPLIER) == 1.0f);
}

/* The boost scales with the machine, not with a fixed kW figure. */
static void test_threshold_follows_the_rating(void)
{
    assert(generator_urgent_ramp_multiplier(true, true, 30.0f, 100.0f, FRACTION, MULTIPLIER) == 1.0f);
    assert(generator_urgent_ramp_multiplier(true, true, 30.0f, 200.0f, FRACTION, MULTIPLIER) ==
           GENERATOR_URGENT_RAMP_MULTIPLIER);
}

/* Doubling must never turn a stopped ramp into a moving one: zero times two is
 * still zero, so a plant with ramping disabled stays disabled. */
static void test_multiplier_cannot_create_movement_from_zero(void)
{
    const float stopped = 0.0f;
    assert(stopped * generator_urgent_ramp_multiplier(true, true, 0.0f, RATED_KW, FRACTION, MULTIPLIER) == 0.0f);
}

int main(void)
{
    test_boost_applies_below_the_threshold();
    test_normal_rate_at_and_above_the_threshold();
    test_no_boost_when_the_grid_is_carrying();
    test_untrusted_inputs_fall_back_to_the_commissioned_rate();
    test_threshold_follows_the_rating();
    test_multiplier_cannot_create_movement_from_zero();
    printf("generator urgent ramp tests passed\n");
    return 0;
}
