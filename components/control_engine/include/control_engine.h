#pragma once
#include "esp_err.h"
#include "control_types.h"

esp_err_t control_engine_init(void);
void control_engine_get_status(control_status_t *out_status);
