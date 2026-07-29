#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Initializes the scan snapshot service and binds requests to the existing
 * Wi-Fi manager task. The HTTP caller only marks a request pending; all
 * esp_wifi scan operations execute from network_manager's task. */
esp_err_t network_scan_service_init(TaskHandle_t manager_task,
                                    uint32_t manager_wake_bit);

/* Called only by the Wi-Fi manager task after receiving manager_wake_bit. */
void network_scan_service_execute(void);

/* Completes a pending request without touching the radio. */
void network_scan_service_reject(esp_err_t error);
