#include "inverter_profiles.h"

#include <string.h>

#define LEGACY_SAFE_DEFAULT_PROFILE_ID "custom-advanced-modbus"
#define SAFE_DEFAULT_PROFILE_ID "custom.modbus-percent-v1"

/*
 * Profile entries are deliberately write-locked until the exact manual revision,
 * model family and command/readback sequence have been extracted and physically
 * qualified. A visible catalogue entry is not permission to write an inverter.
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
        .has_identity_probe = false,
        .has_active_power = false,
        .has_power_limit = false,
        .has_power_limit_readback = false,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "huawei.sun2000.pending",
        .manufacturer = "Huawei",
        .model_family = "SUN2000 family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .has_identity_probe = false,
        .has_active_power = false,
        .has_power_limit = false,
        .has_power_limit_readback = false,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
    },
    {
        .id = "goodwe.commercial.pending",
        .manufacturer = "GoodWe",
        .model_family = "Commercial inverter family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "SolTrix/Manuals — exact model/manual revision extraction pending",
        .has_identity_probe = false,
        .has_active_power = false,
        .has_power_limit = false,
        .has_power_limit_readback = false,
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
        .has_identity_probe = false,
        .has_active_power = false,
        .has_power_limit = false,
        .has_power_limit_readback = false,
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
        .has_identity_probe = false,
        .has_active_power = false,
        .has_power_limit = false,
        .has_power_limit_readback = false,
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
    return profile && profile->qualification >= INVERTER_PROFILE_QUALIFICATION_READ_ONLY_QUALIFIED;
}

bool inverter_profile_allows_write(const inverter_profile_t *profile)
{
    return profile && profile->has_power_limit && profile->has_power_limit_readback &&
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

const char *inverter_profile_connection_label(inverter_profile_connection_t connection)
{
    switch (connection) {
        case INVERTER_PROFILE_CONNECTION_MODBUS_TCP: return "Direct Modbus TCP";
        case INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY: return "Modbus RTU gateway";
        case INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY: return "Manufacturer logger/gateway";
        default: return "Unknown";
    }
}
