#pragma once

/*
 * Normalised inverter operational state.
 *
 * SAFETY DESIGN: INVERTER_STATE_UNKNOWN is value 0, so a zero-initialised
 * profile, runtime record or telemetry sample reports UNKNOWN. Every failure
 * mode - status register not configured, Modbus read failed, raw value not
 * present in the profile mapping table, or the last good sample being older
 * than the profile stale timeout - collapses to UNKNOWN. UNKNOWN never
 * satisfies the synchronisation predicate, so the controller can only take the
 * direct (no-ramp) grid path on positive, fresh evidence that every enabled
 * inverter is actually on grid.
 *
 * Status is READ-ONLY. Only Modbus function code 3 (holding registers) and 4
 * (input registers) may ever be used to sample it; reading an operational
 * state must never write to an inverter.
 */

#include <stdbool.h>
#include <stdint.h>

#include "inverter_profile_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INVERTER_STATE_UNKNOWN = 0,
    INVERTER_STATE_OFFLINE,
    INVERTER_STATE_STANDBY,
    INVERTER_STATE_CHECKING,
    INVERTER_STATE_ON_GRID,
    INVERTER_STATE_DERATED,
    INVERTER_STATE_FAULT
} inverter_state_t;

#define INVERTER_STATUS_FUNCTION_HOLDING 3U
#define INVERTER_STATUS_FUNCTION_INPUT 4U
#define INVERTER_STATUS_MAX_MAPPINGS 12U
#define INVERTER_STATUS_MAX_WORDS 2U

typedef struct {
    uint32_t raw_value;
    uint32_t raw_mask; /* 0 means "compare the whole raw value" */
    inverter_state_t state;
} inverter_status_mapping_t;

/*
 * Optional per-profile status register description. `configured` stays false
 * for every shipped manufacturer profile until a commissioning engineer
 * supplies a manual-verified address, function code, word layout and raw ->
 * state mapping. An unconfigured register always yields UNKNOWN.
 */
typedef struct {
    bool configured;
    uint8_t function; /* 3 or 4 only - validated, never trusted blindly */
    uint16_t address;
    uint8_t words;
    inverter_value_type_t type;
    inverter_word_order_t word_order;
    uint8_t mapping_count;
    inverter_status_mapping_t mappings[INVERTER_STATUS_MAX_MAPPINGS];
} inverter_status_register_t;

/* One inverter's contribution to the fleet synchronisation decision. */
typedef struct {
    bool enabled;
    bool sample_fresh;
    inverter_state_t state;
} inverter_status_sample_t;

/* True only for Modbus read function codes 3 and 4. */
bool inverter_status_function_is_read_only(uint8_t function_code);

/* True only when the register description is complete and safe to poll. */
bool inverter_status_register_is_configured(const inverter_status_register_t *status_register);

/* Assemble the raw register value honouring the profile word order. */
bool inverter_status_decode_raw(const inverter_status_register_t *status_register,
                                const uint16_t *registers,
                                uint8_t register_count,
                                uint32_t *raw_value);

/* Map a raw value to a normalised state; unmapped raw values yield UNKNOWN. */
inverter_state_t inverter_status_map_raw(const inverter_status_register_t *status_register,
                                         uint32_t raw_value);

/*
 * The single honest funnel used by the acquisition path. Returns UNKNOWN when
 * the register is unconfigured, the read failed, the raw value is unmapped or
 * the sample is older than `stale_timeout_ms`.
 */
inverter_state_t inverter_status_evaluate(const inverter_status_register_t *status_register,
                                          bool read_succeeded,
                                          uint32_t raw_value,
                                          uint32_t sample_age_ms,
                                          uint32_t stale_timeout_ms);

const char *inverter_state_label(inverter_state_t state);

/* Synchronised means exactly ON_GRID. Nothing else, and never UNKNOWN. */
bool inverter_state_is_synchronised(inverter_state_t state);

/*
 * True only when at least one inverter is enabled and every enabled inverter
 * reports ON_GRID from a fresh sample. Any UNKNOWN, any stale sample, or an
 * empty fleet returns false.
 */
bool inverter_status_fleet_synchronised(const inverter_status_sample_t *samples, uint8_t count);

#ifdef __cplusplus
}
#endif
