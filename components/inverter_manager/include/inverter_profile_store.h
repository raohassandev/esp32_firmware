#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INVERTER_PROFILE_ID_MAX 40

typedef struct {
    char profile_ids[APP_MAX_INVERTERS][INVERTER_PROFILE_ID_MAX];
} inverter_profile_assignment_manifest_t;

esp_err_t inverter_profile_store_init(void);
esp_err_t inverter_profile_store_get(uint8_t inverter_index, char *profile_id, size_t profile_id_size);
esp_err_t inverter_profile_store_set(uint8_t inverter_index, const char *profile_id);
esp_err_t inverter_profile_store_get_all(inverter_profile_assignment_manifest_t *manifest);
esp_err_t inverter_profile_store_set_all(const inverter_profile_assignment_manifest_t *manifest);

#ifdef __cplusplus
}
#endif