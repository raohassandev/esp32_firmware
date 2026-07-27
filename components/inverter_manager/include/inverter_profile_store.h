#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INVERTER_PROFILE_ID_MAX 40

esp_err_t inverter_profile_store_init(void);
esp_err_t inverter_profile_store_get(uint8_t inverter_index, char *profile_id, size_t profile_id_size);
esp_err_t inverter_profile_store_set(uint8_t inverter_index, const char *profile_id);

#ifdef __cplusplus
}
#endif
