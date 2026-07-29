#pragma once
#include "esp_err.h"
#include "control_types.h"

esp_err_t control_engine_init(void);
void control_engine_get_status(control_status_t *out_status);

/* Latches the running controller disabled. The control task applies a safe zero
 * on its next cycle; the caller never performs inverter I/O synchronously. */
void control_engine_force_disable(void);
