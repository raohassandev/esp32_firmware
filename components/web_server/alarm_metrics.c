#include "alarm_metrics.h"

#include <string.h>

/* See include/alarm_metrics.h for why this is a separate, pure translation unit.
 * Nothing in this file may allocate, log or take a lock: it is called from inside
 * the alarm module's critical section. */

void alarm_rate_reset(alarm_rate_t *rate)
{
    if (!rate) return;
    memset(rate, 0, sizeof(*rate));
}

/* A timestamp is "inside the window" when the elapsed time since it is at least
 * zero and less than the window. The subtraction is done in uint32 and read as
 * int32, which is the only comparison that survives the ~49.7 day wrap of
 * uptime milliseconds: comparing the absolute values instead would place every
 * pre-wrap record in the future and report a rate of zero for the next 49 days.
 *
 * A timestamp in the future - which a caller can produce by passing a now_ms
 * older than a record it already made - is excluded rather than counted. It is
 * not evidence of an alarm inside this window. */
static bool stamp_in_window(uint32_t stamp, uint32_t now_ms, uint32_t window_ms)
{
    const int32_t elapsed = (int32_t)(now_ms - stamp);
    if (elapsed < 0) return false;
    return (uint32_t)elapsed < window_ms;
}

uint16_t alarm_rate_window_count(const alarm_rate_t *rate, uint32_t now_ms,
                                 uint32_t window_ms)
{
    if (!rate || window_ms == 0U) return 0U;
    uint16_t counted = 0U;
    for (uint16_t index = 0U; index < rate->retained; ++index) {
        if (stamp_in_window(rate->stamps[index], now_ms, window_ms)) counted++;
    }
    return counted;
}

void alarm_rate_record(alarm_rate_t *rate, uint32_t timestamp_ms)
{
    if (!rate) return;
    if (rate->retained >= ALARM_RATE_CAPACITY) {
        /* Full: the slot at head holds the oldest timestamp, so writing there
         * both stores the new record and drops the one it replaces. The loss is
         * counted rather than hidden - see alarm_rate_window_truncated(). */
        rate->discarded++;
    } else {
        rate->retained++;
    }
    rate->stamps[rate->head] = timestamp_ms;
    rate->head = (uint16_t)((rate->head + 1U) % ALARM_RATE_CAPACITY);
    if (rate->total < UINT32_MAX) rate->total++;

    /* The peak is evaluated here, at the instant of a record, and that is not an
     * approximation: a trailing count over a fixed window can only increase when
     * a record enters it, so the maximum over every possible window instant is
     * always attained immediately after some record. Sampling at every record
     * therefore finds the true peak without keeping a history of windows. */
    const uint16_t current = alarm_rate_window_count(rate, timestamp_ms, ALARM_RATE_WINDOW_MS);
    if (current > rate->peak_per_window) {
        rate->peak_per_window = current;
        rate->peak_at_ms = timestamp_ms;
    }
}

bool alarm_rate_window_truncated(const alarm_rate_t *rate, uint32_t now_ms,
                                 uint32_t window_ms)
{
    if (!rate || window_ms == 0U) return false;
    if (rate->discarded == 0U) return false;
    if (rate->retained < ALARM_RATE_CAPACITY) return false;
    /* When the ring is full the oldest surviving timestamp is the one at head.
     * If even that still falls inside the window, then the records already
     * discarded fell inside it too and the count understates the truth. */
    return stamp_in_window(rate->stamps[rate->head], now_ms, window_ms);
}

bool alarm_rate_window_observed(uint32_t uptime_ms, uint32_t window_ms)
{
    return uptime_ms >= window_ms;
}

uint32_t alarm_rate_per_window_milli(uint16_t count, uint32_t observed_ms,
                                     uint32_t window_ms)
{
    if (observed_ms == 0U || window_ms == 0U) return 0U;
    /* 64-bit throughout: 65535 alarms x 600000 ms x 1000 overflows uint32 by
     * four orders of magnitude, and a wrapped rate would read as healthy. */
    const uint64_t scaled = (uint64_t)count * (uint64_t)window_ms * 1000ULL;
    const uint64_t result = scaled / (uint64_t)observed_ms;
    return result > (uint64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

bool alarm_rate_meets_steady_target(uint32_t per_window_milli)
{
    /* Strictly fewer than one per window. EEMUA says "fewer than 1 alarm per
     * operator per 10 minutes", and exactly one is not fewer than one. */
    return per_window_milli < ALARM_RATE_STEADY_LIMIT_MILLI;
}

bool alarm_rate_meets_peak_target(uint16_t peak_per_window)
{
    /* "No more than 10", so ten itself passes. */
    return peak_per_window <= ALARM_RATE_UPSET_LIMIT;
}

/* ------------------------------------------------- A6: priority distribution */

uint16_t alarm_priority_total(const alarm_priority_census_t *census)
{
    if (!census) return 0U;
    const uint32_t total = (uint32_t)census->high + census->medium + census->low;
    return total > UINT16_MAX ? UINT16_MAX : (uint16_t)total;
}

uint16_t alarm_priority_percent(uint16_t part, uint16_t total)
{
    if (total == 0U) return 0U;
    if (part > total) part = total;
    /* Rounded half away from zero, in integers: 2 of 7 must read 29%, not 28%. */
    const uint32_t scaled = ((uint32_t)part * 100U) + ((uint32_t)total / 2U);
    return (uint16_t)(scaled / (uint32_t)total);
}

uint16_t alarm_priority_min_representable_percent(uint16_t total)
{
    if (total == 0U) return 0U;
    return alarm_priority_percent(1U, total);
}

bool alarm_priority_target_representable(uint16_t total)
{
    return total >= ALARM_PRIORITY_TARGET_MIN_POPULATION;
}

static bool within_tolerance(uint16_t actual, uint16_t target)
{
    const uint16_t tolerance = ALARM_PRIORITY_TARGET_TOLERANCE_PERCENT;
    if (actual > target) return (uint16_t)(actual - target) <= tolerance;
    return (uint16_t)(target - actual) <= tolerance;
}

bool alarm_priority_meets_target(const alarm_priority_census_t *census)
{
    const uint16_t total = alarm_priority_total(census);
    if (total == 0U) return false;
    /* All three bands, not just the high one. A system with no low-priority
     * alarms at all has not met a distribution target simply because its high
     * count happens to be small. */
    return within_tolerance(alarm_priority_percent(census->high, total),
                            ALARM_PRIORITY_TARGET_HIGH_PERCENT) &&
           within_tolerance(alarm_priority_percent(census->medium, total),
                            ALARM_PRIORITY_TARGET_MEDIUM_PERCENT) &&
           within_tolerance(alarm_priority_percent(census->low, total),
                            ALARM_PRIORITY_TARGET_LOW_PERCENT);
}
