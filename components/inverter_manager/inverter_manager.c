#include "inverter_manager.h"
#include "esp_check.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "modbus_tcp.h"

static const char *TAG = "inverters";

typedef struct {
    inverter_config_t config;
    modbus_connection_t connection;
    inverter_data_t data;
    portMUX_TYPE lock;
} inverter_runtime_t;

static inverter_runtime_t s_inverters[APP_MAX_INVERTERS];
static uint8_t s_inverter_count;
static float s_total_rated_kw;

esp_err_t inverter_manager_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(err));
        return err;
    }
    s_inverter_count = cfg->inverter_count;
    s_total_rated_kw = 0.0f;
    for (uint8_t i = 0; i < s_inverter_count && err == ESP_OK; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        memset(runtime, 0, sizeof(*runtime));
        runtime->config = cfg->inverters[i];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        runtime->data.rated_power_kw = runtime->config.rated_power_kw;
        if (!runtime->config.enabled) continue;
        s_total_rated_kw += runtime->config.rated_power_kw;
        err = modbus_tcp_connection_init(&runtime->connection, &runtime->config.endpoint);
        if (err != ESP_OK) {
            runtime->data.last_error = err;
            ESP_LOGE(TAG, "inverter %u connection init failed: %s", i, esp_err_to_name(err));
        }
    }
    free(cfg);
    return err;
}

uint8_t inverter_manager_get_count(void)
{
    return s_inverter_count;
}

float inverter_manager_get_total_rated_kw(void)
{
    return s_total_rated_kw;
}

esp_err_t inverter_manager_set_total_power_kw(float target_kw)
{
    if (target_kw < 0.0f) target_kw = 0.0f;
    if (s_total_rated_kw <= 0.0f) return ESP_ERR_INVALID_STATE;
    if (target_kw > s_total_rated_kw) target_kw = s_total_rated_kw;
    esp_err_t final_result = ESP_OK;

    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        if (!runtime->config.enabled || runtime->config.rated_power_kw <= 0.0f) continue;
        float share_kw = target_kw * runtime->config.rated_power_kw / s_total_rated_kw;
        float percent = 100.0f * share_kw / runtime->config.rated_power_kw;
        percent = fmaxf(runtime->config.minimum_percent, fminf(runtime->config.maximum_percent, percent));
        uint32_t raw = (uint32_t)lroundf(percent * runtime->config.raw_units_per_percent);
        if (raw > UINT16_MAX) raw = UINT16_MAX;
        esp_err_t err;
        if (runtime->config.power_limit_function == 16) {
            uint16_t value = (uint16_t)raw;
            err = modbus_tcp_write_multiple(&runtime->connection, runtime->config.power_limit_address, &value, 1);
        } else {
            err = modbus_tcp_write_single(&runtime->connection, runtime->config.power_limit_address, (uint16_t)raw);
        }
        portENTER_CRITICAL(&runtime->lock);
        runtime->data.commanded_percent = percent;
        runtime->data.commanded_power_kw = share_kw;
        runtime->data.online = err == ESP_OK;
        runtime->data.has_command = true;
        runtime->data.last_command_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        runtime->data.last_error = err;
        if (err == ESP_OK) runtime->data.write_successes++;
        else runtime->data.write_errors++;
        portEXIT_CRITICAL(&runtime->lock);
        if (err != ESP_OK) {
            final_result = err;
            ESP_LOGW(TAG, "%s command failed: %s", runtime->config.name, esp_err_to_name(err));
        }
    }
    return final_result;
}

bool inverter_manager_get_data(uint8_t index, inverter_data_t *out_data)
{
    if (!out_data || index >= s_inverter_count) return false;
    inverter_runtime_t *runtime = &s_inverters[index];
    portENTER_CRITICAL(&runtime->lock);
    *out_data = runtime->data;
    portEXIT_CRITICAL(&runtime->lock);
    return true;
}
