#pragma once
#include "esp_err.h"
#include "modbus_types.h"

esp_err_t modbus_tcp_connection_init(modbus_connection_t *connection, const modbus_endpoint_t *endpoint);
void modbus_tcp_connection_close(modbus_connection_t *connection);
const char *modbus_tcp_connection_mode_name(uint8_t mode);
esp_err_t modbus_tcp_read_registers(modbus_connection_t *connection, uint8_t function_code,
                                    uint16_t address, uint16_t count, uint16_t *registers);
esp_err_t modbus_tcp_write_single(modbus_connection_t *connection, uint16_t address, uint16_t value);
esp_err_t modbus_tcp_write_multiple(modbus_connection_t *connection, uint16_t address,
                                     const uint16_t *values, uint16_t count);

/* Returns the most recently received Modbus exception. A later successful or
 * transport-failed transaction does not erase the preserved device exception. */
bool modbus_tcp_get_last_exception(modbus_connection_t *connection,
                                   uint8_t *function_code,
                                   uint8_t *exception_code,
                                   uint32_t *exception_ms,
                                   uint32_t *exception_count);
