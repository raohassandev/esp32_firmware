#pragma once

/*
 * Prerequisite enable-register sequencing and verification.
 *
 * WHY THIS EXISTS
 * ---------------
 * Three of the four documented brands ignore their active-power setpoint until a
 * separate register holds a particular value first:
 *
 *   Solis     manual tag 3070 = 0xAA   ("0xAA ON, 0x55 OFF(Power to 100%)
 *                                        (for 3052 and 3081 Reg)", p.27 s5.6)
 *   Sungrow   manual tag 5007 = 0xAA   ("0xAA: Enable; 0x55: Disable", p.16 s3.2;
 *                                        tag 5008 is "Available when the power
 *                                        limitation switch (5007) is enabled")
 *   Chint/CPS 0x2602 = 1               ("1: Remote dispatch mode", p.32)
 *
 * The lethal detail is not that the setpoint is ignored. It is that the setpoint
 * register still ACCEPTS the write and still ECHOES it back, so an ordinary
 * readback matches and the controller reports the command CONFIRMED while the
 * inverter keeps generating at full output. A false confirmation is the worst
 * outcome this module can produce, because every layer above it - including the
 * operator - is then told the plant is under control when it is not.
 *
 * THE DESIGN
 * ----------
 * Identical in shape to the deferred write confirmation next door:
 *
 *   - The BACKGROUND acquisition task does all the I/O. It reads the enable
 *     register; if the register does not hold, it writes it; and then it READS
 *     AGAIN. Only a read may set the satisfied flag. A write never does.
 *   - The CONTROL path only reads that flag. It gains no transaction, no second
 *     round trip and no sleep. The control loop runs at a 20 ms period and HTTP
 *     handlers must never perform a direct blocking Modbus transaction.
 *   - An inverter whose prerequisite is not verified is excluded from the
 *     commandable capacity and from command target selection, exactly as a
 *     confirmation fault already excludes one.
 *
 * A prerequisite that cannot be READ BACK is not usable at all. That is treated
 * exactly like a missing setpoint readback and the profile is refused write
 * authority outright, because an enable register written blind puts the
 * controller straight back into the false-confirmation trap it is trying to
 * escape.
 *
 * Verification is REPEATED, not done once. These are switches a human or another
 * Modbus master can turn off, and the Solis manual is explicit that disabling
 * the switch returns the machine to 100 %. Losing the limit silently is the
 * failure being prevented.
 *
 * PURE. No ESP-IDF, no locks, no I/O, no allocation, no logging. Host-compilable
 * and safe to call from inside the caller's own critical section.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the background task should do next about this inverter's prerequisite.
 * NONE is zero so a zeroed verdict asks for no I/O. */
typedef enum {
    INVERTER_PREREQ_ACTION_NONE = 0,
    /* Read the enable register. Also the action after a write, because only a
     * read may conclude that the prerequisite holds. */
    INVERTER_PREREQ_ACTION_READ,
    /* The register was read and does not hold the required value: write it. */
    INVERTER_PREREQ_ACTION_WRITE
} inverter_prerequisite_action_t;

typedef struct {
    /* True when this evidence was actually assembled from a profile and live
     * runtime state. A zeroed struct has it false, and a zeroed struct must
     * never read as satisfied - unknown is not-satisfied, always. */
    bool populated;

    /* The device needs a prerequisite register at all
     * (profile->requires_prerequisite_enable). */
    bool required;
    /* The profile describes the write: a supported function code, an address
     * and the value to place there. */
    bool write_described;
    /* The profile describes the readback: a read-only function code and an
     * address. Without this the prerequisite is unverifiable. */
    bool readback_described;

    /* A successful, decoded read of the enable register exists. */
    bool have_sample;
    /* That sample held the required value under the profile's mask. */
    bool sample_holds;
    /* That sample was taken strictly AFTER the most recent enable write this
     * controller issued. True when no enable write has ever been issued, since
     * then there is no write for the sample to predate. A sample older than our
     * own write proves nothing about the state the write left behind. */
    bool sample_after_write;
    /* This controller has issued an enable write at least once. */
    bool write_issued;

    /* Age of that sample. */
    uint32_t sample_age_ms;
    /* Re-verify this often. */
    uint32_t recheck_ms;
    /* Past this age the sample is no longer evidence of anything and the
     * prerequisite reverts to unknown, hence not satisfied. Must be greater
     * than recheck_ms so an ordinary re-read does not make the fleet flap. */
    uint32_t expiry_ms;
} inverter_prerequisite_evidence_t;

typedef struct {
    /* The prerequisite is CONFIRMED to hold by a read. Only ever true because a
     * read said so, never because a write was accepted. */
    bool satisfied;
    /* The next I/O step for the background task. */
    inverter_prerequisite_action_t action;
    /* The device needs a prerequisite that this profile cannot describe or
     * cannot read back. No amount of polling will fix that, so write authority
     * must be refused outright rather than retried. */
    bool unverifiable;
} inverter_prerequisite_verdict_t;

/* Evaluates one inverter's prerequisite evidence. NULL or unpopulated evidence
 * yields the fail-closed verdict: not satisfied, no I/O requested, and marked
 * unverifiable so nothing upstream can read it as permission. */
inverter_prerequisite_verdict_t inverter_prerequisite_evaluate(
    const inverter_prerequisite_evidence_t *evidence);

/* Stable lowercase slug for an API or a log line: "none" | "read" | "write". */
const char *inverter_prerequisite_action_name(inverter_prerequisite_action_t action);

/*
 * The three predicates below are header-inline deliberately. The write-permission
 * gate in inverter_profiles.c consults them, and that file is compiled on its own
 * by tests/inverter_write_permission_test.c; putting them in a translation unit
 * would force every caller of the gate to link a second object, which is how the
 * gate ends up duplicated and the two copies drift apart.
 */

/* True for a function code this firmware will use to WRITE an enable register.
 * 0x06 writes a single register, 0x10 writes multiple. Nothing else is accepted:
 * a wrong function code here would be a blind write to a register whose
 * semantics the manual assigned to something else. */
static inline bool inverter_prerequisite_write_function_supported(uint8_t function)
{
    return function == 6U || function == 16U;
}

/* True for a function code that only READS. 0x03 holding, 0x04 input. The
 * readback of an enable register must never be able to mutate it. */
static inline bool inverter_prerequisite_read_function_supported(uint8_t function)
{
    return function == 3U || function == 4U;
}

/* The write-authority half of the rule, kept separate from the runtime evidence
 * so the permission gate can be decided from the profile alone.
 *
 *   needs one, and a readable one is described -> not blocked
 *   needs one, none described                  -> blocked
 *   needs one, described but not readable      -> blocked
 *   needs none                                 -> not blocked
 *
 * The last line is why this is not the same function as inverter_prerequisite_
 * evaluate(): a device with no prerequisite is not blocked BY a prerequisite,
 * but neither has it confirmed anything. */
static inline bool inverter_prerequisite_write_blocked(bool required,
                                                       bool write_described,
                                                       bool readback_described)
{
    if (!required) return false;
    /* Both halves are mandatory. A described write with no described readback is
     * the dangerous case and is refused for the same reason a setpoint with no
     * readback is refused: the controller would be asserting a state it cannot
     * observe, and the setpoint register echoes regardless, so the result is a
     * CONFIRMED verdict for a limit the inverter is ignoring. */
    return !write_described || !readback_described;
}

#ifdef __cplusplus
}
#endif
