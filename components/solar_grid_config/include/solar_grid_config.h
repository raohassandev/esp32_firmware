#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLAR_GRID_CONFIG_MAGIC 0x53475244u
#define SOLAR_GRID_CONFIG_VERSION 2u

typedef enum {
    SOLAR_GRID_POLICY_ZERO_EXPORT = 0,
    SOLAR_GRID_POLICY_LIMITED_EXPORT,
    SOLAR_GRID_POLICY_MINIMUM_IMPORT
} solar_grid_policy_t;

typedef enum {
    SOLAR_GRID_IMPORT_POSITIVE = 0,
    SOLAR_GRID_EXPORT_POSITIVE
} solar_grid_meter_orientation_t;

typedef struct {
    bool enabled;
    uint8_t meter_index;
    uint8_t function_code;
    uint16_t address;
    uint16_t mask;
    uint16_t active_value;
} solar_grid_signal_config_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    solar_grid_policy_t policy;
    solar_grid_meter_orientation_t meter_orientation;
    float export_limit_kw;
    float minimum_import_kw;
    solar_grid_signal_config_t grid_available;
    solar_grid_signal_config_t grid_breaker_closed;
    uint32_t evidence_poll_interval_ms;
    uint32_t evidence_stale_timeout_ms;
    uint32_t grid_loss_trip_ms;
    uint32_t grid_recovery_stable_ms;
    /* Appended in schema 2: generator limits for the power-following topology.
     * Kept last so schema 1 remains a byte-exact prefix.
     *
     * generator_rated_kw of zero means "not commissioned" and keeps PV at zero
     * whenever the generator is carrying the plant. There is no safe default
     * rating: guessing one would let PV be commanded against a machine whose
     * capacity is unknown. */
    float generator_rated_kw;
    float generator_minimum_loading_percent;
    float generator_reserve_kw;
    float generator_reverse_power_margin_kw;
} solar_grid_config_t;

esp_err_t solar_grid_config_init(void);
esp_err_t solar_grid_config_get_snapshot(solar_grid_config_t *out_config);
esp_err_t solar_grid_config_save(const solar_grid_config_t *config);
void solar_grid_config_defaults(solar_grid_config_t *config);
bool solar_grid_config_valid(const solar_grid_config_t *config);
bool solar_grid_config_evidence_complete(const solar_grid_config_t *config);
const char *solar_grid_policy_name(solar_grid_policy_t policy);
const char *solar_grid_orientation_name(solar_grid_meter_orientation_t orientation);

#ifdef __cplusplus
}
#endif
