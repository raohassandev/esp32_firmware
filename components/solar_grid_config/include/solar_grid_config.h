#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLAR_GRID_CONFIG_MAGIC 0x53475244u
#define SOLAR_GRID_CONFIG_VERSION 3u

/* Engine slots the generator policy can describe. Must equal APP_MAX_GENERATORS
 * in config_manager/include/config_types.h, so a meter's generator_index and a
 * commissioned engine rating address the same set of machines. Checked by a
 * _Static_assert in solar_grid_config.c. */
#define SOLAR_GRID_MAX_GENERATORS 3u

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

/* One engine's protection numbers.
 *
 * Every field is a measured or nameplate quantity. None of them has a default
 * and none may be guessed: a rating that is wrong in the optimistic direction
 * lets PV be commanded past what the machine can carry, which is the reverse
 * power condition this product exists to prevent. `enabled` means "this engine
 * slot is in service at this site", and an enabled slot with an uncommissioned
 * rating is what the commissioning gate refuses. */
typedef struct {
    bool enabled;
    float rated_kw;
    float minimum_loading_percent;
    float reserve_kw;
    float reverse_power_margin_kw;
} solar_grid_generator_limits_t;

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
    /* Appended in schema 3: the SECOND and subsequent engine slots, for sites
     * that can run up to SOLAR_GRID_MAX_GENERATORS gensets in parallel.
     *
     * WHY THE FIRST SLOT IS NOT IN THIS ARRAY. Slot 0's numbers remain the four
     * scalar fields above. That keeps schema 2 a byte-exact prefix of schema 3,
     * so a commissioned single-generator unit is upgraded by appending zeroes and
     * cannot lose its rating, and it keeps the already-shipped
     * POST /api/solar-grid/config body meaning exactly what it meant before.
     * Index i in this array is engine slot i + 1.
     *
     * NOTHING SHOULD READ EITHER HALF DIRECTLY. Call
     * solar_grid_config_generator(), which presents all
     * SOLAR_GRID_MAX_GENERATORS slots as one uniform sequence. */
    solar_grid_generator_limits_t generator_extra[SOLAR_GRID_MAX_GENERATORS - 1u];
} solar_grid_config_t;

/* Uniform per-slot view of the generator policy.
 *
 * Slot 0 is synthesised from the legacy scalar fields, and is `enabled` exactly
 * when its rating is positive -- which is precisely what "commissioned" has
 * always meant for the single-generator configuration, so an upgraded unit
 * behaves as it did before. Slots 1.. carry their own `enabled` flag.
 *
 * A NULL configuration or an out-of-range slot yields a zeroed, disabled slot:
 * an unreadable engine is never reported as a commissioned one. */
solar_grid_generator_limits_t solar_grid_config_generator(const solar_grid_config_t *config,
                                                          uint8_t slot);

/* How many engine slots are in service. Zero means the generator policy is not
 * commissioned at all. */
uint8_t solar_grid_config_enabled_generator_count(const solar_grid_config_t *config);

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
