/* Host-compiled unit test for the EEMUA 191 alarm metrics: gaps A6 and A10 of
 * docs/ALARM_MANAGEMENT_RESEARCH.md.
 *
 * This EXECUTES the rules rather than pattern-matching the source, which matters
 * more here than usual for three reasons.
 *
 * The peak-rate claim is a mathematical one - that sampling the trailing count at
 * every record finds the true maximum over every possible window - and the only
 * honest way to check a claim like that is to compare it against a brute-force
 * reference on generated sequences, which is exactly what
 * test_peak_matches_brute_force does.
 *
 * The wrap cases cannot be provoked on hardware in a useful time. uptime
 * milliseconds roll over at about 49.7 days, so a source contract asserting that
 * a signed comparison is present is asserting the shape of the fix, not the fix.
 * Here the counter is actually driven across the boundary.
 *
 * And the priority distribution is the one place a "measurement" could be quietly
 * flattered. The controller's real census is asserted to FAIL the EEMUA target,
 * so an assignment change that made the numbers look better without being
 * justified would break this test rather than silently pass it.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "alarm_metrics.h"

/* ------------------------------------------------------------------ A10: rate */

static void test_empty_ring_measures_nothing(void)
{
    alarm_rate_t rate;
    alarm_rate_reset(&rate);
    assert(alarm_rate_window_count(&rate, 0, ALARM_RATE_WINDOW_MS) == 0);
    assert(alarm_rate_window_count(&rate, 5000000U, ALARM_RATE_DAY_MS) == 0);
    assert(rate.peak_per_window == 0);
    assert(rate.total == 0);
    assert(rate.discarded == 0);
    assert(!alarm_rate_window_truncated(&rate, 5000000U, ALARM_RATE_DAY_MS));
    /* No alarms at all is the healthiest possible steady state, and it must read
     * as meeting the target rather than as "no data". */
    assert(alarm_rate_meets_steady_target(
        alarm_rate_per_window_milli(0, ALARM_RATE_HOUR_MS, ALARM_RATE_WINDOW_MS)));
    assert(alarm_rate_meets_peak_target(0));
}

/* The window is half-open: a record exactly window_ms old has left it. Getting
 * this wrong by one would let a ten-minute count include eleven minutes of
 * alarms, which is the difference between passing and failing EEMUA. */
static void test_window_boundary_is_half_open(void)
{
    alarm_rate_t rate;
    alarm_rate_reset(&rate);
    alarm_rate_record(&rate, 1000U);
    assert(alarm_rate_window_count(&rate, 1000U, 600000U) == 1);          /* same instant */
    assert(alarm_rate_window_count(&rate, 1000U + 599999U, 600000U) == 1); /* 1 ms inside */
    assert(alarm_rate_window_count(&rate, 1000U + 600000U, 600000U) == 0); /* exactly out */
    assert(alarm_rate_window_count(&rate, 1000U + 600001U, 600000U) == 0);
    /* A timestamp in the future is not evidence of an alarm in this window. */
    assert(alarm_rate_window_count(&rate, 999U, 600000U) == 0);
    /* A zero-length window counts nothing rather than dividing by it. */
    assert(alarm_rate_window_count(&rate, 1000U, 0U) == 0);
}

static void test_counts_only_what_is_inside_each_window(void)
{
    alarm_rate_t rate;
    alarm_rate_reset(&rate);
    /* One alarm every five minutes for an hour: 12 in the hour, 2 in the last
     * ten minutes (the one at t and the one 5 min before it). */
    for (unsigned minute = 0; minute < 60U; minute += 5U) {
        alarm_rate_record(&rate, minute * 60000U);
    }
    const uint32_t now = 55U * 60000U;
    assert(rate.total == 12);
    assert(alarm_rate_window_count(&rate, now, ALARM_RATE_WINDOW_MS) == 2);
    assert(alarm_rate_window_count(&rate, now, ALARM_RATE_HOUR_MS) == 12);
    assert(alarm_rate_window_count(&rate, now, ALARM_RATE_DAY_MS) == 12);
}

/* EEMUA's steady-state figure is "fewer than 1 alarm per operator per 10
 * minutes". Fewer than, not at most: exactly one per ten minutes is a miss. */
