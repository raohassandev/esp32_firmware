#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "meter_types.h"

esp_err_t meter_manager_init(void);
uint8_t meter_manager_get_count(void);
bool meter_manager_get_data(uint8_t meter_index, meter_data_t *out_data);

/** Perform a read-only FC03/FC04 transaction through the configured meter's
 * existing serialized Modbus connection. This prevents commissioning and
 * telemetry requests from creating competing TCP sessions to the gateway. */
esp_err_t meter_manager_read_registers(uint8_t meter_index,
                                       uint8_t function_code,
                                       uint16_t address,
                                       uint16_t count,
                                       uint16_t *registers);
