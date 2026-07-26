#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float active_power_kw;
    bool online;
    bool connection_initialized;
    uint32_t last_update_ms;
    uint32_t last_attempt_ms;
    uint32_t success_count;
    uint32_t response_errors;
    uint32_t consecutive_failures;
    int32_t last_error;
} meter_data_t;
