#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inverter_prerequisite.h"
#include "inverter_profile_decode.h"
#include "inverter_status.h"
#include "inverter_write_confirmation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INVERTER_PROFILE_QUALIFICATION_DOCUMENTED = 0,
    INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
    INVERTER_PROFILE_QUALIFICATION_BENCH_VERIFIED,
    INVERTER_PROFILE_QUALIFICATION_READ_ONLY_QUALIFIED,
    INVERTER_PROFILE_QUALIFICATION_WRITE_QUALIFIED,
    INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED
} inverter_profile_qualification_t;

typedef enum {
    INVERTER_PROFILE_CONNECTION_MODBUS_TCP = 0,
    INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
    INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY
} inverter_profile_connection_t;

/*
 * Which manufacturer's telemetry table a profile's block carries.
 *
 * An enum rather than a function pointer so the catalogue stays a table of data
 * that can be inspected, diffed and asserted about by a source contract -- the
 * same reason every other field here is data.
 */
typedef enum {
    /* No transcribed block. The family has no measurement page at all, which is
     * the honest state and is different from having one that reads zeros. */
    INVERTER_TELEMETRY_LAYOUT_NONE = 0,
    /* "Solar Inverter Modbus Interface Definitions (V3.0)", Huawei, Issue 01
     * (2023-01-17), section 3. See inverter_telemetry_block.h. */
    INVERTER_TELEMETRY_LAYOUT_HUAWEI_V3 = 1,
} inverter_telemetry_layout_t;

