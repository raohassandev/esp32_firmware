#include "meter_manager.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_check.h"
#include <string.h>
#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
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
    SemaphoreHandle_t io_mutex;
} meter_runtime_t;

static meter_runtime_t s_meters[APP_MAX_METERS];
static uint8_t s_meter_count;

#define METER_LOG_EVERY_N 30
#define METER_FRESH_GRACE_MS 5000U
#define METER_IO_LOCK_TIMEOUT_MS 5000U

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool legacy_em500_scale_fingerprint(const meter_config_t *config)
{
    if (!config) return false;
    return config->active_power_type == MODBUS_DATA_INT32 &&
           config->active_power_order == MODBUS_ORDER_ABCD &&
           (config->active_power_address == 57U || config->active_power_address == 58U) &&
           fabsf(config->active_power_scale - 0.01f) < 0.000001f;
}

static float effective_active_power_scale(const meter_config_t *config)
{
    return legacy_em500_scale_fingerprint(config) ? 0.00001f
                                                   : config->active_power_scale;
}

static const char *failure_reason(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_NOT_FOUND:        return "hostname/DNS resolution failed";
    case ESP_ERR_TIMEOUT:          return "TCP timeout, no response";
    case ESP_ERR_INVALID_STATE:    return "network unavailable or no route to host";
    case ESP_ERR_INVALID_RESPONSE: return "Modbus protocol error";
    case ESP_ERR_INVALID_SIZE:     return "Modbus response size invalid";
    default:                       return "TCP connection failed";
    }
}

static void log_meter_failure(const meter_runtime_t *meter, esp_err_t err, uint32_t consecutive)
{
    if (consecutive != 1 && (consecutive % METER_LOG_EVERY_N) != 0) return;

    network_status_t net;
    network_manager_get_status(&net);

    ip4_addr_t dest = {0}, own = {0}, mask = {0};
    bool routable_check = ip4addr_aton(meter->config.endpoint.host, &dest) &&
                           ip4addr_aton(net.ip, &own) &&
                           ip4addr_aton(net.netmask, &mask) && mask.addr != 0;
    if (routable_check && (dest.addr & mask.addr) != (own.addr & mask.addr)) {
        ESP_LOGW(TAG, "%s: %s (%s); %s:%u is on another subnet than local %s/%s, relying on gateway %s"
                  " [failure %u]",
                  meter->config.name, failure_reason(err), esp_err_to_name(err),
                  meter->config.endpoint.host, meter->config.endpoint.port,
                  net.ip, net.netmask, net.gateway, (unsigned)consecutive);
    } else {
        ESP_LOGW(TAG, "%s: %s (%s) reading %s:%u [failure %u]",
                  meter->config.name, failure_reason(err), esp_err_to_name(err),
                  meter->config.endpoint.host, meter->config.endpoint.port, (unsigned)consecutive);
    }
}

static meter_data_t data_snapshot(meter_runtime_t *meter)
{
    meter_data_t snapshot;
    portENTER_CRITICAL(&meter->lock);
    snapshot = meter->data;
    portEXIT_CRITICAL(&meter->lock);
    return snapshot;
}

static void store_data(meter_runtime_t *meter, const meter_data_t *next)
{
    portENTER_CRITICAL(&meter->lock);
    meter->data = *next;
    portEXIT_CRITICAL(&meter->lock);
}

static bool sample_is_fresh(const meter_data_t *data, uint32_t timestamp)
{
    return data && data->last_update_ms != 0U &&
           timestamp - data->last_update_ms <= METER_FRESH_GRACE_MS;
}

