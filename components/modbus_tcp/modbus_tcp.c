#include "modbus_tcp.h"
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define MODBUS_MIN_TIMEOUT_MS 100U
#define MODBUS_MAX_TIMEOUT_MS 60000U

static void put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static uint16_t get_u16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | src[1];
}

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static esp_err_t socket_io_error(void)
{
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
}

static bool remaining_timeout(int64_t deadline_us, struct timeval *timeout)
{
    int64_t remaining = deadline_us - now_us();
    if (remaining <= 0) return false;
    timeout->tv_sec = (time_t)(remaining / 1000000LL);
    timeout->tv_usec = (suseconds_t)(remaining % 1000000LL);
    return true;
}

static esp_err_t set_socket_deadline(int fd, int64_t deadline_us)
{
    struct timeval timeout;
    if (!remaining_timeout(deadline_us, &timeout)) return ESP_ERR_TIMEOUT;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t send_all(int fd, const uint8_t *data, size_t length, int64_t deadline_us)
{
    while (length) {
        esp_err_t deadline_err = set_socket_deadline(fd, deadline_us);
        if (deadline_err != ESP_OK) return deadline_err;
        int sent = send(fd, data, length, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return socket_io_error();
        }
        if (sent == 0) return ESP_ERR_INVALID_RESPONSE;
        data += sent;
        length -= (size_t)sent;
    }
    return ESP_OK;
}

static esp_err_t recv_all(int fd, uint8_t *data, size_t length, int64_t deadline_us)
{
    while (length) {
        esp_err_t deadline_err = set_socket_deadline(fd, deadline_us);
        if (deadline_err != ESP_OK) return deadline_err;
        int received = recv(fd, data, length, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            return socket_io_error();
        }
        if (received == 0) return ESP_ERR_INVALID_RESPONSE;
        data += received;
        length -= (size_t)received;
    }
    return ESP_OK;
}

static esp_err_t connect_with_deadline(int fd, const struct sockaddr *addr, socklen_t len,
                                       int64_t deadline_us)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return ESP_FAIL;

    esp_err_t result = ESP_OK;
    if (connect(fd, addr, len) != 0) {
        if (errno == EHOSTUNREACH || errno == ENETUNREACH) {
            result = ESP_ERR_INVALID_STATE;
        } else if (errno != EINPROGRESS) {
            result = ESP_FAIL;
        } else {
            for (;;) {
                struct timeval remaining;
                if (!remaining_timeout(deadline_us, &remaining)) {
                    result = ESP_ERR_TIMEOUT;
                    break;
                }
                fd_set writable;
                FD_ZERO(&writable);
                FD_SET(fd, &writable);
                int ready = select(fd + 1, NULL, &writable, NULL, &remaining);
                if (ready < 0 && errno == EINTR) continue;
                if (ready == 0) {
                    result = ESP_ERR_TIMEOUT;
                } else if (ready < 0) {
                    result = ESP_FAIL;
                } else {
                    int so_error = 0;
                    socklen_t error_len = sizeof(so_error);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &error_len) != 0) {
                        result = ESP_FAIL;
                    } else if (so_error == EHOSTUNREACH || so_error == ENETUNREACH) {
                        result = ESP_ERR_INVALID_STATE;
                    } else if (so_error == ETIMEDOUT) {
                        result = ESP_ERR_TIMEOUT;
                    } else if (so_error != 0) {
                        result = ESP_FAIL;
                    }
                }
                break;
            }
        }
    }

    if (result == ESP_OK && fcntl(fd, F_SETFL, flags) < 0) return ESP_FAIL;
    return result;
}

static esp_err_t connect_address(modbus_connection_t *c, const struct sockaddr *address,
                                 socklen_t address_len, int family, int socktype,
                                 int protocol, int64_t deadline_us)
{
    int fd = socket(family, socktype, protocol);
    if (fd < 0) return ESP_FAIL;
    esp_err_t err = connect_with_deadline(fd, address, address_len, deadline_us);
    if (err == ESP_OK) err = set_socket_deadline(fd, deadline_us);
    if (err != ESP_OK) {
        close(fd);
        return err;
    }
    c->socket_fd = fd;
    return ESP_OK;
}

static esp_err_t ensure_connected(modbus_connection_t *c, int64_t deadline_us)
{
    if (c->socket_fd >= 0) return ESP_OK;

    struct sockaddr_in literal = {
        .sin_family = AF_INET,
        .sin_port = htons(c->endpoint.port)
    };
    if (inet_pton(AF_INET, c->endpoint.host, &literal.sin_addr) == 1) {
        return connect_address(c, (const struct sockaddr *)&literal, sizeof(literal),
                               AF_INET, SOCK_STREAM, IPPROTO_TCP, deadline_us);
    }

    char port[8];
    snprintf(port, sizeof(port), "%u", c->endpoint.port);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *result = NULL;
    if (getaddrinfo(c->endpoint.host, port, &hints, &result) != 0 || !result) return ESP_ERR_NOT_FOUND;

    esp_err_t last_error = ESP_ERR_NOT_FOUND;
    for (struct addrinfo *entry = result; entry; entry = entry->ai_next) {
        if (now_us() >= deadline_us) {
            last_error = ESP_ERR_TIMEOUT;
            break;
        }
        last_error = connect_address(c, entry->ai_addr, entry->ai_addrlen,
                                     entry->ai_family, entry->ai_socktype,
                                     entry->ai_protocol, deadline_us);
        if (last_error == ESP_OK) break;
    }
    freeaddrinfo(result);
    return last_error;
}

