#include "inverter_manager.h"
#include "esp_check.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_decoder.h"
#include "modbus_tcp.h"
#include "network_manager.h"
#include "profile_manager.h"

static const char *TAG = "inverters";
#define INVERTER_TELEMETRY_LOG_EVERY_N 30

typedef struct {
    uint8_t index;
    inverter_config_t config;
    inverter_telemetry_profile_t telemetry_profile;
    modbus_connection_t connection;
    inverter_data_t data;
    portMUX_TYPE lock;
} inverter_runtime_t;

static inverter_runtime_t s_inverters[APP_MAX_INVERTERS];
static uint8_t s_inverter_count;
static float s_total_rated_kw;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inverter_data_t data_snapshot(inverter_runtime_t *runtime)
{
    inverter_data_t snapshot;
    portENTER_CRITICAL(&runtime->lock);
    snapshot = runtime->data;
    portEXIT_CRITICAL(&runtime->lock);
    return snapshot;
}

static void store_data(inverter_runtime_t *runtime, const inverter_data_t *next)
{
    portENTER_CRITICAL(&runtime->lock);
    runtime->data = *next;
    portEXIT_CRITICAL(&runtime->lock);
}

static void log_telemetry_failure(const inverter_runtime_t *runtime,
                                  esp_err_t err, uint32_t consecutive)
{
    if (consecutive != 1 &&
        (consecutive % INVERTER_TELEMETRY_LOG_EVERY_N) != 0) return;
    ESP_LOGW(TAG, "%s telemetry read failed: %s [failure %u]",
             runtime->config.name, esp_err_to_name(err),
             (unsigned)consecutive);
}

static void record_telemetry_failure(inverter_runtime_t *runtime, esp_err_t err)
{
    inverter_data_t next = data_snapshot(runtime);
    next.telemetry_online = false;
    next.telemetry_last_attempt_ms = now_ms();
    next.telemetry_errors++;
    next.telemetry_consecutive_failures++;
    next.telemetry_last_error = err;
    store_data(runtime, &next);
    log_telemetry_failure(runtime, err,
                          next.telemetry_consecutive_failures);
}

static void inverter_telemetry_task(void *argument)
{
    inverter_runtime_t *runtime = argument;
    const register_point_t *point = &runtime->telemetry_profile.active_power;
    const size_t register_count = modbus_data_register_count(point->data_type);
    uint16_t registers[2] = {0};

    while (true) {
        if (!network_manager_wait_ready(5000)) {
            record_telemetry_failure(runtime, ESP_ERR_INVALID_STATE);
            continue;
        }

        esp_err_t err = modbus_tcp_read_registers(
            &runtime->connection,
            point->function_code,
            point->address,
            register_count,
            registers);

        inverter_data_t next = data_snapshot(runtime);
        uint32_t previous_failures = next.telemetry_consecutive_failures;
        next.telemetry_last_attempt_ms = now_ms();

        float decoded_kw = 0.0f;
        if (err == ESP_OK) {
            err = modbus_decode_scaled(registers, register_count,
                                        point->data_type,
                                        point->word_order,
                                        point->scale,
                                        point->offset,
                                        &decoded_kw);
        }

        if (err == ESP_OK) {
            next.active_power_kw = decoded_kw;
            next.telemetry_online = true;
            next.telemetry_last_update_ms = next.telemetry_last_attempt_ms;
            next.telemetry_successes++;
            next.telemetry_consecutive_failures = 0;
            next.telemetry_last_error = ESP_OK;
            if (previous_failures) {
                ESP_LOGI(TAG, "%s telemetry back online after %u failed polls",
                         runtime->config.name,
                         (unsigned)previous_failures);
            }
        } else {
            next.telemetry_online = false;
            next.telemetry_errors++;
            next.telemetry_consecutive_failures++;
            next.telemetry_last_error = err;
            log_telemetry_failure(runtime, err,
                                  next.telemetry_consecutive_failures);
        }

        store_data(runtime, &next);
        vTaskDelay(pdMS_TO_TICKS(point->poll_interval_ms));
    }
}