static void test_steady_target_is_strict(void)
{
    /* 6 alarms in an hour is exactly 1 per 10 min. */
    const uint32_t exactly_one =
        alarm_rate_per_window_milli(6, ALARM_RATE_HOUR_MS, ALARM_RATE_WINDOW_MS);
    assert(exactly_one == 1000U);
    assert(!alarm_rate_meets_steady_target(exactly_one));

    /* 5 in an hour is 0.833 per 10 min and passes. */
    const uint32_t under =
        alarm_rate_per_window_milli(5, ALARM_RATE_HOUR_MS, ALARM_RATE_WINDOW_MS);
    assert(under == 833U);
    assert(alarm_rate_meets_steady_target(under));

    /* EEMUA's ~150 alarms/day is the same target expressed differently, so it
     * must land within a rounding step of the limit rather than somewhere else. */
    const uint32_t daily =
        alarm_rate_per_window_milli(144, ALARM_RATE_DAY_MS, ALARM_RATE_WINDOW_MS);
    assert(daily == 1000U);
    assert(!alarm_rate_meets_steady_target(daily));
    assert(alarm_rate_meets_steady_target(
        alarm_rate_per_window_milli(143, ALARM_RATE_DAY_MS, ALARM_RATE_WINDOW_MS)));
}

static void test_peak_target_is_ten_inclusive(void)
{
    assert(alarm_rate_meets_peak_target(9));
    assert(alarm_rate_meets_peak_target(10));   /* "no more than 10" includes 10 */
    assert(!alarm_rate_meets_peak_target(11));
    assert(!alarm_rate_meets_peak_target(400));
}

/* The Texaco Milford Haven shape: one physical event, a burst of alarms. Twelve
 * in a minute must be caught as a breach of the ten-in-ten-minutes ceiling, and
 * must stay caught long after the burst has scrolled out of the current window. */
static void test_flood_is_recorded_as_a_peak_and_remembered(void)
{
    alarm_rate_t rate;
    alarm_rate_reset(&rate);
    for (unsigned i = 0; i < 12U; ++i) alarm_rate_record(&rate, 10000U + i * 5000U);
    assert(rate.peak_per_window == 12);
    assert(!alarm_rate_meets_peak_target(rate.peak_per_window));

    /* Two hours later the current window is empty, the average is comfortable,
     * and the peak must still report the flood. An alarm system that forgets its
     * worst ten minutes cannot be assessed against EEMUA at all. */
    const uint32_t later = 2U * ALARM_RATE_HOUR_MS;
    assert(alarm_rate_window_count(&rate, later, ALARM_RATE_WINDOW_MS) == 0);
    assert(rate.peak_per_window == 12);
    assert(rate.peak_at_ms == 10000U + 11U * 5000U);
}

/* The claim in alarm_metrics.c is that evaluating the trailing window only at the
 * instant of a record still finds the true maximum over every instant. Checked
 * against a brute-force scan over a millisecond grid rather than taken on trust. */
static uint16_t brute_force_peak(const uint32_t *stamps, unsigned count, uint32_t window,
                                 uint32_t horizon, uint32_t step)
{
    uint16_t worst = 0;
    for (uint32_t now = 0; now <= horizon; now += step) {
        uint16_t seen = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (stamps[i] <= now && (now - stamps[i]) < window) seen++;
        }
        if (seen > worst) worst = seen;
    }
    return worst;
}

static void test_peak_matches_brute_force(void)
{
    /* A crude deterministic generator: no rand(), so the case that fails is the
     * same case on every machine and in every CI run. */
    uint32_t seed = 20260730U;
    for (unsigned trial = 0; trial < 40U; ++trial) {
        uint32_t stamps[60];
        const unsigned count = 5U + (trial % 40U);
        uint32_t when = 0;
        for (unsigned i = 0; i < count; ++i) {
            seed = seed * 1103515245U + 12345U;
            /* Gaps from 0 to ~4 minutes, so windows genuinely overlap. */
            when += (seed >> 16) % 240000U;
            stamps[i] = when;
        }
        alarm_rate_t rate;
        alarm_rate_reset(&rate);
        for (unsigned i = 0; i < count; ++i) alarm_rate_record(&rate, stamps[i]);
        const uint16_t reference = brute_force_peak(stamps, count, ALARM_RATE_WINDOW_MS,
                                                    when + ALARM_RATE_WINDOW_MS, 1000U);
        assert(rate.peak_per_window == reference);
    }
}

/* uptime milliseconds wrap at ~49.7 days. A rate metric that reported zero for
 * the 49 days after a wrap would be worse than no metric, because it would read
 * as a perfectly quiet plant. */
static void test_counts_survive_the_uptime_wrap(void)
{
    alarm_rate_t rate;
    alarm_rate_reset(&rate);
    const uint32_t before_wrap = 0xFFFFFFFFU - 120000U;   /* two minutes to go */
    alarm_rate_record(&rate, before_wrap);
    alarm_rate_record(&rate, before_wrap + 60000U);
    /* Now past the wrap: one minute after zero. */
    const uint32_t after_wrap = 60000U;
    alarm_rate_record(&rate, after_wrap);
    assert(rate.total == 3);
    /* All three fall inside a ten-minute window that straddles the wrap. */
    assert(alarm_rate_window_count(&rate, after_wrap, ALARM_RATE_WINDOW_MS) == 3);
    assert(rate.peak_per_window == 3);
    /* An hour after the wrap they have all left the ten-minute window, and the
     * pre-wrap records must not have become "the future". */
    assert(alarm_rate_window_count(&rate, ALARM_RATE_HOUR_MS, ALARM_RATE_WINDOW_MS) == 0);
    /* Half an hour past the wrap all three are still inside an hour-long window,
     * measured across the boundary rather than lost at it. */
    assert(alarm_rate_window_count(&rate, ALARM_RATE_HOUR_MS / 2U, ALARM_RATE_HOUR_MS) == 3);
}

