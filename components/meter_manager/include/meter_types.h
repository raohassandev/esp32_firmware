#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float active_power_kw;
    bool online;
    uint32_t last_update_ms;
    uint32_t response_errors;
} meter_data_t;
