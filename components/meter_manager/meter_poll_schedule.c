#include "meter_poll_schedule.h"

uint32_t meter_poll_delay_ms(uint32_t configured_interval_ms,
                             bool degraded,
                             uint32_t consecutive_failures)
{
    /* Healthy: exactly what the engineer asked for. Zero means the next request
     * goes out as soon as this one finished. */
    if (!degraded && consecutive_failures == 0U) return configured_interval_ms;

    /* Failing: back off from at least our own floor, never from below the
     * configured interval. Taking the larger of the two is what guarantees the
     * backoff can never poll faster than the healthy cadence while still
     * producing a real delay when that cadence is zero. */
    uint32_t failure_base = configured_interval_ms > METER_FAILURE_BASE_MS
                                ? configured_interval_ms
                                : METER_FAILURE_BASE_MS;

    /* At least one doubling always applies here: reaching this point means either
     * degraded, or at least one consecutive failure. */
    uint32_t multiplier = degraded ? 2U : 1U;
    uint32_t steps = consecutive_failures > 3U ? 3U : consecutive_failures;
    multiplier <<= steps;

    /* 64-bit so a large configured interval times a multiplier cannot wrap. */
    uint64_t delay = (uint64_t)failure_base * (uint64_t)multiplier;

    /* Ceiling never clamps below the configured interval, or a slow bus would be
     * sped up by its own backoff. */
    uint32_t ceiling = failure_base > METER_MAX_BACKOFF_MS ? failure_base
                                                           : METER_MAX_BACKOFF_MS;
    return delay > (uint64_t)ceiling ? ceiling : (uint32_t)delay;
}