/* The ring is finite. When it overflows it must keep the newest records, keep
 * counting the total, and admit that a window's count is now a lower bound. */
static void test_overflow_keeps_newest_and_admits_the_loss(void)
{
    alarm_rate_t rate;
    alarm_rate_reset(&rate);
    const unsigned burst = ALARM_RATE_CAPACITY + 50U;
    for (unsigned i = 0; i < burst; ++i) alarm_rate_record(&rate, 1000U + i * 10U);
    const uint32_t last = 1000U + (burst - 1U) * 10U;
    assert(rate.total == burst);
    assert(rate.retained == ALARM_RATE_CAPACITY);
    assert(rate.discarded == 50U);
    /* Every surviving record is inside the window, so the count is capped by the
     * ring and the caller is told so rather than being handed a flattering
     * number. */
    assert(alarm_rate_window_count(&rate, last, ALARM_RATE_WINDOW_MS) == ALARM_RATE_CAPACITY);
    assert(alarm_rate_window_truncated(&rate, last, ALARM_RATE_WINDOW_MS));
    /* The truncation claim is about a specific window: once the surviving records
     * have aged out, the window is honestly empty and not "truncated". */
    assert(!alarm_rate_window_truncated(&rate, last + ALARM_RATE_WINDOW_MS,
                                        ALARM_RATE_WINDOW_MS));
    /* A breach this size must still be reported as a breach. */
    assert(!alarm_rate_meets_peak_target(rate.peak_per_window));

    /* An unsaturated ring never claims truncation. */
    alarm_rate_t small;
    alarm_rate_reset(&small);
    for (unsigned i = 0; i < 10U; ++i) alarm_rate_record(&small, 1000U + i * 10U);
    assert(!alarm_rate_window_truncated(&small, 2000U, ALARM_RATE_WINDOW_MS));
}

/* count x window x 1000 overflows uint32 by four orders of magnitude. A wrapped
 * rate would read as healthy, which is the worst possible failure here. */
static void test_rate_arithmetic_never_wraps(void)
{
    assert(alarm_rate_per_window_milli(65535U, 1U, ALARM_RATE_WINDOW_MS) == UINT32_MAX);
    assert(alarm_rate_per_window_milli(65535U, ALARM_RATE_DAY_MS, ALARM_RATE_WINDOW_MS)
           == 455104U);   /* ~455 alarms per 10 min: a catastrophic flood, not zero */
    /* Division by an unobserved window is refused rather than trapped. */
    assert(alarm_rate_per_window_milli(10U, 0U, ALARM_RATE_WINDOW_MS) == 0U);
    assert(alarm_rate_per_window_milli(10U, ALARM_RATE_HOUR_MS, 0U) == 0U);
}

/* A rate over a window longer than the controller has been running is an
 * extrapolation, and reporting it as a measurement would be a claim the device
 * cannot support. */
static void test_window_observed_gates_the_verdict(void)
{
    assert(!alarm_rate_window_observed(0U, ALARM_RATE_WINDOW_MS));
    assert(!alarm_rate_window_observed(ALARM_RATE_WINDOW_MS - 1U, ALARM_RATE_WINDOW_MS));
    assert(alarm_rate_window_observed(ALARM_RATE_WINDOW_MS, ALARM_RATE_WINDOW_MS));
    assert(!alarm_rate_window_observed(ALARM_RATE_HOUR_MS, ALARM_RATE_DAY_MS));
    assert(alarm_rate_window_observed(ALARM_RATE_DAY_MS, ALARM_RATE_DAY_MS));
}

static void test_null_arguments_are_survivable(void)
{
    alarm_rate_reset(NULL);
    alarm_rate_record(NULL, 1000U);
    assert(alarm_rate_window_count(NULL, 1000U, ALARM_RATE_WINDOW_MS) == 0);
    assert(!alarm_rate_window_truncated(NULL, 1000U, ALARM_RATE_WINDOW_MS));
    assert(alarm_priority_total(NULL) == 0);
    assert(!alarm_priority_meets_target(NULL));
}

/* ------------------------------------------------------ A6: the distribution */

