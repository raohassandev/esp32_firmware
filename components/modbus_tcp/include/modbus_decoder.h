#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "modbus_types.h"

size_t modbus_data_register_count(modbus_data_type_t type);
esp_err_t modbus_decode_scaled(const uint16_t *registers, size_t register_count,
                               modbus_data_type_t type, modbus_word_order_t order,
                               float scale, float offset, float *out_value);

/** Decode four high-word-first Modbus registers as one unsigned 64-bit value.
 *
 * The scaled output is double precision so cumulative energy counters do not
 * lose integer resolution through the existing float-based measurement path.
 */
esp_err_t modbus_decode_u64_be_scaled(const uint16_t *registers,
                                      size_t register_count,
                                      double scale,
                                      double offset,
                                      double *out_value);
