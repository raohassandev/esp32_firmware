#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float rated_power_kw;
    float commanded_percent;
    float commanded_power_kw;
    bool online;
    bool has_command;
    uint32_t last_command_ms;
    uint32_t write_successes;
    uint32_t write_errors;
    int32_t last_error;
} inverter_data_t;
