#include "meter_manager.h"
#include <stdio.h>
#include "esp_check.h"
#include <string.h>
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_decoder.h"
#include "modbus_tcp.h"
#include "network_manager.h"

static const char *TAG = "meters";

typedef struct {
    uint8_t index;
    meter_config_t config;
    modbus_connection_t connection;
    meter_data_t data;
    portMUX_TYPE lock;
} meter_runtime_t;

static meter_runtime_t s_meters[APP_MAX_METERS];
static uint8_t s_meter_count;

static void meter_task(void *argument)
{
    meter_runtime_t *meter = argument;
    const size_t count = modbus_data_register_count(meter->config.active_power_type);
    uint16_t registers[2] = {0};
    while (true) {
        if (!network_manager_wait_ready(5000)) {
            portENTER_CRITICAL(&meter->lock);
            meter->data.online = false;
            portEXIT_CRITICAL(&meter->lock);
            continue;
        }
        esp_err_t err = modbus_tcp_read_registers(&meter->connection, meter->config.function_code,
                                                 meter->config.active_power_address, count, registers);
        meter_data_t next;
        portENTER_CRITICAL(&meter->lock);
        next = meter->data;
        portEXIT_CRITICAL(&meter->lock);
        if (err == ESP_OK && modbus_decode_scaled(registers, count, meter->config.active_power_type,
                                                  meter->config.active_power_order,
                                                  meter->config.active_power_scale, 0.0f,
                                                  &next.active_power_kw) == ESP_OK) {
            next.online = true;
            next.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        } else {
            next.online = false;
            next.response_errors++;
            ESP_LOGW(TAG, "%s read failed after network ready: %s", meter->config.name, esp_err_to_name(err));
        }
        portENTER_CRITICAL(&meter->lock);
        meter->data = next;
        portEXIT_CRITICAL(&meter->lock);
        vTaskDelay(pdMS_TO_TICKS(meter->config.poll_interval_ms));
    }
}

esp_err_t meter_manager_init(void)
{
    app_config_t cfg;
    ESP_RETURN_ON_ERROR(config_manager_get_snapshot(&cfg), TAG, "configuration unavailable");
    s_meter_count = cfg.meter_count;
    for (uint8_t i = 0; i < s_meter_count; ++i) {
        if (!cfg.meters[i].enabled) continue;
        meter_runtime_t *runtime = &s_meters[i];
        memset(runtime, 0, sizeof(*runtime));
        runtime->index = i;
        runtime->config = cfg.meters[i];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        ESP_RETURN_ON_ERROR(modbus_tcp_connection_init(&runtime->connection, &runtime->config.endpoint), TAG, "meter connection init failed");
        char task_name[16];
        snprintf(task_name, sizeof(task_name), "meter_%u", i);
        if (xTaskCreate(meter_task, task_name, 4096, runtime, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool meter_manager_get_data(uint8_t index, meter_data_t *out_data)
{
    if (!out_data || index >= s_meter_count) return false;
    meter_runtime_t *meter = &s_meters[index];
    portENTER_CRITICAL(&meter->lock);
    *out_data = meter->data;
    portEXIT_CRITICAL(&meter->lock);
    return true;
}
