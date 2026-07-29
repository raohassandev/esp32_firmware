#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MODBUS_HOST_MAX_LEN 64

typedef struct {
    char host[MODBUS_HOST_MAX_LEN];
    uint16_t port;
    uint8_t unit_id;
    uint32_t timeout_ms;
} modbus_endpoint_t;

typedef enum {
    MODBUS_DATA_UINT16 = 0,
    MODBUS_DATA_INT16,
    MODBUS_DATA_UINT32,
    MODBUS_DATA_INT32,
    MODBUS_DATA_FLOAT32
} modbus_data_type_t;

typedef enum {
    MODBUS_ORDER_ABCD = 0,
    MODBUS_ORDER_CDAB,
    MODBUS_ORDER_BADC,
    MODBUS_ORDER_DCBA
} modbus_word_order_t;

typedef struct {
    int socket_fd;
    uint16_t transaction_id;
    modbus_endpoint_t endpoint;
    SemaphoreHandle_t lock;
    uint32_t success_count;
    uint32_t error_count;
    uint32_t last_response_ms;
    bool last_exception_valid;
    uint8_t last_exception_function;
    uint8_t last_exception_code;
    uint32_t last_exception_ms;
    uint32_t exception_count;
} modbus_connection_t;
