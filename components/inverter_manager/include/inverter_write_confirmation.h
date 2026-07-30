#pragma once

/*
 * Deferred write confirmation (P0-9).
 *
 * WHY THIS EXISTS
 * ---------------
 * When the controller writes a power setpoint it must read the value back and
 * confirm it took effect. The previous implementation did that inline: after
 * every write it slept for a settle delay and then performed a second, blocking
 * Modbus read - inside the control task, once per inverter, up to twice per
 * inverter. On a twelve-inverter fleet that is over a second of the control
 * period spent asleep before the loop can close. Modbus speed is the product
 * owner's stated highest priority, so that cost is not acceptable.
 *
 * THE DESIGN
 * ----------
 * The write path issues the write and nothing else - no sleep, no second
 * transaction. It records what was requested and when. The readback rides the
 * background telemetry acquisition that already polls every inverter, and that
 * background task feeds each observation to this module, which returns one of:
 *
 *   PENDING     - written, settle window has not elapsed yet. Transient only.
 *   CONFIRMED   - a post-write readback matched the request within tolerance.
 *   MISMATCHED  - a post-write readback disagreed with the request.
 *   UNVERIFIED  - the write cannot be confirmed: no qualified readback register
 *                 for this manufacturer, unreadable state, or the confirmation
 *                 deadline elapsed with no usable post-write sample.
 *
 * A write is NEVER reported as successful merely because the transport accepted
 * it. UNVERIFIED is the honest answer for a manufacturer whose readback register
 * this firmware does not have a manual for, and it is never upgraded to
 * CONFIRMED. MISMATCHED and a lapsed deadline both demand a safe-zero.
 *
 * PURE. No ESP-IDF, no locks, no I/O, no allocation, no logging. Host-compilable
 * and safe to call from any task, including from inside the caller's own lock.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reported over the API; append, never renumber. */
typedef enum {
    INVERTER_WRITE_UNVERIFIED = 0,
    INVERTER_WRITE_PENDING,
    INVERTER_WRITE_CONFIRMED,
    INVERTER_WRITE_MISMATCHED
} inverter_write_state_t;

typedef struct {
    /* The assigned profile carries a manual-verified setpoint readback
     * register. False for every manufacturer whose readback address this
     * firmware does not have documented. */
    bool readback_supported;
    /* A write has been issued at all. False before the first command. */
    bool write_issued;
    /* The most recent write reached the device at transport level. */
    bool write_accepted;
    /* A readback sample was read and decoded successfully. */
    bool readback_valid;
    /* That sample was taken strictly after the write it is being compared to.
     * A sample older than the write proves nothing about the write. */
    bool readback_after_write;

    float commanded_percent;
    float readback_percent;
    float tolerance_percent;

    /* Milliseconds since the write being judged. */
    uint32_t age_since_write_ms;
    /* Grace period before a missing readback counts against the write. */
    uint32_t settle_ms;
    /* Beyond this age an unconfirmed write is UNVERIFIED, not PENDING. */
    uint32_t deadline_ms;
} inverter_write_evidence_t;

typedef struct {
    inverter_write_state_t state;
    /* The controller must drive this inverter to its safe fallback and drop it
     * from the commandable fleet until it confirms again. */
    bool requires_safe_zero;
    /* The observation is final: no further waiting will change it. */
    bool settled;
} inverter_write_verdict_t;

/* Evaluates one inverter's confirmation evidence. NULL yields the fully
 * fail-closed verdict: UNVERIFIED, safe-zero required, settled. */
inverter_write_verdict_t inverter_write_confirmation_evaluate(
    const inverter_write_evidence_t *evidence);

/* Stable lowercase slug for the API: "unverified" | "pending" | "confirmed" |
 * "mismatched". */
const char *inverter_write_state_name(inverter_write_state_t state);

/* Fleet roll-up. Worst-first precedence: MISMATCHED beats UNVERIFIED beats
 * PENDING beats CONFIRMED, so a fleet is only ever as trustworthy as its least
 * trustworthy member. An empty fleet is UNVERIFIED, never CONFIRMED. */
inverter_write_state_t inverter_write_state_worst(const inverter_write_state_t *states,
                                                  uint8_t count);

/* Whether a power-limit command must be withheld to respect the device's
 * documented minimum command interval.
 *
 * Pure, so the rule is executed by host tests rather than asserted about. It is
 * safety-relevant in an asymmetric way: withholding an INCREASE is harmless, but
 * withholding a REDUCTION is the harm the limit must never cause, because
 * reducing PV is how this product protects a generator from under-loading and
 * reverse power.
 *
 * Therefore:
 *   - minimum_interval_ms == 0            -> never limited
 *   - no previous command                 -> never limited
 *   - requested < previously requested    -> never limited (a reduction)
 *   - otherwise limited while the interval has not elapsed
 *
 * Timestamps are milliseconds and compared with unsigned arithmetic, so the
 * 32-bit rollover cannot make an old command look recent. A non-finite
 * previously-requested value is treated as unknown, and the command is allowed
 * rather than withheld: an unknown previous setpoint must not be able to block a
 * reduction. */
bool inverter_command_rate_limited(uint32_t minimum_interval_ms,
                                   bool has_previous_command,
                                   uint32_t last_command_ms,
                                   float previous_percent,
                                   float requested_percent,
                                   uint32_t now_ms);

#ifdef __cplusplus
}
#endif
