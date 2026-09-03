#include "modbus_tcp.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

/* modbus_tcp.c is compiled with this symbol renamed source-locally. */
esp_err_t modbus_tcp_connection_init_core(modbus_connection_t *connection,
                                          const modbus_endpoint_t *endpoint);

esp_err_t modbus_tcp_connection_init(modbus_connection_t *connection,
                                     const modbus_endpoint_t *endpoint)
{
    if (!connection || !endpoint || !endpoint->host[0]) return ESP_ERR_INVALID_ARG;

    /* lwIP getaddrinfo() is synchronous and cannot be interrupted by the
     * Modbus cumulative transaction deadline.  Industrial endpoints are
     * therefore required to be literal IPv4 addresses at the transport
     * boundary.  A hostname fails immediately instead of being allowed to
     * stall meter/inverter acquisition and safety-control freshness.
     */
    struct in_addr address;
    if (inet_pton(AF_INET, endpoint->host, &address) != 1) {
        return ESP_ERR_INVALID_ARG;
    }

    return modbus_tcp_connection_init_core(connection, endpoint);
}
