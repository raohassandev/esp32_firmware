/*
 * The generator PV cap must be a cap on ALLOWABLE PV, not on present headroom.
 *
 * source_mode_generator_safe_pv_kw() returns facility_load - required, and the
 * policy uses that value as an absolute maximum for the PV command. So the two
 * only agree if facility_load is the TOTAL plant load. The control engine used
 * to pass the source meter reading alone, which in generator mode is the
 * generator's own output -- the plant load minus PV.
 *
 * That error is invisible in a snapshot: every number on screen looks
 * reasonable. It shows up only when the loop is iterated, because the cap falls
 * as PV rises. This test iterates it.
 *
 *   correct:  cap = L - R                 (fixed point PV = L - R)
 *   defect:   cap = (L - PV) - R          (fixed point PV = (L - R) / 2)
 *
 * A plant would have run at half the solar it could safely carry.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "source_mode.h"

#define LOAD_KW 357.3f          /* total plant load, constant */
#define RATED_KW 500.0f
#define MIN_LOADING_PCT 30.0f   /* -> floor 150 kW */
#define FLEET_CAPACITY_KW 400.0f /* deliberately larger than the answer, so the
                                  * generator limit is what binds, not capacity */

static float cap_for(float facility_load_kw)
{
    generator_limit_input_t input = {
        .evidence_fresh = true,
        .facility_load_kw = facility_load_kw,
        .running_generator_rated_kw = RATED_KW,
        .minimum_loading_percent = MIN_LOADING_PCT,
        .reserve_kw = 0.0f,
        .reverse_power_margin_kw = 0.0f,
    };
    return source_mode_generator_safe_pv_kw(&input);
}

/*
 * Iterates "command the cap, observe the plant" until it settles.
 *
 * The step toward the cap is damped, because the real loop is: it ramps and it
 * integrates rather than jumping to the limit in one cycle. The damping matters
 * for what the defect LOOKS like, and both shapes are bad. Undamped, feeding
 * back the generator's own output gives pv_next = (L - R) - pv, a gain of -1:
 * the command oscillates between zero and full every cycle forever. Damped, it
 * converges -- to half the correct value. A real plant, which is damped, would
 * therefore have run quietly at half its usable solar rather than visibly
 * hunting, which is why this went unnoticed.
 */
#define DAMPING 0.2f

static float settle(int total_load_includes_pv)
{
    float pv = 0.0f;
    for (int cycle = 0; cycle < 4000; ++cycle) {
        const float meter_kw = LOAD_KW - pv;   /* what the source meter reads */
        const float presented = total_load_includes_pv ? meter_kw + pv : meter_kw;
        float cap = cap_for(presented);
        if (cap > FLEET_CAPACITY_KW) cap = FLEET_CAPACITY_KW;
        pv += DAMPING * (cap - pv);
    }
    return pv;
}

static void test_total_load_settles_at_the_full_answer(void)
{
    const float floor_kw = RATED_KW * MIN_LOADING_PCT / 100.0f;   /* 150 */
    const float expected = LOAD_KW - floor_kw;                    /* 207.3 */
    const float settled = settle(1);
    assert(fabsf(settled - expected) < 0.5f);

    /* And the generator is left exactly at its floor, not below it. */
    const float generator_kw = LOAD_KW - settled;
    assert(generator_kw >= floor_kw - 0.5f);
}

/* The defect, stated as a test so it cannot come back unnoticed: presenting the
 * generator's own output settles at half. */
static void test_generator_output_alone_settles_at_half(void)
{
    const float floor_kw = RATED_KW * MIN_LOADING_PCT / 100.0f;
    const float half = (LOAD_KW - floor_kw) / 2.0f;
    const float settled = settle(0);
    assert(fabsf(settled - half) < 0.5f);
    /* Safe, but wrong: the generator is left far above its floor and the site
     * throws away the difference. */
    assert(LOAD_KW - settled > floor_kw + 50.0f);
}

/* When the plant cannot carry the floor even with PV at zero, the cap is zero
 * rather than negative. */
static void test_underloaded_plant_yields_zero(void)
{
    assert(cap_for(100.0f) == 0.0f);   /* floor 150 > load 100 */
}

/* Reserve and reverse-power margin raise the floor, and the user-stated default
 * margin is 5 percent of rating. */
static void test_margin_raises_the_floor(void)
{
    generator_limit_input_t input = {
        .evidence_fresh = true,
        .facility_load_kw = LOAD_KW,
        .running_generator_rated_kw = RATED_KW,
        .minimum_loading_percent = MIN_LOADING_PCT,
        .reserve_kw = 0.0f,
        .reverse_power_margin_kw = RATED_KW * 0.05f,   /* 25 kW */
    };
    const float with_margin = source_mode_generator_safe_pv_kw(&input);
    input.reverse_power_margin_kw = 0.0f;
    const float without = source_mode_generator_safe_pv_kw(&input);
    assert(fabsf((without - with_margin) - 25.0f) < 0.01f);
}

static void test_stale_evidence_yields_zero(void)
{
    generator_limit_input_t input = {
        .evidence_fresh = false,
        .facility_load_kw = LOAD_KW,
        .running_generator_rated_kw = RATED_KW,
        .minimum_loading_percent = MIN_LOADING_PCT,
    };
    assert(source_mode_generator_safe_pv_kw(&input) == 0.0f);
}

int main(void)
{
    test_total_load_settles_at_the_full_answer();
    test_generator_output_alone_settles_at_half();
    test_underloaded_plant_yields_zero();
    test_margin_raises_the_floor();
    test_stale_evidence_yields_zero();
    printf("generator safe PV convergence tests passed\n");
    return 0;
}
