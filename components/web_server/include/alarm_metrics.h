#pragma once

/* EEMUA 191 quantitative alarm metrics: gaps A6 and A10 of
 * docs/ALARM_MANAGEMENT_RESEARCH.md.
 *
 * EEMUA 191 is unusual among the alarm standards in giving hard numbers, which
 * is exactly what makes it testable:
 *
 *   - average alarm rate in steady state: fewer than 1 alarm per operator per
 *     10 minutes;
 *   - peak rate after a major upset: no more than 10 alarms in the first
 *     10 minutes;
 *   - priority distribution: roughly 5% high, 15% medium, 80% low.
 *
 * Until something measures those, there is no evidence the alarm system is
 * usable - only an assertion that it is. This module is the measurement, and it
 * is deliberately a separate translation unit from operational_api.c for two
 * reasons.
 *
 * The first is that every function here is pure: it reads its arguments, touches
 * no global state, allocates nothing, logs nothing and calls nothing. That makes
 * it safe to call from inside the alarm module's critical section, where
 * interrupts are disabled and a malloc or an ESP_LOG has already caused real
 * crashes on this controller.
 *
 * The second is that pure arithmetic can be executed on a host rather than
 * pattern-matched in a source contract. tests/alarm_metrics_test.c compiles this
 * file with gcc and runs it, including the wrap-around and saturation cases that
 * cannot be provoked on hardware in a reasonable time.
 *
 * TIME BASE. This controller has no real-time clock. Every timestamp here is
 * milliseconds since the controller started, following the convention the alarm
 * journal already declares as time_base: "uptime_ms". uint32 milliseconds wrap
 * at about 49.7 days, so every comparison in this module is written as a signed
 * difference rather than an ordering of absolute values.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- A10: rate */

/* EEMUA states both of its rate targets over ten minutes, so ten minutes is the
 * window the metric is defined on and everything else is derived from it. */
#define ALARM_RATE_WINDOW_MS 600000U
#define ALARM_RATE_HOUR_MS 3600000U
#define ALARM_RATE_DAY_MS 86400000U

/* "Fewer than 1 alarm per operator per 10 minutes" and "no more than 10 alarms
 * in the first 10 minutes of an upset", expressed as the numbers the code
 * actually compares against. The steady-state figure is strict: EEMUA says
 * fewer than one, not at most one. */
#define ALARM_RATE_STEADY_LIMIT_MILLI 1000U
#define ALARM_RATE_UPSET_LIMIT 10U

/* 192 timestamps is 768 bytes. It bounds the ring at roughly nineteen times the
 * ten-minute flood limit, so a genuine EEMUA-scale upset is measured exactly
 * rather than clipped, and it still covers a day at the handful of transitions a
 * healthy PV-DG site produces. Beyond that the ring drops its oldest records and
 * says so - alarm_rate_window_truncated() - rather than reporting a count that
 * quietly understates a flood. Silently under-reporting an alarm rate would
 * defeat the only purpose this module has. */
#define ALARM_RATE_CAPACITY 192U

typedef struct {
    uint32_t stamps[ALARM_RATE_CAPACITY];
    uint16_t retained;          /* timestamps currently held */
    uint16_t head;              /* next slot to write; oldest slot when full */
    uint32_t total;             /* every alarm counted since reset, never dropped */
    uint32_t discarded;         /* timestamps pushed out of the ring */
    uint16_t peak_per_window;   /* worst ten-minute count ever observed */
    uint32_t peak_at_ms;        /* when that worst window closed */
} alarm_rate_t;

void alarm_rate_reset(alarm_rate_t *rate);

/* Counts one alarm at `timestamp_ms` and updates the peak. Pure in the sense
 * that matters here: no allocation, no logging, no file access, bounded work.
 * Safe to call with interrupts disabled. */
void alarm_rate_record(alarm_rate_t *rate, uint32_t timestamp_ms);

/* Alarms counted in the `window_ms` immediately before `now_ms`. */
uint16_t alarm_rate_window_count(const alarm_rate_t *rate, uint32_t now_ms,
                                 uint32_t window_ms);

/* True when the ring has dropped records that the window still covers, so the
 * count above is a lower bound rather than the answer. */
bool alarm_rate_window_truncated(const alarm_rate_t *rate, uint32_t now_ms,
                                 uint32_t window_ms);

/* A rate over a window longer than the controller has been running is not a
 * measurement, it is an extrapolation. This says whether the window has actually
 * elapsed. */
bool alarm_rate_window_observed(uint32_t uptime_ms, uint32_t window_ms);

/* Normalises `count` alarms seen over `observed_ms` into alarms per
 * `window_ms`, scaled by 1000 so the comparison stays in integers. */
uint32_t alarm_rate_per_window_milli(uint16_t count, uint32_t observed_ms,
                                     uint32_t window_ms);

bool alarm_rate_meets_steady_target(uint32_t per_window_milli);
bool alarm_rate_meets_peak_target(uint16_t peak_per_window);

/* ------------------------------------------------- A6: priority distribution */

#define ALARM_PRIORITY_TARGET_HIGH_PERCENT 5U
#define ALARM_PRIORITY_TARGET_MEDIUM_PERCENT 15U
#define ALARM_PRIORITY_TARGET_LOW_PERCENT 80U

/* EEMUA says "roughly". A band of five percentage points either side is the
 * width this controller will call a match; anything wider would let 50/50/0 pass
 * as 5/15/80. */
#define ALARM_PRIORITY_TARGET_TOLERANCE_PERCENT 5U

/* Below twenty conditions a 5% high band cannot be represented at all: one
 * condition out of nineteen is already 5.3%, so the smallest non-zero share
 * exceeds the target and the distribution is arithmetically unreachable rather
 * than badly assigned. Reporting a miss without reporting that fact would
 * invite someone to "fix" it by demoting a condition that genuinely stops the
 * plant being controlled safely, which is the opposite of rationalisation. */
#define ALARM_PRIORITY_TARGET_MIN_POPULATION 20U

typedef struct {
    uint16_t high;
    uint16_t medium;
    uint16_t low;
} alarm_priority_census_t;

uint16_t alarm_priority_total(const alarm_priority_census_t *census);

/* Rounded to the nearest whole percent, half away from zero. */
uint16_t alarm_priority_percent(uint16_t part, uint16_t total);

/* The smallest non-zero share a population of this size can express. */
uint16_t alarm_priority_min_representable_percent(uint16_t total);

/* Whether the 5/15/80 target is reachable at all for a population this small. */
bool alarm_priority_target_representable(uint16_t total);

/* Whether the census sits inside the tolerance band on all three bands. */
bool alarm_priority_meets_target(const alarm_priority_census_t *census);

#ifdef __cplusplus
}
#endif
