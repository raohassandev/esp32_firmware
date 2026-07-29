#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOURCE_MAX_GENERATORS 3U

typedef enum {
    SOURCE_MODE_UNKNOWN = 0,
    SOURCE_MODE_NO_SOURCE,
    SOURCE_MODE_GRID_ONLY,
    SOURCE_MODE_GENERATOR_ONLY,
    SOURCE_MODE_GRID_GENERATOR_SYNC,
    SOURCE_MODE_TRANSFER,
    SOURCE_MODE_ISLAND,
    SOURCE_MODE_CONFLICT
} source_mode_t;

/* Breaker and synchronisation evidence. Only a genset controller or dry contacts
 * can establish these; a power measurement cannot. Nothing in the power-following
 * path may populate these fields - see source_mode_from_measured_source(). */
typedef struct {
    bool evidence_fresh;
    bool transfer_active;
    bool grid_available;
    bool grid_breaker_closed;
    bool generator_running;
    bool generator_breaker_closed;
    bool grid_generator_synchronized;
} source_evidence_t;

/* Which source is carrying the plant, as established by measurement: a digital
 * source input, or grid and generator meter power. This is deliberately weaker
 * than source_evidence_t - it says which source is carrying load, and says
 * nothing about breaker position or synchronisation. */
typedef enum {
    MEASURED_SOURCE_UNKNOWN = 0,
    MEASURED_SOURCE_GRID,
    MEASURED_SOURCE_GENERATOR
} measured_source_t;

typedef struct {
    source_mode_t mode;
    bool control_allowed;
    bool transition_active;
    bool evidence_conflict;
} source_mode_result_t;

typedef struct {
    bool configured;
    bool evidence_fresh;
    bool running;
    bool breaker_closed;
    float rated_kw;
    float measured_kw;
    float minimum_loading_percent;
    float reserve_kw;
    float reverse_power_margin_kw;
} generator_channel_evidence_t;

typedef struct {
    bool valid;
    bool conflict;
    uint8_t running_count;
    float running_rated_kw;
    float measured_total_kw;
    float required_minimum_kw;
} generator_fleet_result_t;

typedef struct {
    bool evidence_fresh;
    float facility_load_kw;
    float running_generator_rated_kw;
    float minimum_loading_percent;
    float reserve_kw;
    float reverse_power_margin_kw;
} generator_limit_input_t;

source_mode_result_t source_mode_evaluate(const source_evidence_t *evidence);

/* Maps a measured source identity onto a source mode for the power-following
 * topology, where no genset controller is integrated.
 *
 * It can only ever return GRID_ONLY, GENERATOR_ONLY or UNKNOWN. It never returns
 * GRID_GENERATOR_SYNC: measurement cannot prove two sources are synchronised, and
 * claiming it could close PV onto an unsynchronised bus. Stale evidence and an
 * unknown source both fail closed. */
source_mode_result_t source_mode_from_measured_source(measured_source_t source,
                                                      bool evidence_fresh);
generator_fleet_result_t source_mode_aggregate_generators(
    const generator_channel_evidence_t channels[SOURCE_MAX_GENERATORS]);

/* Returns the largest safe aggregate PV command while generators are carrying
 * the plant bus. Invalid, stale, conflicting, or non-finite inputs fail closed
 * to zero. */
float source_mode_generator_safe_pv_kw(const generator_limit_input_t *input);

const char *source_mode_name(source_mode_t mode);

#ifdef __cplusplus
}
#endif
