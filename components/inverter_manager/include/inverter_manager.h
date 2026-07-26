#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "inverter_types.h"

esp_err_t inverter_manager_init(void);
uint8_t inverter_manager_get_count(void);
float inverter_manager_get_total_rated_kw(void);
esp_err_t inverter_manager_set_total_power_kw(float target_kw);
bool inverter_manager_get_data(uint8_t inverter_index, inverter_data_t *out_data);