typedef struct {
    const char *id;
    const char *manufacturer;
    const char *model_family;
    const char *protocol;
    inverter_profile_connection_t connection;
    inverter_profile_qualification_t qualification;
    const char *manual_reference;
    bool simulator_only;

    /*
     * PARKED FOR THIS RELEASE PHASE BY PRODUCT DECISION.
     *
     * The product owner scoped this phase to Huawei, which is the brand with the
     * strongest evidence in this catalogue. Every other manufacturer's profile is
     * parked: kept in the catalogue, kept with its citations, and refused for
     * writing until the phase scope widens. Deleting them would throw away the
     * manual transcription work and, worse, would remove the record of WHY each
     * one is not commandable.
     *
     * THIS IS NOT A SUBSTITUTE FOR ANY EXISTING REFUSAL. It is checked first and
     * it only ever ADDS a refusal -- the readback requirement, the prerequisite
     * rule, the flash-backed rate rule and the production-qualification rule all
     * still apply underneath it, unchanged. Unparking a profile therefore
     * restores exactly the verdict it had before, which is what makes this
     * reversible without re-arguing the safety cases.
     *
     * Zero means in scope, so the flag reads as an exception list in the
     * catalogue. That is the one place the default is deliberately permissive,
     * and it is safe because being in scope grants nothing on its own: a profile
     * still has to pass every rule below to reach even LAB_ONLY.
     *
     * Per-brand unpark criteria are recorded in docs/RELEASE_READINESS.md
     * section 4b.
     */
    bool deferred_this_phase;

    /*
     * WHY THIS PROFILE IS PARKED, AND WHAT WOULD UNPARK IT.
     *
     * The criterion from docs/RELEASE_READINESS.md section 4b.1, carried on the
     * profile so the catalogue API can publish it. It existed only in that
     * document and in comments here, which meant an engineer looking for their
     * brand in the picker was told nothing: a parked profile and a profile with
     * no register map at all both simply reported write_allowed:false, and the
     * reasonable conclusion was that the product does not support the brand.
     *
     * Descriptive only. Nothing reads it to make a decision -- the refusal is
     * deferred_this_phase above, and this string cannot lift it. NULL for a
     * profile that is in scope.
     */
    const char *deferred_reason;

    bool has_identity_probe;
    uint8_t identity_function;
    uint16_t identity_address;
    uint8_t identity_words;
    uint32_t identity_expected;
    uint32_t identity_mask;

    /*
     * THE FULL TELEMETRY BLOCK: everything the machine measures, in one read.
     *
     * Separate from has_active_power above, and deliberately. That one register
     * is a CONTROL input -- the confirmation evaluator uses it, the fleet cap
     * uses it -- and it is read at the control rate on its own. This block is
     * what a PERSON needs: DC strings, AC per phase, yield, temperature, device
     * status. It is polled on a slower cadence, and its failure never touches a
     * control decision.
     *
     * READING GRANTS NOTHING. Every register in the block is RO in the manual.
     * Declaring it does not make a profile commandable, does not promote it out
     * of LAB_ONLY and is not evidence about physical equipment; the write gates
     * are unchanged and independent. A brand this firmware may not command can
     * still be SHOWN, which is precisely what a controller that refuses to
     * command it owes the person standing in front of it.
     *
     * The layout names which manufacturer's table the block carries. Zero means
     * the family has no transcribed block and simply has no such page -- not a
     * page of registers borrowed from a family that does.
     */
    inverter_telemetry_layout_t telemetry_layout;
    uint8_t telemetry_function;
    uint16_t telemetry_start;
    uint16_t telemetry_registers;

    bool has_active_power;
    uint8_t active_power_function;
    uint16_t active_power_address;
    uint8_t active_power_words;
    inverter_value_type_t active_power_type;
    inverter_word_order_t active_power_word_order;
    float active_power_scale;

    bool has_power_limit;
    uint8_t power_limit_function;
    uint16_t power_limit_address;
    uint8_t power_limit_words;

    /* How the command register is laid out on the wire.
     *
     * These two fields existed on the READBACK side only, and that asymmetry was a
     * bug rather than an omission: the encoder emitted the high word first
     * unconditionally, so a manufacturer documenting the other order got a value
     * with its halves swapped. For a float dispatch register the swapped halves are
     * not merely wrong, they are a different order of magnitude -- and the readback
     * decoder, being the only half that DID model word order, would decode
     * correctly, disagree, and latch a confirmation fault on a healthy machine.
     * With the profile-driven order the two halves are inverses by construction.
     *
     * Zero means U16/AB, which is exactly what the encoder previously hardcoded, so
     * every profile written before these fields existed is unchanged. For an
     * integer command the width in power_limit_words still decides the range; the
     * type field is what selects IEEE-754 encoding, because a float register cannot
     * be inferred from a width -- U32 and Float32 are both two registers wide and
     * the same percentage produces completely different bytes. */
    inverter_value_type_t power_limit_type;
    inverter_word_order_t power_limit_word_order;

    float raw_units_per_percent;
    float minimum_percent;
    float maximum_percent;

    bool has_power_limit_readback;
    uint8_t power_limit_readback_function;
    uint16_t power_limit_readback_address;
    uint8_t power_limit_readback_words;
    inverter_value_type_t power_limit_readback_type;
    inverter_word_order_t power_limit_readback_word_order;
    float power_limit_readback_scale;
    float readback_tolerance_percent;

    /* Confirmation on MEASURED power instead of, or in addition to, the setpoint
     * readback above.
     *
     * Why this exists: some command targets cannot be confirmed by reading the
     * command back, because the readback is an echo of a stored command rather
     * than an applied state. The Huawei SmartLogger plant interface is the case
     * this was written for -- the logger STORES the percentage and forwards it,
     * and an undocumented "Adjustment coefficient" may scale it on the way, so a
     * readback of the command register would report a limit that is not in force.
     * That is the same false confirmation that already required refusing four
     * brands, reached by a different door.
     *
     * The measured quantity is the profile's own active-power register: the
     * plant's measured output. A profile declaring a measured mode must therefore
     * also declare has_active_power, and the confirmation evaluator refuses the
     * evidence outright if the description is incomplete -- it does NOT quietly
     * fall back to the echo.
     *
     * READ THE CONFIRMATION HEADER BEFORE CHANGING THIS. Measured power is a weak
     * witness in one direction: output below a limit is equally consistent with
     * the limit being honoured and with the sun going in. A limit is only ever
     * demonstrated when output was ABOVE the new limit before the command and at
     * or below it after; anything else is UNVERIFIED, never confirmed.
     *
     * The tolerance is stated in kW, or as a percentage of the rated capacity the
     * command refers to, or both. At least one must be positive: a zero band on a
     * physical measurement is not a tolerance. The capacity itself comes from the
     * inverter's configured rating at runtime, which for a plant-level endpoint
     * MUST be the plant's total rated capacity -- a wrong capacity makes every
     * measured verdict wrong, so it is a commissioning value and not a default. */
    inverter_measured_confirm_mode_t measured_power_confirm;
    float measured_tolerance_kw;
    float measured_tolerance_percent_of_capacity;

    /* Post-command scheduling-authority assertion: a READ-ONLY register naming
     * which authority currently owns scheduling of this command target.
     *
     * It is checked AFTER a command, never as a precondition. A precondition
     * would deadlock on equipment that only adopts remote scheduling once it has
     * received a command: the controller would refuse to command until the mode
     * changed, and the mode would not change until it commanded.
     *
     * A value other than the expected one after our own command means a different
     * master owns the target and will fight this controller, so the verdict is
     * MISMATCHED and the safe fallback is demanded. The function code must be
     * read-only; nothing in this group is ever written. */
    bool has_command_authority_check;
    uint8_t command_authority_function; /* 0x03 holding, 0x04 input */
    uint16_t command_authority_address;
    uint16_t command_authority_expected;
    uint16_t command_authority_mask; /* zero means the whole register */

    /* How long this device may take before an accepted setpoint appears in its
     * readback register. Zero means "use the firmware default".
     *
     * This belongs to the profile because it is a property of the device, not of
     * the controller. Devices differ: the lab simulator defers a 40125 write by
     * about 1500 ms and reports the PREVIOUS active limit until it applies, so a
     * global 500 ms window judged a perfectly accepted command to be a mismatch
     * -- which latches a confirmation fault, removes the inverter from
     * commandable capacity and drives it to zero. A false fault on a healthy
     * machine is as damaging as a missed real one.
     *
     * It can only ever delay a verdict. It never turns a disagreement into a
     * success, and the confirmation deadline still bounds how long an
     * unconfirmed setpoint may stand. */
    uint32_t power_limit_settle_ms;

    /* Shortest interval permitted between two power-limit commands to this
     * device. Zero means "derive a safe default from the connection type".
     *
     * Not a performance knob -- a manufacturer constraint. The Huawei SmartLogger
     * Modbus definitions state that the adjustment value "should be issued at
     * intervals of not less than 1 seconds". This controller's default control
     * period is 250 ms, so an unthrottled loop would issue commands roughly four
     * times faster than the equipment documents as safe.
     *
     * This throttles ordinary setpoint updates ONLY. A protective reduction --
     * driving an inverter to its safe fallback -- is never delayed: holding back
     * a reduction is what endangers a generator. */
    uint32_t min_command_interval_ms;

    /* True when the manual documents a register that must hold a particular
     * value BEFORE a power-limit setpoint has any effect -- Solis tag 3070
     * (0xAA), Sungrow tag 5007 (0xAA), Chint/CPS 0x2602 (1) are the known cases.
     *
     * The meaning of this flag is unchanged: "this device needs one". It is a
     * statement about the equipment, not about what the firmware can do.
     *
     * It matters far more than it looks, because of how these devices fail. The
     * setpoint register still ACCEPTS the write and still ECHOES it back, so the
     * controller sees a matching readback and reports CONFIRMED -- while the
     * inverter quietly ignores the limit and keeps generating. A false
     * confirmation is the single worst outcome this module can produce: worse
     * than a mismatch, worse than a timeout, because every layer above it,
     * including the operator, is told the plant is under control when it is not.
     *
     * The firmware can now sequence and verify a prerequisite, so write
     * authority depends on whether THIS profile describes a verifiable one:
     *
     *   needs one + describes a write AND a readback -> ordinary rules apply
     *   needs one + nothing described                -> FORBIDDEN
     *   needs one + described but not readable       -> FORBIDDEN
     *
     * The unreadable case is refused for exactly the reason a setpoint with no
     * readback is refused. An enable register written blind is an assertion the
     * controller cannot check, and the false-confirmation trap is reached again
     * by a different door.
     *
     * Being unable to command is recoverable. Being told a plant is limited when
     * it is not is not. */
    bool requires_prerequisite_enable;

    /* True when the manual says the command register is stored in non-volatile
     * memory -- GoodWe 42407 is the known case, whose remark column reads
     * "Storage, does not support high-frequency write operations".
     *
     * This controller exists to move a setpoint continuously against a moving
     * generator load. Doing that to a flash-backed register wears the inverter's
     * memory out: a permanent hardware failure on a customer's machine, caused by
     * the controller working exactly as designed, with every individual write
     * reporting success. Nothing in the readback or the confirmation logic can see
     * it coming, which is why it needs its own field rather than a comment.
     *
     * A flash-backed register is therefore refused unless the profile also carries
     * a min_command_interval_ms, because without a manufacturer-stated permitted
     * rate there is no number this firmware could choose that is not invented. The
     * GoodWe manual states the prohibition and gives neither a permitted rate nor a
     * write-cycle budget, so that profile stays refused until GoodWe supplies one.
     *
     * If such a rate is ever supplied, the control engine's keepalive refresh must
     * ALSO be suppressed or lengthened for these registers: refreshing an unchanged
     * setpoint every couple of seconds is tens of thousands of writes a day, which
     * defeats the purpose of the interval. */
    bool command_register_is_flash_backed;
    /* The prerequisite WRITE: which function code, which address, which value.
     *
     * Nothing here is defaulted or inferred. A profile either carries a manual
     * citation for all three or leaves has_prerequisite_enable false, in which
     * case the device stays refused. There is no register in this product whose
     * address may be guessed, and an enable register is the last place to start:
     * the neighbouring addresses in all three manuals are mode selectors,
     * on/off commands and absolute power limits. */
    bool has_prerequisite_enable;
    uint8_t prerequisite_write_function;  /* 0x06 single, 0x10 multiple */
    uint16_t prerequisite_address;        /* PDU address, as it goes on the wire */
    uint16_t prerequisite_value;          /* the value that means "enabled" */

    /* The prerequisite READBACK. Mandatory for the device to be commandable at
     * all: a prerequisite that cannot be read back cannot be confirmed, and an
     * unconfirmable enable register is indistinguishable from an ignored one.
     *
     * The function code must be read-only (0x03 or 0x04). The mask selects the
     * bits that carry the meaning; zero is treated as 0xFFFF, i.e. the whole
     * register must equal prerequisite_value. */
    bool has_prerequisite_readback;
    uint8_t prerequisite_readback_function; /* 0x03 holding, 0x04 input */
    uint16_t prerequisite_readback_address;
    uint16_t prerequisite_readback_mask;

    /* How often the enable register is re-read to confirm it still holds. Zero
     * means "use the firmware default". A property of the risk, not of the
     * device, so a profile only needs to set it where the manual or the site
     * demands something tighter. See INVERTER_PREREQUISITE_RECHECK_MS. */
    uint32_t prerequisite_recheck_ms;

    /*
     * Optional operational status register. Deliberately left unconfigured for
     * every shipped profile: no manufacturer status address is hardcoded in
     * this firmware. Until a commissioning engineer supplies a manual-verified
     * description, every inverter reports INVERTER_STATE_UNKNOWN.
     */
    inverter_status_register_t status_register;

    uint32_t telemetry_poll_ms;
    uint32_t telemetry_stale_timeout_ms;
} inverter_profile_t;

