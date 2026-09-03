#include "modbus_connection_policy.h"

bool modbus_connection_mode_valid(uint8_t mode)
{
    return mode <= MODBUS_CONNECTION_RECONNECT_ON_ERROR;
}

bool modbus_connection_should_close(uint8_t mode,
                                    bool transaction_ok,
                                    bool device_exception)
{
    if (!modbus_connection_mode_valid(mode)) return true;
    if (mode == MODBUS_CONNECTION_PER_TRANSACTION) return true;
    if (transaction_ok) return false;
    if (mode == MODBUS_CONNECTION_RECONNECT_ON_ERROR) return true;
    return !device_exception;
}
