#pragma once

/* Read-only reporting for the commissioning gate (P0-6) and inverter setpoint
 * write confirmation (P0-9).
 *
 * Both handlers read state that has already been acquired by the control task
 * and the background inverter telemetry task. Neither performs a Modbus
 * transaction, neither blocks on one, and neither issues a write of any kind. */

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t commissioning_gate_api_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