/* True when this profile describes a prerequisite write completely enough to
 * issue: a supported write function code and a value. Address zero is legal --
 * Chint/CPS reads its identity from PDU 0x0000 -- so the address is not part of
 * the test. */
static inline bool inverter_profile_prerequisite_write_described(
    const inverter_profile_t *profile)
{
    return profile && profile->has_prerequisite_enable &&
           inverter_prerequisite_write_function_supported(profile->prerequisite_write_function);
}

/* True when this profile describes a readback of the prerequisite that this
 * firmware will actually perform: a read-only function code. */
static inline bool inverter_profile_prerequisite_readback_described(
    const inverter_profile_t *profile)
{
    return profile && profile->has_prerequisite_readback &&
           inverter_prerequisite_read_function_supported(
               profile->prerequisite_readback_function);
}

/* The write-authority consequence: this device needs a prerequisite and this
 * profile does not describe a verifiable one, so no command may be issued to it
 * in any mode. A NULL profile is blocked, like every other unknown. */
static inline bool inverter_profile_prerequisite_blocks_write(
    const inverter_profile_t *profile)
{
    if (!profile) return true;
    return inverter_prerequisite_write_blocked(
        profile->requires_prerequisite_enable,
        inverter_profile_prerequisite_write_described(profile),
        inverter_profile_prerequisite_readback_described(profile));
}

