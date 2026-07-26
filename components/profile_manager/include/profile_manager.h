#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "profile_types.h"

esp_err_t profile_manager_init(void);
esp_err_t profile_manager_validate_point(const register_point_t *point);
esp_err_t profile_manager_validate_inverter_telemetry_profile(const inverter_telemetry_profile_t *profile);
esp_err_t profile_manager_get_inverter_telemetry_set(inverter_telemetry_profile_set_t *out_set);
esp_err_t profile_manager_save_inverter_telemetry_set(const inverter_telemetry_profile_set_t *profile_set);
bool profile_manager_get_inverter_telemetry(uint8_t inverter_index,
                                            inverter_telemetry_profile_t *out_profile);
