#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MODBUS_CONNECTION_PER_TRANSACTION = 0,
    MODBUS_CONNECTION_PERSISTENT = 1,
    MODBUS_CONNECTION_RECONNECT_ON_ERROR = 2
} modbus_connection_mode_t;

bool modbus_connection_mode_valid(uint8_t mode);

/* Decide whether the TCP stream must be closed after one completed caller
 * transaction. A valid Modbus exception is a device response on a healthy
 * stream; transport/framing failures are not. This function never requests a
 * retry — reconnect_on_error reconnects only when the next caller arrives. */
bool modbus_connection_should_close(uint8_t mode,
                                    bool transaction_ok,
                                    bool device_exception);