/* Mask to apply to a prerequisite readback. Zero in the profile means "the whole
 * register", so it is never used as a mask literally -- a zero mask would make
 * every possible reading compare equal and confirm the prerequisite
 * unconditionally, which is the exact false confirmation being prevented. */
static inline uint16_t inverter_profile_prerequisite_mask(const inverter_profile_t *profile)
{
    if (!profile || profile->prerequisite_readback_mask == 0U) return 0xFFFFU;
    return profile->prerequisite_readback_mask;
}

/* True when this profile describes measured-power confirmation completely enough
 * to evaluate: a mode other than NONE, an active-power register to measure with,
 * and at least one positive tolerance band.
 *
 * Deliberately NOT used to downgrade the mode to NONE when it returns false. A
 * profile that asks for measured confirmation and describes it badly must be
 * refused, not silently reverted to confirming on a setpoint echo -- that would
 * turn a transcription slip into the exact false confirmation the mode exists to
 * prevent. It is here so a contract test can execute the rule. */
static inline bool inverter_profile_measured_confirmation_described(
    const inverter_profile_t *profile)
{
    if (!profile) return false;
    if (profile->measured_power_confirm == INVERTER_MEASURED_CONFIRM_NONE) return false;
    if (!profile->has_active_power) return false;
    return profile->measured_tolerance_kw > 0.0f ||
           profile->measured_tolerance_percent_of_capacity > 0.0f;
}

