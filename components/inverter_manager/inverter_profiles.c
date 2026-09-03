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
        .protocol = "Modbus (transport unqualified)",
        .connection = INVERTER_PROFILE_CONNECTION_UNQUALIFIED,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "goodwe.commercial.pending",
        .manufacturer = "GoodWe",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus (transport unqualified)",
        .connection = INVERTER_PROFILE_CONNECTION_UNQUALIFIED,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "solis.commercial.pending",
        .manufacturer = "Solis",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus (transport unqualified)",
        .connection = INVERTER_PROFILE_CONNECTION_UNQUALIFIED,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "foxess.commercial.pending",
        .manufacturer = "FoxESS / Knox",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus (transport unqualified)",
        .connection = INVERTER_PROFILE_CONNECTION_UNQUALIFIED,
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
    return profile && !profile->simulator_only && profile->has_identity_probe &&
           profile->has_power_limit && profile->has_power_limit_readback &&
           profile->qualification == INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
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
        case INVERTER_PROFILE_CONNECTION_UNQUALIFIED: return "Unqualified — verify manufacturer transport";
        default: return "Unknown";
    }
}
