#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inverter_profile_decode.h"
#include "inverter_status.h"

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

typedef struct {
    const char *id;
    const char *manufacturer;
    const char *model_family;
    const char *protocol;
    inverter_profile_connection_t connection;
    inverter_profile_qualification_t qualification;
    const char *manual_reference;
    bool simulator_only;

    bool has_identity_probe;
    uint8_t identity_function;
    uint16_t identity_address;
    uint8_t identity_words;
    uint32_t identity_expected;
    uint32_t identity_mask;

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