/* True when the scheduling-authority assertion is described and READ-ONLY. A
 * write function code here would mean the controller mutating the register that
 * tells it who is in charge, so it is rejected rather than corrected. */
static inline bool inverter_profile_command_authority_described(
    const inverter_profile_t *profile)
{
    return profile && profile->has_command_authority_check &&
           inverter_prerequisite_read_function_supported(profile->command_authority_function);
}

/* Mask for the scheduling-authority comparison. Zero in the profile means "the
 * whole register", never a literal zero mask: a zero mask would compare every
 * possible reading equal and assert authority unconditionally. */
static inline uint16_t inverter_profile_command_authority_mask(
    const inverter_profile_t *profile)
{
    if (!profile || profile->command_authority_mask == 0U) return 0xFFFFU;
    return profile->command_authority_mask;
}

size_t inverter_profiles_count(void);
const inverter_profile_t *inverter_profiles_get(size_t index);
const inverter_profile_t *inverter_profiles_find(const char *id);
bool inverter_profile_allows_read(const inverter_profile_t *profile);

/* True only for a profile qualified to command physical equipment. Unchanged in
 * meaning: a lab-target declaration never makes this true. */
bool inverter_profile_allows_write(const inverter_profile_t *profile);

/* How far a command may go. Ordered by increasing authority, and FORBIDDEN is
 * zero so that a zeroed or uninitialised value denies the write. */
typedef enum {
    INVERTER_WRITE_FORBIDDEN = 0,
    /* Permitted only because an engineer declared this endpoint a simulator.
     * Never valid against physical equipment, and never a production release. */
    INVERTER_WRITE_LAB_ONLY,
    /* Permitted against physical equipment: the profile passed readback
     * qualification on real hardware. */
    INVERTER_WRITE_PRODUCTION
} inverter_write_permission_t;

/* Decides whether a power-limit command may be issued, and under what authority.
 *
 * A write requires a power-limit register AND its readback register in every
 * case: a command that cannot be read back cannot be confirmed, and an
 * unconfirmable command to a 100 kW machine is not a feature.
 *
 * A device that needs a prerequisite enable register requires that the profile
 * describe one AND describe how to read it back, for exactly the same reason and
 * with exactly the same force. A described-but-unreadable prerequisite is
 * FORBIDDEN, not "best effort".
 *
 * PRODUCTION additionally requires a profile that is not simulator-only and has
 * been qualified against physical hardware.
 *
 * LAB_ONLY requires the engineer's explicit per-inverter simulator declaration.
 * It deliberately does not also require a high qualification level, because
 * raising a documented register map to simulator-verified is precisely what lab
 * testing is for -- demanding the qualification first would make it
 * unobtainable. The declaration is the control, and it is a statement a human
 * must make about physical reality.
 *
 * Returns FORBIDDEN for a NULL profile. */
inverter_write_permission_t inverter_profile_write_permission(const inverter_profile_t *profile,
                                                              bool declared_lab_target);
const char *inverter_write_permission_label(inverter_write_permission_t permission);
const char *inverter_profile_qualification_label(inverter_profile_qualification_t qualification);
const char *inverter_profile_connection_label(inverter_profile_connection_t connection);
bool inverter_profile_has_status_register(const inverter_profile_t *profile);

#ifdef __cplusplus
}
#endif
