/* Host-compiled unit test for the meter poll cadence.
 *
 * This executes the real rule. It replaces two source-contract assertions that
 * pattern-matched the old expression text and broke on a legitimate refactor even
 * though every guarantee still held -- the reason for testing behaviour rather
 * than source.
 *
 * The requirement being served is "poll again as soon as the previous transaction
 * completes", so zero is a legal healthy interval. That makes the failure path
 * safety-relevant: scaling zero yields zero, and a refused TCP connection returns
 * in microseconds, so a loop with no floor would spin at CPU speed, saturate the
 * network and starve everything sharing the transport.
 */

#include <assert.h>
#include <stdio.h>

#include "meter_poll_schedule.h"

/* Healthy means exactly what was configured, including zero. */
static void test_healthy_honours_the_configured_interval(void)
{
    const uint32_t intervals[] = {0, 1, 25, 100, 250, 1000, 3600000};
    for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        assert(meter_poll_delay_ms(intervals[i], false, 0) == intervals[i]);
    }
}

/* The property that keeps a dead device from spinning the CPU. */
static void test_failure_always_waits_even_at_zero_interval(void)
{
    for (uint32_t failures = 1; failures <= 10; ++failures) {
        assert(meter_poll_delay_ms(0, false, failures) >= METER_FAILURE_BASE_MS);
    }
    /* Degraded with no consecutive failure still waits. */
    assert(meter_poll_delay_ms(0, true, 0) >= METER_FAILURE_BASE_MS);
}

/* Backing off must never accidentally speed the bus up. */
static void test_backoff_is_never_faster_than_configured(void)
{
    const uint32_t intervals[] = {0, 1, 50, 100, 250, 1000, 9999, 10000, 20000, 3600000};
    for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        const uint32_t base = intervals[i];
        for (uint32_t failures = 0; failures <= 6; ++failures) {
            for (int degraded = 0; degraded <= 1; ++degraded) {
                if (!degraded && failures == 0U) continue; /* the healthy case */
                const uint32_t delay = meter_poll_delay_ms(base, degraded != 0, failures);
                assert(delay >= base);
                assert(delay > 0U);
            }
        }
    }
}

/* Bounded, so a long outage cannot push the retry period out indefinitely. */
static void test_backoff_is_bounded(void)
{
    for (uint32_t failures = 1; failures <= 1000; failures += 97) {
        assert(meter_poll_delay_ms(250, true, failures) <= METER_MAX_BACKOFF_MS);
    }
    /* A configured interval above the ceiling is its own ceiling: it must be
     * honoured rather than clamped down to METER_MAX_BACKOFF_MS. */
    const uint32_t slow = 20000U;
    const uint32_t delay = meter_poll_delay_ms(slow, true, 5);
    assert(delay >= slow);
    assert(delay == slow); /* clamped to its own base, not below it */
}

/* Backoff grows with consecutive failures, then stops growing. */
static void test_backoff_is_monotonic_then_saturates(void)
{
    uint32_t previous = 0;
    for (uint32_t failures = 1; failures <= 3; ++failures) {
        const uint32_t delay = meter_poll_delay_ms(100, false, failures);
        assert(delay >= previous);
        previous = delay;
    }
    /* Beyond the step cap the delay stops growing rather than exploding. */
    assert(meter_poll_delay_ms(100, false, 4) == meter_poll_delay_ms(100, false, 3));
    assert(meter_poll_delay_ms(100, false, 100000) == meter_poll_delay_ms(100, false, 3));
}

/* A large interval times a multiplier must not wrap a 32-bit value into a tiny
 * delay, which would be the worst possible failure of this function. */
static void test_no_overflow_at_extremes(void)
{
    const uint32_t huge = 0xFFFFFFFFu;
    const uint32_t delay = meter_poll_delay_ms(huge, true, 3);
    assert(delay >= huge / 2U);   /* certainly not wrapped to something small */
    assert(delay == huge);        /* clamped to its own base */
}

int main(void)
{
    test_healthy_honours_the_configured_interval();
    test_failure_always_waits_even_at_zero_interval();
    test_backoff_is_never_faster_than_configured();
    test_backoff_is_bounded();
    test_backoff_is_monotonic_then_saturates();
    test_no_overflow_at_extremes();
    printf("meter poll schedule unit tests passed\n");
    return 0;
}