static void close_socket(modbus_connection_t *c)
{
    if (c->socket_fd >= 0) close(c->socket_fd);
    c->socket_fd = -1;
}

static void finish_transaction(modbus_connection_t *c, esp_err_t result)
{
    if (result != ESP_OK) c->error_count++;
    close_socket(c);
}

esp_err_t modbus_tcp_connection_init(modbus_connection_t *c, const modbus_endpoint_t *endpoint)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    memset(c, 0, sizeof(*c));
    c->socket_fd = -1;
    if (!endpoint || !endpoint->host[0] || !endpoint->port || !endpoint->unit_id ||
        endpoint->timeout_ms < MODBUS_MIN_TIMEOUT_MS ||
        endpoint->timeout_ms > MODBUS_MAX_TIMEOUT_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    c->endpoint = *endpoint;
    c->lock = xSemaphoreCreateMutex();
    return c->lock ? ESP_OK : ESP_ERR_NO_MEM;
}

void modbus_tcp_connection_close(modbus_connection_t *c)
{
    if (!c) return;
    close_socket(c);
}

static esp_err_t exchange(modbus_connection_t *c, const uint8_t *request, size_t request_len,
                          uint8_t expected_function, uint8_t *pdu, size_t pdu_capacity,
                          size_t *pdu_length)
{
    c->last_exception_valid = false;
    c->last_exception_function = 0;
    c->last_exception_code = 0;

    int64_t deadline_us = now_us() + (int64_t)c->endpoint.timeout_ms * 1000LL;
    esp_err_t err = ensure_connected(c, deadline_us);
    if (err != ESP_OK) return err;
    err = send_all(c->socket_fd, request, request_len, deadline_us);
    if (err != ESP_OK) return err;

    uint8_t header[7];
    err = recv_all(c->socket_fd, header, sizeof(header), deadline_us);
    if (err != ESP_OK) return err;
    uint16_t transaction = get_u16(header);
    uint16_t protocol = get_u16(header + 2);
    uint16_t length = get_u16(header + 4);
    if (transaction != c->transaction_id || protocol != 0 ||
        header[6] != c->endpoint.unit_id || length < 2) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    size_t body_len = length - 1;
    if (body_len > pdu_capacity) return ESP_ERR_INVALID_SIZE;
    err = recv_all(c->socket_fd, pdu, body_len, deadline_us);
    if (err != ESP_OK) return err;
    if (pdu[0] == (expected_function | 0x80U)) {
        if (body_len != 2U) return ESP_ERR_INVALID_RESPONSE;
        c->last_exception_valid = true;
        c->last_exception_function = pdu[0];
        c->last_exception_code = pdu[1];
        c->exception_count++;
        c->last_response_ms = (uint32_t)(now_us() / 1000LL);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (pdu[0] != expected_function) return ESP_ERR_INVALID_RESPONSE;
    *pdu_length = body_len;
    c->success_count++;
    c->last_response_ms = (uint32_t)(now_us() / 1000LL);
    return ESP_OK;
}

esp_err_t modbus_tcp_read_registers(modbus_connection_t *c, uint8_t function_code,
                                    uint16_t address, uint16_t count, uint16_t *registers)
{
    if (!c || !c->lock || !registers || (function_code != 3 && function_code != 4) ||
        count == 0 || count > 125) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100U)) != pdTRUE) return ESP_ERR_TIMEOUT;
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
    finish_transaction(c, err);
    xSemaphoreGive(c->lock);
    return err;
}

esp_err_t modbus_tcp_write_single(modbus_connection_t *c, uint16_t address, uint16_t value)
{
    if (!c || !c->lock) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100U)) != pdTRUE) return ESP_ERR_TIMEOUT;
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
    if (err == ESP_OK && (pdu_len != 5 || get_u16(pdu + 1) != address ||
                          get_u16(pdu + 3) != value)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    finish_transaction(c, err);
    xSemaphoreGive(c->lock);
    return err;
}

esp_err_t modbus_tcp_write_multiple(modbus_connection_t *c, uint16_t address,
                                    const uint16_t *values, uint16_t count)
{
    if (!c || !c->lock || !values || count == 0 || count > 123) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100U)) != pdTRUE) return ESP_ERR_TIMEOUT;
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
    if (err == ESP_OK && (pdu_len != 5 || get_u16(pdu + 1) != address ||
                          get_u16(pdu + 3) != count)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    finish_transaction(c, err);
    xSemaphoreGive(c->lock);
    return err;
}

bool modbus_tcp_get_last_exception(modbus_connection_t *c,
                                   uint8_t *function_code,
                                   uint8_t *exception_code,
                                   uint32_t *exception_count)
{
    if (!c || !c->lock) return false;
    if (xSemaphoreTake(c->lock, pdMS_TO_TICKS(c->endpoint.timeout_ms + 100U)) != pdTRUE) {
        return false;
    }
    bool valid = c->last_exception_valid;
    if (function_code) *function_code = valid ? c->last_exception_function : 0U;
    if (exception_code) *exception_code = valid ? c->last_exception_code : 0U;
    if (exception_count) *exception_count = c->exception_count;
    xSemaphoreGive(c->lock);
    return valid;
}
