#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float active_power_kw;
    bool online;
    bool degraded;
    bool connection_initialized;
    uint32_t last_update_ms;
    uint32_t last_attempt_ms;
    uint32_t last_response_time_ms;
    uint32_t success_count;
    uint32_t response_errors;
    uint32_t consecutive_failures;
    uint32_t current_poll_delay_ms;
    uint8_t recent_sample_count;
    uint8_t recent_success_percent;
    int32_t last_error;
    bool last_modbus_exception_valid;
    uint8_t last_modbus_exception_function;
    uint8_t last_modbus_exception_code;
    uint32_t modbus_exception_count;
} meter_data_t;