static esp_err_t serialized_read(meter_runtime_t *meter,
                                 uint8_t function_code,
                                 uint16_t address,
                                 uint16_t count,
                                 uint16_t *registers)
{
    if (!meter || !meter->io_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(meter->io_mutex, pdMS_TO_TICKS(METER_IO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "%s: Modbus transaction queue timeout", meter->config.name);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = modbus_tcp_read_registers(&meter->connection,
                                              function_code,
                                              address,
                                              count,
                                              registers);
    xSemaphoreGive(meter->io_mutex);
    return err;
}

static void record_failure(meter_runtime_t *meter, esp_err_t err)
{
    meter_data_t next = data_snapshot(meter);
    next.last_attempt_ms = now_ms();
    next.online = sample_is_fresh(&next, next.last_attempt_ms);
    next.response_errors++;
    next.consecutive_failures++;
    next.last_error = err;
    store_data(meter, &next);
    log_meter_failure(meter, err, next.consecutive_failures);
}

static void meter_task(void *argument)
{
    meter_runtime_t *meter = argument;
    const size_t count = modbus_data_register_count(meter->config.active_power_type);
    const float scale = effective_active_power_scale(&meter->config);
    uint16_t registers[2] = {0};

    if (legacy_em500_scale_fingerprint(&meter->config)) {
        ESP_LOGW(TAG, "%s legacy EM500 scale %.8f normalized to %.8f kW/raw",
                 meter->config.name, (double)meter->config.active_power_scale,
                 (double)scale);
    }

    while (true) {
        if (!network_manager_wait_ready(5000)) {
            record_failure(meter, ESP_ERR_INVALID_STATE);
            continue;
        }

        esp_err_t err = serialized_read(meter,
                                        meter->config.function_code,
                                        meter->config.active_power_address,
                                        count,
                                        registers);
        meter_data_t next = data_snapshot(meter);
        uint32_t previous_failures = next.consecutive_failures;
        next.last_attempt_ms = now_ms();

        if (err == ESP_OK && modbus_decode_scaled(registers, count, meter->config.active_power_type,
                                                   meter->config.active_power_order,
                                                   scale, 0.0f,
                                                   &next.active_power_kw) == ESP_OK) {
            next.online = true;
            next.last_update_ms = next.last_attempt_ms;
            next.success_count++;
            next.consecutive_failures = 0;
            next.last_error = ESP_OK;
            if (previous_failures) {
                ESP_LOGI(TAG, "%s back online after %u failed polls", meter->config.name,
                          (unsigned)previous_failures);
            }
        } else {
            if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
            next.online = sample_is_fresh(&next, next.last_attempt_ms);
            next.response_errors++;
            next.consecutive_failures++;
            next.last_error = err;
            log_meter_failure(meter, err, next.consecutive_failures);
        }

        store_data(meter, &next);
        vTaskDelay(pdMS_TO_TICKS(meter->config.poll_interval_ms));
    }
}

esp_err_t meter_manager_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(err));
        return err;
    }

    s_meter_count = cfg->meter_count;
    for (uint8_t i = 0; i < s_meter_count; ++i) {
        meter_runtime_t *runtime = &s_meters[i];
        memset(runtime, 0, sizeof(*runtime));
        runtime->index = i;
        runtime->config = cfg->meters[i];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        runtime->io_mutex = xSemaphoreCreateMutex();
        if (!runtime->io_mutex) {
            runtime->data.last_error = ESP_ERR_NO_MEM;
        }
    }

    esp_err_t first_error = ESP_OK;
    for (uint8_t i = 0; i < s_meter_count; ++i) {
        meter_runtime_t *runtime = &s_meters[i];
        if (!runtime->config.enabled) continue;
        if (!runtime->io_mutex) {
            if (first_error == ESP_OK) first_error = ESP_ERR_NO_MEM;
            ESP_LOGE(TAG, "meter %u transaction mutex allocation failed", i);
            continue;
        }

        esp_err_t init_err = modbus_tcp_connection_init(&runtime->connection, &runtime->config.endpoint);
        if (init_err != ESP_OK) {
            runtime->data.last_error = init_err;
            if (first_error == ESP_OK) first_error = init_err;
            ESP_LOGE(TAG, "meter %u connection init failed: %s", i, esp_err_to_name(init_err));
            continue;
        }
        runtime->data.connection_initialized = true;

        char task_name[16];
        snprintf(task_name, sizeof(task_name), "meter_%u", i);
        if (xTaskCreate(meter_task, task_name, 4096, runtime, 8, NULL) != pdPASS) {
            runtime->data.last_error = ESP_ERR_NO_MEM;
            if (first_error == ESP_OK) first_error = ESP_ERR_NO_MEM;
            ESP_LOGE(TAG, "meter %u task creation failed", i);
        }
    }
    free(cfg);
    return first_error;
}

uint8_t meter_manager_get_count(void)
{
    return s_meter_count;
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

esp_err_t meter_manager_read_registers(uint8_t meter_index,
                                       uint8_t function_code,
                                       uint16_t address,
                                       uint16_t count,
                                       uint16_t *registers)
{
    if (!registers || count == 0 || count > 125 ||
        (function_code != 3 && function_code != 4)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (meter_index >= s_meter_count) return ESP_ERR_NOT_FOUND;

    meter_runtime_t *meter = &s_meters[meter_index];
    meter_data_t status = data_snapshot(meter);
    if (!meter->config.enabled || !status.connection_initialized || !meter->io_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!network_manager_wait_ready(0)) return ESP_ERR_INVALID_STATE;

    return serialized_read(meter, function_code, address, count, registers);
}
