#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "modbus_connection_policy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MODBUS_HOST_MAX_LEN 64

typedef struct {
    char host[MODBUS_HOST_MAX_LEN];
    uint16_t port;
    uint8_t unit_id;
    /* This byte occupies the alignment byte that existed before timeout_ms in
     * schemas <=5, keeping modbus_endpoint_t and all containing NVS layouts the
     * same size. Schema 6 explicitly normalizes legacy padding to the safe
     * per-transaction mode instead of trusting stored padding bytes. */
    uint8_t connection_mode; /* modbus_connection_mode_t */
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
    /* Transient classification for finish_transaction(). It is never persisted:
     * a valid device exception is different from a broken TCP/MBAP stream. */
    bool last_exchange_device_exception;
} modbus_connection_t;
