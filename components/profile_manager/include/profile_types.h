#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "modbus_types.h"

#define PROFILE_KEY_MAX_LEN 32
#define PROFILE_MAX_INVERTERS 12
#define INVERTER_TELEMETRY_PROFILE_MAGIC 0x49545046u
#define INVERTER_TELEMETRY_PROFILE_VERSION 1u

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

typedef struct {
    bool enabled;
    register_point_t active_power;
} inverter_telemetry_profile_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    inverter_telemetry_profile_t inverters[PROFILE_MAX_INVERTERS];
} inverter_telemetry_profile_set_t;
