#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "modbus_types.h"

size_t modbus_data_register_count(modbus_data_type_t type);
esp_err_t modbus_decode_scaled(const uint16_t *registers, size_t register_count,
                               modbus_data_type_t type, modbus_word_order_t order,
                               float scale, float offset, float *out_value);