static void test_percent_rounds_half_away_from_zero(void)
{
    assert(alarm_priority_percent(0, 0) == 0);      /* no population, no share */
    assert(alarm_priority_percent(5, 0) == 0);
    assert(alarm_priority_percent(2, 7) == 29);     /* 28.57 -> 29, not 28 */
    assert(alarm_priority_percent(3, 7) == 43);     /* 42.86 -> 43 */
    assert(alarm_priority_percent(1, 8) == 13);     /* 12.5  -> 13 */
    assert(alarm_priority_percent(2, 4) == 50);
    assert(alarm_priority_percent(4, 4) == 100);
    /* A part larger than the population is clamped rather than reported as
     * over 100%. */
    assert(alarm_priority_percent(9, 4) == 100);
}

static void test_target_is_unreachable_for_a_small_population(void)
{
    /* One condition out of four is 25%: the 5% high band does not exist. */
    assert(alarm_priority_min_representable_percent(4) == 25);
    assert(alarm_priority_min_representable_percent(7) == 14);
    assert(alarm_priority_min_representable_percent(20) == 5);
    assert(alarm_priority_min_representable_percent(0) == 0);
    assert(!alarm_priority_target_representable(4));
    assert(!alarm_priority_target_representable(7));
    assert(!alarm_priority_target_representable(19));
    assert(alarm_priority_target_representable(20));
    assert(alarm_priority_target_representable(200));
}

/* A real 5/15/80 system passes; the shapes that must not be mistaken for it fail. */
static void test_target_recognises_a_genuine_distribution(void)
{
    const alarm_priority_census_t textbook = {5, 15, 80};
    assert(alarm_priority_meets_target(&textbook));
    assert(alarm_priority_total(&textbook) == 100);

    /* Inside the tolerance band on all three. */
    const alarm_priority_census_t near = {8, 12, 80};
    assert(alarm_priority_meets_target(&near));

    /* Too many high-priority alarms - the classic failure EEMUA describes. */
    const alarm_priority_census_t top_heavy = {30, 20, 50};
    assert(!alarm_priority_meets_target(&top_heavy));

    /* A small high count is not a pass on its own: this system has no low band
     * at all, which is a different defect and must not be reported as compliant. */
    const alarm_priority_census_t no_low_band = {5, 95, 0};
    assert(!alarm_priority_meets_target(&no_low_band));

    const alarm_priority_census_t empty = {0, 0, 0};
    assert(!alarm_priority_meets_target(&empty));
    assert(alarm_priority_total(&empty) == 0);
}

/* The controller's own census, asserted honestly. If somebody later demotes an
 * alarm to make the distribution look better, this is the test that should stop
 * them and make them justify it instead.
 *
 * Alarm population: NET-001 high, MTR-002 high, MTR-003 medium, MTR-001 medium.
 * Condition population adds the three informational event codes as low. */
static void test_this_controller_misses_the_target_and_says_so(void)
{
    const alarm_priority_census_t alarms = {2, 2, 0};
    assert(alarm_priority_total(&alarms) == 4);
    assert(alarm_priority_percent(alarms.high, 4) == 50);
    assert(alarm_priority_percent(alarms.medium, 4) == 50);
    assert(alarm_priority_percent(alarms.low, 4) == 0);
    assert(!alarm_priority_meets_target(&alarms));
    assert(!alarm_priority_target_representable(alarm_priority_total(&alarms)));

    const alarm_priority_census_t conditions = {2, 2, 3};
    assert(alarm_priority_total(&conditions) == 7);
    assert(alarm_priority_percent(conditions.high, 7) == 29);
    assert(alarm_priority_percent(conditions.medium, 7) == 29);
    assert(alarm_priority_percent(conditions.low, 7) == 43);
    assert(!alarm_priority_meets_target(&conditions));
    assert(!alarm_priority_target_representable(alarm_priority_total(&conditions)));
}

int main(void)
{
    test_empty_ring_measures_nothing();
    test_window_boundary_is_half_open();
    test_counts_only_what_is_inside_each_window();
    test_steady_target_is_strict();
    test_peak_target_is_ten_inclusive();
    test_flood_is_recorded_as_a_peak_and_remembered();
    test_peak_matches_brute_force();
    test_counts_survive_the_uptime_wrap();
    test_overflow_keeps_newest_and_admits_the_loss();
    test_rate_arithmetic_never_wraps();
    test_window_observed_gates_the_verdict();
    test_null_arguments_are_survivable();
    test_percent_rounds_half_away_from_zero();
    test_target_is_unreachable_for_a_small_population();
    test_target_recognises_a_genuine_distribution();
    test_this_controller_misses_the_target_and_says_so();
    printf("alarm metrics unit tests passed "
           "(EEMUA rate windows, peak against brute force, uptime wrap, "
           "and the honest 50/50/0 alarm distribution)\n");
    return 0;
}
