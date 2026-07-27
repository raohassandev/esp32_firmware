#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "inverter_types.h"

#define INVERTER_PROBE_MAX_REGISTERS 8

typedef struct {
    bool profile_read_allowed;
    bool connection_initialized;
    bool identity_attempted;
    bool identity_ok;
    uint8_t identity_count;
    uint16_t identity_registers[INVERTER_PROBE_MAX_REGISTERS];
    esp_err_t identity_error;
    bool active_power_attempted;
    bool active_power_ok;
    uint8_t active_power_count;
    uint16_t active_power_registers[INVERTER_PROBE_MAX_REGISTERS];
    esp_err_t active_power_error;
} inverter_probe_result_t;

esp_err_t inverter_manager_init(void);
uint8_t inverter_manager_get_count(void);
float inverter_manager_get_total_rated_kw(void);
esp_err_t inverter_manager_set_total_power_kw(float target_kw);
esp_err_t inverter_manager_probe_read_only(uint8_t inverter_index,
                                           inverter_probe_result_t *result);
bool inverter_manager_get_data(uint8_t inverter_index, inverter_data_t *out_data);
