#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EM500_CACHE_SCOPE_INSTANTANEOUS = 1U << 0,
    EM500_CACHE_SCOPE_ENERGY = 1U << 1,
    EM500_CACHE_SCOPE_SETUP = 1U << 2,
    EM500_CACHE_SCOPE_ALL = EM500_CACHE_SCOPE_INSTANTANEOUS |
                            EM500_CACHE_SCOPE_ENERGY |
                            EM500_CACHE_SCOPE_SETUP,
} em500_cache_scope_t;

typedef struct {
    bool has_data;
    bool stale;
    esp_err_t last_error;
    uint32_t updated_ms;
    uint32_t age_ms;
    uint32_t response_ms;
    uint32_t success_count;
    uint32_t error_count;
} em500_cache_group_status_t;

typedef struct {
    bool configured;
    bool scan_in_progress;
    uint8_t function_code;
    uint8_t address_base;
    uint8_t requested_scopes;
    uint32_t generation;
    uint32_t requested_ms;
    em500_cache_group_status_t instantaneous;
    em500_cache_group_status_t source_input;
    em500_cache_group_status_t energy;
    em500_cache_group_status_t setup;
} em500_cache_status_t;

esp_err_t em500_cache_init(void);

/* Requests background acquisition. This never performs Modbus I/O in the
 * caller and is safe to call from an HTTP handler. */
esp_err_t em500_cache_request(uint8_t meter_index,
                              uint8_t function_code,
                              uint8_t address_base,
                              uint8_t scope_mask);

bool em500_cache_get_status(uint8_t meter_index,
                            em500_cache_status_t *out_status);

/* Copies one exact EM500 register block from the last-good cache. No Modbus I/O
 * occurs here. ESP_ERR_INVALID_STATE means that block has not completed yet. */
esp_err_t em500_cache_read_registers(uint8_t meter_index,
                                     uint8_t function_code,
                                     uint8_t address_base,
                                     uint16_t table_address,
                                     uint16_t count,
                                     uint16_t *registers);

#ifdef __cplusplus
}
#endif
