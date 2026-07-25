#pragma once
#include "esp_err.h"
#include "modbus_types.h"

esp_err_t modbus_tcp_connection_init(modbus_connection_t *connection, const modbus_endpoint_t *endpoint);
void modbus_tcp_connection_close(modbus_connection_t *connection);
esp_err_t modbus_tcp_read_registers(modbus_connection_t *connection, uint8_t function_code,
                                   uint16_t address, uint16_t count, uint16_t *registers);
esp_err_t modbus_tcp_write_single(modbus_connection_t *connection, uint16_t address, uint16_t value);
esp_err_t modbus_tcp_write_multiple(modbus_connection_t *connection, uint16_t address,
                                    const uint16_t *values, uint16_t count);
