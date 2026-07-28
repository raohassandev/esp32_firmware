#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t meter_read_jobs_init(void);

/* Drop-in replacement for read-only Engineering handlers. The function never
 * performs Modbus I/O in the caller. It returns fresh cached data immediately,
 * or queues a bounded background job and returns ESP_ERR_INVALID_STATE. */
esp_err_t meter_read_jobs_cached_read(uint8_t meter_index,
                                      uint8_t function_code,
                                      uint16_t address,
                                      uint16_t count,
                                      uint16_t *registers);

#ifdef __cplusplus
}
#endif
