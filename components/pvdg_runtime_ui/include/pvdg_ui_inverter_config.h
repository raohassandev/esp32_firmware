#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PVDG_UI_MAX_INVERTER_CONFIGS 12U
#define PVDG_UI_INVERTER_NAME_BYTES 24U
#define PVDG_UI_INVERTER_HOST_BYTES 64U
#define PVDG_UI_INVERTER_PROFILE_ID_BYTES 40U
#define PVDG_UI_MAX_INVERTER_PROFILES 24U
#define PVDG_UI_INVERTER_RATED_MAX_KW 100000.0

typedef struct {
    bool enabled;
    char name[PVDG_UI_INVERTER_NAME_BYTES];
    char host[PVDG_UI_INVERTER_HOST_BYTES];
    uint16_t port;
    uint8_t unit_id;
    uint32_t timeout_ms;
    double rated_kw;
} pvdg_ui_inverter_config_t;

typedef struct {
    uint8_t count;
    pvdg_ui_inverter_config_t inverters[PVDG_UI_MAX_INVERTER_CONFIGS];
} pvdg_ui_inverter_list_t;

typedef enum {
    PVDG_UI_PROFILE_DOCUMENTED = 0,
    PVDG_UI_PROFILE_SIMULATOR_VERIFIED,
    PVDG_UI_PROFILE_BENCH_VERIFIED,
    PVDG_UI_PROFILE_READ_ONLY_QUALIFIED,
    PVDG_UI_PROFILE_WRITE_QUALIFIED,
    PVDG_UI_PROFILE_PRODUCTION_APPROVED
} pvdg_ui_profile_qualification_t;

typedef struct {
    char id[PVDG_UI_INVERTER_PROFILE_ID_BYTES];
    char manufacturer[32];
    char model_family[64];
    char protocol[48];
    char connection[32];
    char manual_reference[128];
    char fingerprint[17];
    pvdg_ui_profile_qualification_t qualification;
    bool simulator_only;
    bool read_allowed;
    bool write_allowed;
    bool identity_probe_supported;
    bool active_power_supported;
    bool power_limit_supported;
    bool power_limit_readback_supported;
    bool status_register_supported;
    bool status_register_commissioning_required;
    double minimum_percent;
    double maximum_percent;
} pvdg_ui_inverter_profile_t;

typedef struct {
    uint8_t count;
    bool truncated;
    pvdg_ui_inverter_profile_t profiles[PVDG_UI_MAX_INVERTER_PROFILES];
} pvdg_ui_inverter_profile_catalog_t;

typedef struct {
    char profile_id[PVDG_UI_INVERTER_PROFILE_ID_BYTES];
    char fingerprint[17];
} pvdg_ui_inverter_profile_assignment_t;

typedef struct {
    pvdg_ui_inverter_profile_assignment_t assignments[PVDG_UI_MAX_INVERTER_CONFIGS];
} pvdg_ui_inverter_assignment_map_t;

typedef enum {
    PVDG_UI_INVERTER_CONFIG_OK = 0,
    PVDG_UI_INVERTER_CONFIG_INVALID_ARGUMENT,
    PVDG_UI_INVERTER_CONFIG_COUNT_INVALID,
    PVDG_UI_INVERTER_CONFIG_NAME_INVALID,
    PVDG_UI_INVERTER_CONFIG_ENDPOINT_INVALID,
    PVDG_UI_INVERTER_CONFIG_RATED_INVALID,
    PVDG_UI_INVERTER_CONFIG_DUPLICATE_ENDPOINT,
    PVDG_UI_INVERTER_PROFILE_INVALID
} pvdg_ui_inverter_config_result_t;

void pvdg_ui_inverter_defaults(pvdg_ui_inverter_config_t *inverter, uint8_t index);
pvdg_ui_inverter_config_result_t pvdg_ui_inverter_list_validate(const pvdg_ui_inverter_list_t *list, char *error, size_t error_size);
const pvdg_ui_inverter_profile_t *pvdg_ui_inverter_profile_find(const pvdg_ui_inverter_profile_catalog_t *catalog, const char *profile_id);
bool pvdg_ui_inverter_profile_assignment_valid(const pvdg_ui_inverter_profile_catalog_t *catalog, const char *profile_id);

#ifdef __cplusplus
}
#endif
