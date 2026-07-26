#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float rated_power_kw;
    float commanded_percent;
    float commanded_power_kw;
    bool online;
    bool connection_initialized;
    bool has_command;
    uint32_t last_command_ms;
    uint32_t write_successes;
    uint32_t write_errors;
    int32_t last_error;

    float active_power_kw;
    bool telemetry_enabled;
    bool telemetry_online;
    uint32_t telemetry_last_update_ms;
    uint32_t telemetry_last_attempt_ms;
    uint32_t telemetry_successes;
    uint32_t telemetry_errors;
    uint32_t telemetry_consecutive_failures;
    int32_t telemetry_last_error;
} inverter_data_t;
