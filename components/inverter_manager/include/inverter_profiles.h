#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    bool has_identity_probe;
    uint8_t identity_function;
    uint16_t identity_address;
    uint8_t identity_words;
    bool has_active_power;
    uint8_t active_power_function;
    uint16_t active_power_address;
    uint8_t active_power_words;
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
    float power_limit_readback_scale;
} inverter_profile_t;

size_t inverter_profiles_count(void);
const inverter_profile_t *inverter_profiles_get(size_t index);
const inverter_profile_t *inverter_profiles_find(const char *id);
bool inverter_profile_allows_read(const inverter_profile_t *profile);
bool inverter_profile_allows_write(const inverter_profile_t *profile);
const char *inverter_profile_qualification_label(inverter_profile_qualification_t qualification);
const char *inverter_profile_connection_label(inverter_profile_connection_t connection);

#ifdef __cplusplus
}
#endif
