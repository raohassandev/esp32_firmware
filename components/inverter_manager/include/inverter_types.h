#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "inverter_status.h"

typedef struct {
    float rated_power_kw;
    float measured_power_kw;
    float commanded_percent;
    float commanded_power_kw;
    float readback_percent;
    bool online;
    bool connection_initialized;
    bool identity_supported;
    bool identity_verified;
    bool telemetry_supported;
    bool telemetry_valid;
    bool telemetry_stale;
    bool has_command;
    bool has_readback;
    bool command_mismatch;
    uint32_t last_telemetry_ms;
    uint32_t last_command_ms;
    uint32_t last_readback_ms;
    uint32_t read_successes;
    uint32_t read_errors;
    uint32_t consecutive_read_failures;
    uint32_t write_successes;
    uint32_t write_errors;
    uint32_t mismatch_count;
    int32_t last_error;

    /* Operational status, read-only and independent of the command path.
     * status_state defaults to INVERTER_STATE_UNKNOWN (0). */
    inverter_state_t status_state;
    bool status_supported;
    bool status_raw_valid;
    bool status_stale;
    uint32_t status_raw;
    uint32_t last_status_ms;
    uint32_t status_read_successes;
    uint32_t status_read_errors;
    int32_t status_last_error;
} inverter_data_t;
