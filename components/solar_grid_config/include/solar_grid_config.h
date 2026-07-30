#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLAR_GRID_CONFIG_MAGIC 0x53475244u
#define SOLAR_GRID_CONFIG_VERSION 5u

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

/*
 * How the engines on the bus divide the plant's kW load. Mirrors
 * generator_sharing_mode_t in control_engine/include/generator_fleet_limit.h value
 * for value; the two are kept numerically identical by _Static_asserts in the
 * control engine, because the control engine copies this value straight across.
 * Declared separately so this component keeps depending on nothing.
 *
 * UNSET is zero. There is no safe default: base-load sharing can put the floor
 * either above or below the isochronous floor depending on the commissioned
 * setpoints, so no mode dominates and no mode may be assumed. An uncommissioned
 * mode keeps the commissioning gate closed for a plant that can run two or more
 * engines. DROOP is stored and reported but REFUSED by the control engine, which is
 * documented at the enum it mirrors.
 */
typedef enum {
    SOLAR_GRID_LOAD_SHARING_UNSET = 0,
    SOLAR_GRID_LOAD_SHARING_ISOCHRONOUS,
    SOLAR_GRID_LOAD_SHARING_BASE_LOAD,
    SOLAR_GRID_LOAD_SHARING_DROOP,
    SOLAR_GRID_LOAD_SHARING_COUNT
} solar_grid_load_sharing_t;

/* One engine's part in a base-load plant. Mirrors generator_engine_role_t. Read
 * only when the sharing mode is BASE_LOAD. */
typedef enum {
    SOLAR_GRID_ENGINE_ROLE_UNSET = 0,
    SOLAR_GRID_ENGINE_ROLE_SWING,
    SOLAR_GRID_ENGINE_ROLE_BASE_LOAD,
    SOLAR_GRID_ENGINE_ROLE_COUNT
} solar_grid_engine_role_t;

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
    /* Appended in schema 4: the kW load-sharing mode and what base-load sharing
     * needs in order to be computable. Kept last so schema 3 remains a byte-exact
     * prefix and a commissioned unit is upgraded by appending zeroes.
     *
     * Zero throughout means UNSET, which is what an upgraded unit gets. That is the
     * honest state -- nobody has told this controller how the plant shares load --
     * and it keeps the gate closed for a plant that can run two or more engines
     * while leaving a single-engine site behaving exactly as before, because with
     * one engine on the bus there is no load to share.
     *
     * WHY THESE ARE NOT FIELDS OF solar_grid_generator_limits_t. That struct is
     * embedded in generator_extra, so appending to it would move and resize the
     * schema-3 region and change sizeof(solar_grid_config_t) in a way no stored blob
     * could be matched against. These live in parallel arrays instead, indexed by
     * engine slot 0..SOLAR_GRID_MAX_GENERATORS-1 directly -- unlike generator_extra,
     * which is offset by one. Read them through the accessors below. */
    uint8_t load_sharing_mode; /* solar_grid_load_sharing_t */
    uint8_t engine_role[SOLAR_GRID_MAX_GENERATORS]; /* solar_grid_engine_role_t */
    float engine_base_load_kw[SOLAR_GRID_MAX_GENERATORS];
    /* Appended in schema 5: the tolerance within which a base-loaded engine's
     * measured power must agree with its commissioned setpoint. Kept last so schema 4
     * remains a byte-exact prefix and a commissioned unit is upgraded by appending
     * zeroes.
     *
     * ZERO IN BOTH MEANS NOT COMMISSIONED, and that is what an upgraded unit gets.
     * NO MANUAL, NAMEPLATE OR SITE DOCUMENT IN THIS REPOSITORY STATES A TOLERANCE, so
     * none is invented here or anywhere else. The consequence is stated at the control
     * engine's generator_fleet_input_t and enforced by the commissioning gate:
     * base-load sharing with at least one base-loaded engine cannot be commissioned
     * until a tolerance is supplied. Every other plant is untouched -- an isochronous
     * plant never reads these fields, a base-load plant with no base-loaded engine has
     * no setpoint to check, and a single-engine site is unaffected.
     *
     * Either may be stated, or both; the CONTROL ENGINE owns the rule for combining
     * them (the narrower band wins, for the reason given there). Nothing is interpreted
     * in this component beyond bounds. */
    float base_load_tolerance_kw;
    float base_load_tolerance_percent_of_rating;
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

/* The commissioned kW load-sharing mode. A NULL configuration, or a stored value
 * this build does not recognise, reads as SOLAR_GRID_LOAD_SHARING_UNSET -- never as
 * a supported mode. */
uint8_t solar_grid_config_load_sharing_mode(const solar_grid_config_t *config);

/* One engine slot's base-load role. A NULL configuration, an out-of-range slot or
 * an unrecognised stored value reads as SOLAR_GRID_ENGINE_ROLE_UNSET. */
uint8_t solar_grid_config_engine_role(const solar_grid_config_t *config, uint8_t slot);

/* One engine slot's commissioned fixed kW setpoint. Zero means "not commissioned",
 * which is what the control engine refuses for a base-loaded engine. A NULL
 * configuration or an out-of-range slot yields zero. */
float solar_grid_config_engine_base_load_kw(const solar_grid_config_t *config, uint8_t slot);

/* The commissioned base-load setpoint-agreement tolerance, absolute kW and percent of
 * the engine's own rating. Zero means "not commissioned" for either, which is what an
 * upgraded unit holds and what keeps a base-loaded plant out of commissioning until an
 * engineer supplies a figure. A NULL configuration, or a value that is not a usable
 * tolerance (non-finite, negative, or a percentage above 100), reads as zero: an
 * unreadable tolerance must never present itself as a commissioned one. */
float solar_grid_config_base_load_tolerance_kw(const solar_grid_config_t *config);
float solar_grid_config_base_load_tolerance_percent(const solar_grid_config_t *config);

esp_err_t solar_grid_config_init(void);
esp_err_t solar_grid_config_get_snapshot(solar_grid_config_t *out_config);
esp_err_t solar_grid_config_save(const solar_grid_config_t *config);
void solar_grid_config_defaults(solar_grid_config_t *config);
bool solar_grid_config_valid(const solar_grid_config_t *config);
bool solar_grid_config_evidence_complete(const solar_grid_config_t *config);
const char *solar_grid_policy_name(solar_grid_policy_t policy);
const char *solar_grid_orientation_name(solar_grid_meter_orientation_t orientation);
/* Stable lowercase slugs. An unrecognised value reads as "unset". */
const char *solar_grid_load_sharing_name(uint8_t mode);
const char *solar_grid_engine_role_name(uint8_t role);

#ifdef __cplusplus
}
#endif
