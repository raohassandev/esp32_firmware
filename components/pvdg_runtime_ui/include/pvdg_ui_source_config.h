#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PVDG_UI_SOURCE_SCHEMA 4U
#define PVDG_UI_SOURCE_MAX_METERS 4U
#define PVDG_UI_SOURCE_MAX_GENERATORS 3U
#define PVDG_UI_SOURCE_KW_MAX 1000000.0
#define PVDG_UI_SOURCE_POLL_MIN_MS 100U
#define PVDG_UI_SOURCE_POLL_MAX_MS 60000U
#define PVDG_UI_SOURCE_STALE_MAX_MS 600000U
#define PVDG_UI_SOURCE_GRID_LOSS_MAX_MS 60000U
#define PVDG_UI_SOURCE_GRID_RECOVERY_MAX_MS 600000U

typedef enum {
    PVDG_UI_SOURCE_ZERO_EXPORT = 0,
    PVDG_UI_SOURCE_LIMITED_EXPORT = 1,
    PVDG_UI_SOURCE_MINIMUM_IMPORT = 2
} pvdg_ui_source_policy_t;

typedef enum {
    PVDG_UI_SOURCE_IMPORT_POSITIVE = 0,
    PVDG_UI_SOURCE_EXPORT_POSITIVE = 1
} pvdg_ui_source_meter_orientation_t;

typedef struct {
    bool enabled;
    uint8_t meter_index;
    uint8_t function_code;
    uint16_t address;
    uint16_t mask;
    uint16_t active_value;
} pvdg_ui_source_signal_t;

typedef struct {
    double rated_kw;
    double minimum_loading_percent;
    double reserve_kw;
    double reverse_power_margin_kw;
    pvdg_ui_source_signal_t running;
    pvdg_ui_source_signal_t breaker_closed;
} pvdg_ui_source_generator_t;

typedef struct {
    uint16_t schema;
    pvdg_ui_source_policy_t policy;
    pvdg_ui_source_meter_orientation_t meter_orientation;
    double export_limit_kw;
    double minimum_import_kw;
    pvdg_ui_source_signal_t grid_available;
    pvdg_ui_source_signal_t grid_breaker_closed;
    uint32_t evidence_poll_interval_ms;
    uint32_t evidence_stale_timeout_ms;
    uint32_t grid_loss_trip_ms;
    uint32_t grid_recovery_stable_ms;
    pvdg_ui_source_generator_t generators[PVDG_UI_SOURCE_MAX_GENERATORS];
    pvdg_ui_source_signal_t transfer_active;
    pvdg_ui_source_signal_t grid_generator_synchronized;
} pvdg_ui_source_config_t;

typedef enum {
    PVDG_UI_SOURCE_CONFIG_OK = 0,
    PVDG_UI_SOURCE_CONFIG_INVALID_ARGUMENT,
    PVDG_UI_SOURCE_CONFIG_SCHEMA_MISMATCH,
    PVDG_UI_SOURCE_CONFIG_POLICY_INVALID,
    PVDG_UI_SOURCE_CONFIG_ORIENTATION_INVALID,
    PVDG_UI_SOURCE_CONFIG_LIMIT_INVALID,
    PVDG_UI_SOURCE_CONFIG_SIGNAL_INVALID,
    PVDG_UI_SOURCE_CONFIG_SIGNAL_PAIR_INVALID,
    PVDG_UI_SOURCE_CONFIG_TIMING_INVALID,
    PVDG_UI_SOURCE_CONFIG_GENERATOR_INVALID
} pvdg_ui_source_config_result_t;

void pvdg_ui_source_config_defaults(pvdg_ui_source_config_t *config);
pvdg_ui_source_config_result_t pvdg_ui_source_config_validate(const pvdg_ui_source_config_t *config, char *error, size_t error_size);
bool pvdg_ui_source_signal_valid(const pvdg_ui_source_signal_t *signal);
bool pvdg_ui_source_grid_evidence_complete(const pvdg_ui_source_config_t *config);
bool pvdg_ui_source_generator_evidence_complete(const pvdg_ui_source_config_t *config, uint8_t generator_index);

#ifdef __cplusplus
}
#endif
