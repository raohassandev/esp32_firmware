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
 * CONFIRMATION ON MEASURED POWER, AND WHAT IT CAN AND CANNOT PROVE
 * ----------------------------------------------------------------
 * Some command targets cannot be confirmed by reading the command back at all.
 * The Huawei SmartLogger plant interface is the worked example: a write to the
 * plant percentage register is STORED by the logger and then forwarded to the
 * inverters ("This interface stores data ...", "After the SmartLogger receives
 * the instruction value, it synchronizes the value in percentage to all
 * connected inverters" - SmartLogger ModBus Interface Definitions, Issue 35,
 * PDF p.32 (24) Table 3-1 and p.33 (25) s3.3). Reading that register back
 * returns the stored command, not the plant's achieved state. Worse, the
 * logger's own allocation policy may multiply the commanded percentage by an
 * "Adjustment coefficient" that has no register and is invisible to a Modbus
 * client (SmartLogger3000 User Manual, Issue 03, PDF p.130 (122)), so a
 * commanded 80 % need not deliver 80 %. A profile confirming on that echo would
 * report CONFIRMED for a limit that is not in force - the same
 * false-confirmation defect that already required refusing four brands.
 *
 * So a profile may declare that its command is confirmed by an INDEPENDENT
 * MEASURED quantity: plant active power, in kW, read from a different register.
 *
 * This is where honesty matters more than anywhere else in the module, because
 * measured power is a WEAK witness in one specific direction:
 *
 *     Output BELOW a commanded limit is equally consistent with
 *         (a) the limit being honoured, and
 *         (b) the sun going in.
 *
 * There is no way to tell (a) from (b) from a single post-command measurement,
 * and a controller that calls (b) "confirmed" has invented a limit it never
 * demonstrated. Therefore:
 *
 *   A limit is DEMONSTRATED only when output was ABOVE the new limit before the
 *   command and at or below it after. Nothing else is demonstration.
 *
 *   If that cannot be established - no usable pre-command baseline, or a
 *   baseline that was already at or below the new limit - the verdict is
 *   UNVERIFIED, never CONFIRMED. The proof field says why
 *   (INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM).
 *
 * What measured-power confirmation CAN prove, and does:
 *   - A limit that IS being honoured on a plant that was generating above it:
 *     CONFIRMED, with limit_demonstrated set.
 *   - A limit that is NOT being honoured: output above the limit past the settle
 *     window is MISMATCHED. This direction is unambiguous, because no change in
 *     irradiance can lift a plant ABOVE a limit that is in force. It is also the
 *     direction that matters for protecting a generator.
 *
 * What it CANNOT prove, ever:
 *   - That a limit is in force while the plant is below it for its own reasons.
 *     A commanded 80 % on a plant running at 30 % under cloud proves nothing at
 *     all, and it is reported as proving nothing.
 *   - Anything about a command of 100 % (or any limit at or above capacity):
 *     the baseline can never be above such a limit, so it is permanently
 *     ambiguous - correctly.
 *   - The "Adjustment coefficient". A demonstrated limit says output is at or
 *     below what was asked; it does not say the coefficient is 1.
 *
 * The ambiguous verdict deliberately does NOT demand a safe-zero. Demanding one
 * would drive PV to zero every time irradiance fell below the commanded limit,
 * which is most of a working day, and that is a worse outcome than the ambiguity.
 * The escalation is left to the next sample: if output later rises above the
 * limit past the settle window the verdict becomes MISMATCHED and the safe-zero
 * is demanded then. Callers must therefore keep evaluating, and must not treat
 * an ambiguous verdict as a command in force - only CONFIRMED advances a
 * commanded value.
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

/* Whether, and how, an independent measured quantity takes part in the verdict.
 *
 * NONE is zero, so every profile written before measured-power confirmation
 * existed keeps exactly the behaviour it had: the setpoint readback decides. */
typedef enum {
    INVERTER_MEASURED_CONFIRM_NONE = 0,
    /* Measured power is preferred but not mandatory. A demonstrated limit
     * confirms; if measurement cannot demonstrate anything, a matching setpoint
     * readback may still confirm - with limit_demonstrated false, so the caller
     * can tell the two apart. For a device whose readback is a genuine applied
     * value rather than an echo. */
    INVERTER_MEASURED_CONFIRM_CORROBORATING,
    /* Measured power is the ONLY thing that may confirm. A matching setpoint
     * readback is ACCEPTANCE and nothing more. This is the mode for a command
     * target whose readback is a stored echo. */
    INVERTER_MEASURED_CONFIRM_REQUIRED
} inverter_measured_confirm_mode_t;

/* What the verdict actually rests on. Reported so that "confirmed" is never
 * read without knowing what confirmed it. NONE is zero. */
