#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float rated_power_kw;
    float commanded_percent;
    float commanded_power_kw;
    bool online;
    uint32_t write_errors;
} inverter_data_t;