esp_err_t inverter_manager_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        ESP_LOGE(TAG, "configuration unavailable: %s",
                 esp_err_to_name(err));
        return err;
    }

    s_inverter_count = cfg->inverter_count;
    s_total_rated_kw = 0.0f;
    for (uint8_t index = 0; index < s_inverter_count; ++index) {
        inverter_runtime_t *runtime = &s_inverters[index];
        memset(runtime, 0, sizeof(*runtime));
        runtime->index = index;
        runtime->config = cfg->inverters[index];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        runtime->data.rated_power_kw = runtime->config.rated_power_kw;
        if (profile_manager_get_inverter_telemetry(
                index, &runtime->telemetry_profile)) {
            runtime->data.telemetry_enabled =
                runtime->telemetry_profile.enabled;
        }
    }

    esp_err_t first_error = ESP_OK;
    for (uint8_t index = 0; index < s_inverter_count; ++index) {
        inverter_runtime_t *runtime = &s_inverters[index];
        if (!runtime->config.enabled) continue;

        esp_err_t init_err = modbus_tcp_connection_init(
            &runtime->connection, &runtime->config.endpoint);
        if (init_err != ESP_OK) {
            runtime->data.last_error = init_err;
            runtime->data.telemetry_last_error = init_err;
            if (first_error == ESP_OK) first_error = init_err;
            ESP_LOGE(TAG, "inverter %u connection init failed: %s",
                     index, esp_err_to_name(init_err));
            continue;
        }

        runtime->data.connection_initialized = true;
        s_total_rated_kw += runtime->config.rated_power_kw;

        if (runtime->telemetry_profile.enabled) {
            char task_name[20];
            snprintf(task_name, sizeof(task_name), "inv_telemetry_%u",
                     index);
            if (xTaskCreate(inverter_telemetry_task, task_name, 4096,
                            runtime, 8, NULL) != pdPASS) {
                runtime->data.telemetry_last_error = ESP_ERR_NO_MEM;
                if (first_error == ESP_OK) first_error = ESP_ERR_NO_MEM;
                ESP_LOGE(TAG, "inverter %u telemetry task creation failed",
                         index);
            }
        }
    }

    free(cfg);
    return first_error;
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

    for (uint8_t index = 0; index < s_inverter_count; ++index) {
        inverter_runtime_t *runtime = &s_inverters[index];
        if (!runtime->config.enabled ||
            !runtime->data.connection_initialized ||
            runtime->config.rated_power_kw <= 0.0f) continue;

        float share_kw = target_kw * runtime->config.rated_power_kw /
                         s_total_rated_kw;
        float percent = 100.0f * share_kw /
                        runtime->config.rated_power_kw;
        percent = fmaxf(runtime->config.minimum_percent,
                        fminf(runtime->config.maximum_percent, percent));
        uint32_t raw = (uint32_t)lroundf(
            percent * runtime->config.raw_units_per_percent);
        if (raw > UINT16_MAX) raw = UINT16_MAX;

        esp_err_t write_err;
        if (runtime->config.power_limit_function == 16) {
            uint16_t value = (uint16_t)raw;
            write_err = modbus_tcp_write_multiple(
                &runtime->connection,
                runtime->config.power_limit_address,
                &value, 1);
        } else {
            write_err = modbus_tcp_write_single(
                &runtime->connection,
                runtime->config.power_limit_address,
                (uint16_t)raw);
        }

        portENTER_CRITICAL(&runtime->lock);
        runtime->data.commanded_percent = percent;
        runtime->data.commanded_power_kw = share_kw;
        runtime->data.online = write_err == ESP_OK;
        runtime->data.has_command = true;
        runtime->data.last_command_ms = now_ms();
        runtime->data.last_error = write_err;
        if (write_err == ESP_OK) runtime->data.write_successes++;
        else runtime->data.write_errors++;
        portEXIT_CRITICAL(&runtime->lock);

        if (write_err != ESP_OK) {
            final_result = write_err;
            ESP_LOGW(TAG, "%s command failed: %s",
                     runtime->config.name,
                     esp_err_to_name(write_err));
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
