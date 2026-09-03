#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "modbus_connection_policy.h"

static void expect_close(uint8_t mode, bool ok, bool device_exception, bool expected)
{
    assert(modbus_connection_should_close(mode, ok, device_exception) == expected);
}

int main(void)
{
    assert(modbus_connection_mode_valid(MODBUS_CONNECTION_PER_TRANSACTION));
    assert(modbus_connection_mode_valid(MODBUS_CONNECTION_PERSISTENT));
    assert(modbus_connection_mode_valid(MODBUS_CONNECTION_RECONNECT_ON_ERROR));
    assert(!modbus_connection_mode_valid(3U));
    assert(!modbus_connection_mode_valid(255U));

    /* Read/write success has the same socket lifecycle: only legacy
     * per_transaction closes a healthy stream after every caller transaction. */
    expect_close(MODBUS_CONNECTION_PER_TRANSACTION, true, false, true);
    expect_close(MODBUS_CONNECTION_PERSISTENT, true, false, false);
    expect_close(MODBUS_CONNECTION_RECONNECT_ON_ERROR, true, false, false);

    /* A valid Modbus exception is a complete device response. Persistent keeps
     * the healthy TCP stream; reconnect_on_error deliberately closes it so the
     * next caller transaction establishes a new connection. */
    expect_close(MODBUS_CONNECTION_PER_TRANSACTION, false, true, true);
    expect_close(MODBUS_CONNECTION_PERSISTENT, false, true, false);
    expect_close(MODBUS_CONNECTION_RECONNECT_ON_ERROR, false, true, true);

    /* Timeout, TCP reset, malformed MBAP and other transport/framing failures
     * are represented by a non-OK result without a device exception. Every mode
     * must close the stream; no policy row asks for a same-call replay. */
    expect_close(MODBUS_CONNECTION_PER_TRANSACTION, false, false, true);
    expect_close(MODBUS_CONNECTION_PERSISTENT, false, false, true);
    expect_close(MODBUS_CONNECTION_RECONNECT_ON_ERROR, false, false, true);

    /* Unknown persisted modes fail closed. */
    expect_close(3U, true, false, true);
    expect_close(255U, false, false, true);

    puts("modbus connection policy tests passed");
    return 0;
}
