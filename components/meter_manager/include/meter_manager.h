#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "meter_types.h"

esp_err_t meter_manager_init(void);
bool meter_manager_get_data(uint8_t meter_index, meter_data_t *out_data);
