#include "inverter_profiles.h"

#include <string.h>

#define LEGACY_SAFE_DEFAULT_PROFILE_ID "custom-advanced-modbus"
#define SAFE_DEFAULT_PROFILE_ID "custom.modbus-percent-v1"

/*
 * Manufacturer profiles remain deliberately write-locked until exact manuals
 * and physical command/readback evidence exist. The SolTrix entries below are
 * simulator-only contracts. Their addresses must never be reused as production
 * manufacturer evidence.
 *
 * No profile below configures `status_register`. Operational status addresses
 * are NOT guessed: the Huawei, Solis and FoxESS/Knox manuals are not present in
 * this tree, and a plausible-looking wrong address could report "on grid" while
 * an inverter is still checking or faulted, which would let the controller
 * command full output directly. Every profile therefore leaves the status
 * register unconfigured and every inverter reports INVERTER_STATE_UNKNOWN until
 * a commissioning engineer supplies a verified description.
 */
static const inverter_profile_t PROFILES[] = {
    {
        .id = SAFE_DEFAULT_PROFILE_ID,
        .manufacturer = "Custom",
        .model_family = "Advanced Modbus percentage control",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Operator-supplied verified register map required",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    /*
     * Contract for the SolTrix ESP firmware lab simulator (huawei-v3 profile),
     * which models the Huawei SUN2000 register layout behind a closed-loop plant
     * model. Every field below was measured against the running simulator, not
     * copied from a manual and not copied from the older v1 entry, whose probe
     * addresses (40000, 40010) this simulator does not implement at all.
     *
     * Measured 2026-07-30 against config.esp-firmware-lab.json, unit 1:
     *   FC03 30000  -> "SUN2000-SIM"; first word 0x5355 ("SU") is the identity
     *   FC03 32080  -> I32 watts, high word first (85102 = 85.102 kW)
     *   FC03 32089  -> operating state; the simulator reports 0x0200 on grid
     *   FC06 40125  -> percentage limit, raw = percent x 10
     *   FC03 40125  -> readback of the ACTIVE limit, percent x 10
     *
     * IMPORTANT, and the reason this profile exists separately from any
     * manufacturer entry: the simulator applies a 40125 write ~1500 ms LATER,
     * and until then the readback still reports the previous active limit. A
     * correct controller must therefore treat that window as pending rather than
     * as a mismatch. Whether real SUN2000 hardware behaves the same way is NOT
     * established here and must be confirmed on site.
     *
     * simulator_only stays true: this validates the controller's logic against a
     * model, which is not evidence about physical equipment. It must never be
     * promoted, and must never be used as a manufacturer register reference.
     */
    {
        .id = "soltrix.sim.huawei.v3",
        .manufacturer = "SolTrix Simulator",
        .model_family = "Huawei SUN2000 layout, closed-loop plant model",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "Measured against SolTrix inverter-simulator huawei-v3; "
                            "not a manufacturer manual",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 30000,
        .identity_words = 1,
        /* First two characters of the model string, "SU". */
        .identity_expected = 0x5355,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 32080,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 40125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 40125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_S16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        /* Measured: this simulator defers a 40125 write by ~1500 ms and reports
         * the previous active limit until then. 2500 ms leaves margin without
         * approaching the 5000 ms confirmation deadline. */
        .power_limit_settle_ms = 2500,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "soltrix.sim.huawei.v1",
        .manufacturer = "SolTrix Simulator",
        .model_family = "Huawei SUN2000 contract",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "SolTrix commit fe84696 simulator contract; synthetic Modbus map",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 40000,
        .identity_words = 1,
        .identity_expected = 0xA021,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 40010,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 40125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 40125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "soltrix.sim.goodwe.v1",
        .manufacturer = "SolTrix Simulator",
        .model_family = "GoodWe commercial contract",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "SolTrix ESP32 integration simulator; synthetic map",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 41000,
        .identity_words = 1,
        .identity_expected = 0xA022,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 41010,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 41125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 41125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "soltrix.sim.solis.v1",
        .manufacturer = "SolTrix Simulator",
        .model_family = "Solis commercial contract",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "SolTrix ESP32 integration simulator; synthetic map",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 42000,
        .identity_words = 1,
        .identity_expected = 0xA023,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 42010,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 42125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 42125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "huawei.sun2000.pending",
        .manufacturer = "Huawei",
        .model_family = "SUN2000 family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY,
        /* DOCUMENTED, not qualified. Every field below is transcribed from the
         * manufacturer manual and NONE of it has been exercised against physical
         * hardware, so the production write gate still refuses this profile. It
         * becomes commandable only against an endpoint an engineer has declared a
         * simulator, which is how the map gets validated before anyone travels.
         * Raising this profile to production approval requires physical readback
         * evidence from the machine itself.
         *
         * Source: "Huawei Inverter Modbus Interface Definitions (V3.0)",
         * Issue 01 (2023-01-17).
         *
         * Addressing: the manual's decimal addresses are the raw on-the-wire
         * register addresses, not 1-based tag numbers. It states a command
         * "register address: 40200/0X9D08", and 0x9D08 == 40200, so the decimal
         * values are used directly with no offset. This is the trap that has bitten
         * this project before, and it is settled by that line.
         *
         * Register 1 "Model", RO STR, 30000, 15 registers ("Nameplate name of
         * machine"). The identity probe reads the first word only: "SU" = 0x5355,
         * the start of every SUN2000 nameplate.
         *
         * Signal 171 "active power", RO I32, unit kW, gain 1000, 32080, 2
         * registers. Gain 1000 means raw watts, hence scale 0.001.
         *
         * Signal 409 "Active Power Percentage Derating [Low Precision]", RW I16,
         * unit %, gain 10, 40125, 1 register, described as the "Active fine
         * adjustment interface". Used here as the command register because it is
         * the conventional third-party derating interface.
         *
         * NOT YET DECIDED, and a site-verification item: signal 419 "Active Power
         * Percentage Control [Low Precision]", RW I16, %, gain 10, 40199, is
         * described by Huawei as the interface "used in distributed mode ... in
         * anti-backcurrent control to control the upper limit of the output active
         * power". Anti-backcurrent is exactly this product's application, so 40199
         * may be the more correct register. Both encode percent x10. The lab
         * simulator applies 40125 with a delay and 40199 immediately, which hints
         * that 40199 is the control-rate interface, but a hint from a model is not
         * evidence. Confirm on the physical machine before promoting either.
         *
         * Signal 432 "active power gradient", RW U32, unit %/s, gain 1000, 42017,
         * 2 registers, "Limiting the speed of power change". The inverter has its
         * own ramp limiter; this firmware ramps in the control engine and does not
         * write it. Worth reconciling on site so the two do not fight.
         *
         * Signal 178 "Device Status", RO E16, at 32089, 1 register. The manual
         * defers the code table to a separate "Inverter Key Signal Extension
         * Description" that is NOT among the manuals available, so the meaning of
         * the codes is unknown and no operating-state description is set here.
         * Every inverter therefore reports INVERTER_STATE_UNKNOWN rather than a
         * guessed state.
         *
         * No settle or response time for a percentage command is documented
         * anywhere in this manual, so power_limit_settle_ms is left at the
         * firmware default and MUST be measured during commissioning. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Huawei Inverter Modbus Interface Definitions (V3.0), "
                            "Issue 01 (2023-01-17); not qualified on hardware",
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 30000,
        .identity_words = 1,
        .identity_expected = 0x5355, /* "SU" */
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 32080,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 40125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 40125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_S16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "goodwe.commercial.pending",
        .manufacturer = "GoodWe",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "solis.commercial.pending",
        .manufacturer = "Solis",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "foxess.commercial.pending",
        .manufacturer = "FoxESS / Knox",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
};

size_t inverter_profiles_count(void)
{
    return sizeof(PROFILES) / sizeof(PROFILES[0]);
}

const inverter_profile_t *inverter_profiles_get(size_t index)
{
    return index < inverter_profiles_count() ? &PROFILES[index] : NULL;
}

const inverter_profile_t *inverter_profiles_find(const char *id)
{
    if (!id || !id[0]) return NULL;
    if (strcmp(id, LEGACY_SAFE_DEFAULT_PROFILE_ID) == 0) id = SAFE_DEFAULT_PROFILE_ID;
    for (size_t index = 0; index < inverter_profiles_count(); ++index) {
        if (strcmp(PROFILES[index].id, id) == 0) return &PROFILES[index];
    }
    return NULL;
}

bool inverter_profile_allows_read(const inverter_profile_t *profile)
{
    if (!profile) return false;
    if (profile->simulator_only) {
        return profile->qualification >= INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED;
    }
    return profile->qualification >= INVERTER_PROFILE_QUALIFICATION_READ_ONLY_QUALIFIED;
}

bool inverter_profile_allows_write(const inverter_profile_t *profile)
{
    return profile && !profile->simulator_only && profile->has_power_limit &&
           profile->has_power_limit_readback &&
           profile->qualification == INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
}

inverter_write_permission_t inverter_profile_write_permission(const inverter_profile_t *profile,
                                                              bool declared_lab_target)
{
    if (!profile) return INVERTER_WRITE_FORBIDDEN;
    /* Confirmability is not negotiable in either mode: without a readback there
     * is no way to tell a command that landed from one that was ignored. */
    if (!profile->has_power_limit || !profile->has_power_limit_readback) {
        return INVERTER_WRITE_FORBIDDEN;
    }
    if (inverter_profile_allows_write(profile)) return INVERTER_WRITE_PRODUCTION;
    if (declared_lab_target) return INVERTER_WRITE_LAB_ONLY;
    return INVERTER_WRITE_FORBIDDEN;
}

const char *inverter_write_permission_label(inverter_write_permission_t permission)
{
    switch (permission) {
        case INVERTER_WRITE_PRODUCTION: return "production";
        case INVERTER_WRITE_LAB_ONLY: return "lab_simulator_only";
        case INVERTER_WRITE_FORBIDDEN: default: return "forbidden";
    }
}

const char *inverter_profile_qualification_label(inverter_profile_qualification_t qualification)
{
    static const char *const LABELS[] = {
        "Documented",
        "Simulator verified",
        "Bench verified",
        "Read-only qualified",
        "Write qualified",
        "Production approved",
    };
    return qualification <= INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED
        ? LABELS[qualification]
        : "Unknown";
}

bool inverter_profile_has_status_register(const inverter_profile_t *profile)
{
    return profile && inverter_status_register_is_configured(&profile->status_register);
}

const char *inverter_profile_connection_label(inverter_profile_connection_t connection)
{
    switch (connection) {
        case INVERTER_PROFILE_CONNECTION_MODBUS_TCP: return "Direct Modbus TCP";
        case INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY: return "Modbus RTU gateway";
        case INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY: return "Manufacturer logger/gateway";
        default: return "Unknown";
    }
}
