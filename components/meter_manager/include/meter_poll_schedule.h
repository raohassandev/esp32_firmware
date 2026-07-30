#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Floor for the FAILURE cadence, independent of the configured healthy interval.
 * Zero is a legal healthy interval meaning "poll again as soon as the previous
 * transaction completed", but a refused connection returns almost instantly, so
 * the failure path needs a floor of its own or the loop spins at CPU speed. */
#define METER_FAILURE_BASE_MS 100U
#define METER_MAX_BACKOFF_MS 10000U

/* Delay to insert after a completed transaction, in milliseconds.
 *
 * Pure so the invariants are executed by host tests rather than pattern-matched
 * in source. Two earlier grep-based contracts broke on legitimate refactors of
 * this calculation while the guarantees still held, which is precisely the case
 * for testing behaviour instead of text.
 *
 * The guarantees, all covered by tests/meter_poll_schedule_test.c:
 *   - A healthy meter polls at exactly the configured interval, including 0,
 *     which means poll-on-completion.
 *   - A failing or degraded meter ALWAYS waits, even when the configured interval
 *     is 0, so an unreachable device can never become a tight retry loop.
 *   - Backoff is never shorter than the configured healthy interval: slowing down
 *     on failure must not accidentally speed the bus up.
 *   - Backoff is bounded, so a long outage cannot push the retry period out
 *     indefinitely.
 */
uint32_t meter_poll_delay_ms(uint32_t configured_interval_ms,
                             bool degraded,
                             uint32_t consecutive_failures);

#ifdef __cplusplus
}
#endif
