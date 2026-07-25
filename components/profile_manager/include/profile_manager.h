#pragma once
#include "esp_err.h"
#include "profile_types.h"

esp_err_t profile_manager_init(void);
esp_err_t profile_manager_validate_point(const register_point_t *point);
