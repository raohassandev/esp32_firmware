#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "meter_types.h"

#define SAFETY_ALARM_METER_OFFLINE (1u << 0)
#define SAFETY_ALARM_METER_STALE   (1u << 1)

esp_err_t safety_manager_init(void);
float safety_manager_limit_target_kw(float requested_kw, const meter_data_t *grid_meter, uint32_t now_ms);
uint32_t safety_manager_get_alarm_flags(void);