typedef enum {
    INVERTER_WRITE_PROOF_NONE = 0,
    /* A post-write setpoint readback matched. Depending on the device this is
     * either an applied value or an echo of a stored command. */
    INVERTER_WRITE_PROOF_SETPOINT_READBACK,
    /* Measured output was above the new limit before the command and at or below
     * it after. The limit is demonstrated. */
    INVERTER_WRITE_PROOF_MEASURED_POWER,
    /* Measured output is at or below the limit, but was ALREADY at or below it
     * before the command (or no baseline exists). Consistent with the limit
     * being honoured and equally consistent with falling irradiance. Proves
     * nothing, and is reported as UNVERIFIED. */
    INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM
} inverter_write_proof_t;

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

    /* ---- Measured-power confirmation. All ignored when mode is NONE. ---- */
    inverter_measured_confirm_mode_t measured_mode;

    /* A measured-power sample was read and decoded successfully, and is fresh. */
    bool measured_valid;
    /* That sample was taken strictly AFTER the write being judged. A measurement
     * older than the command says nothing about the command. */
    bool measured_after_write;
    /* Measured output now, kW. May be negative (a plant can import). */
    float measured_kw;

    /* A pre-command measurement exists. Without it a limit can never be
     * demonstrated, only found consistent - see the header comment. */
    bool baseline_valid;
    /* That baseline was taken strictly BEFORE the write. A "baseline" sampled
     * after the command has already been affected by it. */
    bool baseline_before_write;
    /* Measured output immediately before the command, kW. */
    float baseline_kw;

    /* Rated capacity the commanded percentage refers to, kW. Must be positive
     * and finite whenever a measured mode is in use, because the commanded
     * limit in kW is derived from it:
     *
     *     limit_kw = capacity_kw * commanded_percent / 100
     *
     * It is derived here rather than passed in so that there is exactly one
     * definition of the limit being judged. A WRONG capacity makes every
     * measured verdict wrong, which is why it is a commissioning value and not
     * a default. */
    float capacity_kw;

    /* The tolerance band on the measured comparison, stated either absolutely in
     * kW or as a percentage of capacity_kw. A profile may state either or both;
     * at least one must be positive or the measured evidence is refused as
     * incompletely described, because a zero band on a physical measurement is
     * not a tolerance, it is a bug.
     *
     * When both are stated the WIDER band is used. That is deliberately not
     * simply "the safer" choice: the band appears on both sides of the test, so
     * widening it both delays a MISMATCHED verdict AND demands a larger
     * pre-command excursion before a limit counts as demonstrated. The second
     * effect is the conservative one and it is the reason for the choice. */
    float measured_tolerance_kw;
    float measured_tolerance_percent_of_capacity;

    /* ---- Post-command scheduling-authority assertion (contention detector) ---
     *
     * Some plant-level command targets publish a read-only register saying WHICH
     * authority currently owns scheduling. After this controller has commanded,
     * that register must name this controller's channel; anything else means a
     * different master owns the plant and will fight this one. It is checked
     * AFTER the write, never as a precondition - see the profile comment for why
     * a precondition deadlocks.
     *
     * authority_checked false disables the whole group. */
    bool authority_checked;
    bool authority_valid;        /* a read decoded successfully */
    bool authority_after_write;  /* strictly after the write being judged */
    bool authority_holds;        /* it named this controller's channel */
} inverter_write_evidence_t;

typedef struct {
    inverter_write_state_t state;
    /* The controller must drive this inverter to its safe fallback and drop it
     * from the commandable fleet until it confirms again. */
    bool requires_safe_zero;
    /* The observation is final: no further waiting will change it. */
    bool settled;
    /* True ONLY when measured output was above the new limit before the command
     * and at or below it after. Never true from a setpoint readback, however
     * exactly it matched, and never true from a measurement that was already
     * below the limit beforehand. */
    bool limit_demonstrated;
    /* What the verdict rests on. */
    inverter_write_proof_t proof;
} inverter_write_verdict_t;

/* Evaluates one inverter's confirmation evidence. NULL yields the fully
 * fail-closed verdict: UNVERIFIED, safe-zero required, settled. */
inverter_write_verdict_t inverter_write_confirmation_evaluate(
    const inverter_write_evidence_t *evidence);

/* Stable lowercase slug for the API: "unverified" | "pending" | "confirmed" |
 * "mismatched". */
const char *inverter_write_state_name(inverter_write_state_t state);

/* Stable lowercase slug for the API: "none" | "setpoint_readback" |
 * "measured_power" | "ambiguous_headroom". An unrecognised value degrades to
 * "none", the value that claims the least. */
const char *inverter_write_proof_name(inverter_write_proof_t proof);

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
