#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "modbus_types.h"

#define PROFILE_KEY_MAX_LEN 32

typedef struct {
    char key[PROFILE_KEY_MAX_LEN];
    uint8_t function_code;
    uint16_t address;
    modbus_data_type_t data_type;
    modbus_word_order_t word_order;
    float scale;
    float offset;
    uint32_t poll_interval_ms;
    bool writable;
} register_point_t;
