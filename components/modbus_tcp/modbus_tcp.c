#include "modbus_tcp.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "modbus_tcp";

static void put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static uint16_t get_u16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | src[1];
}

static esp_err_t socket_io_error(void)
{
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
}

static esp_err_t send_all(int fd, const uint8_t *data, size_t length)
{
    while (length) {
        int sent = send(fd, data, length, 0);
        if (sent <= 0) return socket_io_error();
        data += sent;
        length -= (size_t)sent;
    }
    return ESP_OK;
}

static esp_err_t recv_all(int fd, uint8_t *data, size_t length)
{
    while (length) {
        int received = recv(fd, data, length, 0);
        if (received <= 0) return socket_io_error();
        data += received;
        length -= (size_t)received;
    }
    return ESP_OK;
}

static esp_err_t ensure_connected(modbus_connection_t *c)
{
    if (c->socket_fd >= 0) return ESP_OK;
    char port[8];
    snprintf(port, sizeof(port), "%u", c->endpoint.port);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *result = NULL;
    if (getaddrinfo(c->endpoint.host, port, &hints, &result) != 0 || !result) return ESP_ERR_NOT_FOUND;

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return ESP_FAIL;
    }
    struct timeval timeout = {
        .tv_sec = c->endpoint.timeout_ms / 1000,
        .tv_usec = (c->endpoint.timeout_ms % 1000) * 1000
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, result->ai_addr, result->ai_addrlen) != 0) {
        esp_err_t err = (errno == EHOSTUNREACH || errno == ENETUNREACH) ? ESP_ERR_INVALID_STATE
                        : (errno == EINPROGRESS || errno == ETIMEDOUT || errno == EAGAIN) ? ESP_ERR_TIMEOUT
                        : ESP_FAIL;
        close(fd);
        freeaddrinfo(result);
        return err;
    }
    freeaddrinfo(result);
    c->socket_fd = fd;
    return ESP_OK;
}

static void mark_error(modbus_connection_t *c)
{
    c->error_count++;
    if (c->socket_fd >= 0) close(c->socket_fd);
    c->socket_fd = -1;
}

esp_err_t modbus_tcp_connection_init(modbus_connection_t *c, const modbus_endpoint_t *endpoint)
{
    if (!c || !endpoint || !endpoint->host[0] || !endpoint->port || !endpoint->unit_id) return ESP_ERR_INVALID_ARG;
    memset(c, 0, sizeof(*c));
    c->socket_fd = -1;
    c->endpoint = *endpoint;
    c->lock = xSemaphoreCreateMutex();
    return c->lock ? ESP_OK : ESP_ERR_NO_MEM;
}

void modbus_tcp_connection_close(modbus_connection_t *c)
{
    if (!c) return;
    if (c->socket_fd >= 0) close(c->socket_fd);
    c->socket_fd = -1;
}

static esp_err_t exchange(modbus_connection_t *c, const uint8_t *request, size_t request_len,
                          uint8_t expected_function, uint8_t *pdu, size_t pdu_capacity, size_t *pdu_length)
{
    esp_err_t err = ensure_connected(c);
    if (err != ESP_OK) return err;
    err = send_all(c->socket_fd, request, request_len);
    if (err != ESP_OK) return err;

    uint8_t header[7];
    err = recv_all(c->socket_fd, header, sizeof(header));
    if (err != ESP_OK) return err;
    uint16_t transaction = get_u16(header);
    uint16_t protocol = get_u16(header + 2);
    uint16_t length = get_u16(header + 4);
    if (transaction != c->transaction_id || protocol != 0 || header[6] != c->endpoint.unit_id || length < 2) return ESP_ERR_INVALID_RESPONSE;
    size_t body_len = length - 1;
    if (body_len > pdu_capacity) return ESP_ERR_INVALID_SIZE;
    err = recv_all(c->socket_fd, pdu, body_len);
    if (err != ESP_OK) return err;
    if (pdu[0] == (expected_function | 0x80)) return ESP_ERR_INVALID_RESPONSE;
    if (pdu[0] != expected_function) return ESP_ERR_INVALID_RESPONSE;
    *pdu_length = body_len;
    c->success_count++;
    c->last_response_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return ESP_OK;
}

esp_err_t modbus_tcp_read_registers(modbus_connection_t *c, uint8_t function_code,
                                   uint16_t address, uint16_t count, uint16_t *registers)
{
    if (!c || !registers || (function_code != 3 && function_code != 4) || count == 0 || count > 125) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    c->transaction_id++;
    uint8_t request[12] = {0};
    put_u16(request, c->transaction_id);
    put_u16(request + 4, 6);
    request[6] = c->endpoint.unit_id;
    request[7] = function_code;
    put_u16(request + 8, address);
    put_u16(request + 10, count);
    uint8_t pdu[252];
    size_t pdu_len = 0;
    esp_err_t err = exchange(c, request, sizeof(request), function_code, pdu, sizeof(pdu), &pdu_len);
    if (err == ESP_OK) {
        if (pdu_len != (size_t)(2 + count * 2) || pdu[1] != count * 2) {
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            for (uint16_t i = 0; i < count; ++i) registers[i] = get_u16(&pdu[2 + i * 2]);
        }
    }
    if (err != ESP_OK) mark_error(c);
    xSemaphoreGive(c->lock);
    return err;
}

esp_err_t modbus_tcp_write_single(modbus_connection_t *c, uint16_t address, uint16_t value)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    c->transaction_id++;
    uint8_t request[12] = {0};
    put_u16(request, c->transaction_id);
    put_u16(request + 4, 6);
    request[6] = c->endpoint.unit_id;
    request[7] = 6;
    put_u16(request + 8, address);
    put_u16(request + 10, value);
    uint8_t pdu[8];
    size_t pdu_len = 0;
    esp_err_t err = exchange(c, request, sizeof(request), 6, pdu, sizeof(pdu), &pdu_len);
    if (err == ESP_OK && (pdu_len != 5 || get_u16(pdu + 1) != address || get_u16(pdu + 3) != value)) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) mark_error(c);
    xSemaphoreGive(c->lock);
    return err;
}

esp_err_t modbus_tcp_write_multiple(modbus_connection_t *c, uint16_t address,
                                    const uint16_t *values, uint16_t count)
{
    if (!c || !values || count == 0 || count > 123) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    c->transaction_id++;
    size_t request_len = 13 + count * 2;
    uint8_t request[259] = {0};
    put_u16(request, c->transaction_id);
    put_u16(request + 4, (uint16_t)(7 + count * 2));
    request[6] = c->endpoint.unit_id;
    request[7] = 16;
    put_u16(request + 8, address);
    put_u16(request + 10, count);
    request[12] = count * 2;
    for (uint16_t i = 0; i < count; ++i) put_u16(request + 13 + i * 2, values[i]);
    uint8_t pdu[8];
    size_t pdu_len = 0;
    esp_err_t err = exchange(c, request, request_len, 16, pdu, sizeof(pdu), &pdu_len);
    if (err == ESP_OK && (pdu_len != 5 || get_u16(pdu + 1) != address || get_u16(pdu + 3) != count)) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) mark_error(c);
    xSemaphoreGive(c->lock);
    return err;
}
