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
#include "meter_poll_schedule.h"
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
    uint32_t recent_results;
    uint8_t recent_count;
    /* Number of non-poll callers currently waiting for this meter's transport.
     *
     * With a zero poll interval the poll task finishes a transaction and
     * immediately wants the bus again. A FreeRTOS mutex is granted by priority,
     * not fairness, and the poll task runs above every UI and cache task, so
     * those callers would wait until their 5 s lock timeout and the EM500 pages
     * would simply stop working. Counting waiters lets the poll loop stand aside
     * for one tick, which costs one sample and keeps the rest of the product
     * alive. */
    volatile uint8_t io_waiters;
} meter_runtime_t;

static meter_runtime_t s_meters[APP_MAX_METERS];
static uint8_t s_meter_count;

#define METER_LOG_EVERY_N 30
#define METER_FRESH_GRACE_MS 5000U
#define METER_IO_LOCK_TIMEOUT_MS 5000U
#define METER_QUALITY_WINDOW 20U
#define METER_MIN_QUALITY_SAMPLES 5U
#define METER_MIN_SUCCESS_PERCENT 80U


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

/* Delay to insert after a completed transaction. The rule and its invariants live
 * in meter_poll_schedule so host tests can execute them; see that header. */
static uint32_t bounded_poll_delay(const meter_runtime_t *meter,
                                   const meter_data_t *data)
{
    return meter_poll_delay_ms(meter->config.poll_interval_ms,
                               data->degraded,
                               data->consecutive_failures);
}

static void update_quality(meter_runtime_t *meter, meter_data_t *data, bool success)
{
    const uint32_t mask = (1U << METER_QUALITY_WINDOW) - 1U;
    meter->recent_results = ((meter->recent_results << 1U) | (success ? 1U : 0U)) & mask;
    if (meter->recent_count < METER_QUALITY_WINDOW) meter->recent_count++;

    uint32_t successes = (uint32_t)__builtin_popcount(meter->recent_results);
    data->recent_sample_count = meter->recent_count;
    data->recent_success_percent = meter->recent_count
                                       ? (uint8_t)((successes * 100U) / meter->recent_count)
                                       : 0U;
    data->degraded = meter->recent_count >= METER_MIN_QUALITY_SAMPLES &&
                     data->recent_success_percent < METER_MIN_SUCCESS_PERCENT;
    data->current_poll_delay_ms = bounded_poll_delay(meter, data);
}

static void capture_modbus_exception(meter_runtime_t *meter)
{
    uint8_t function_code = 0;
    uint8_t exception_code = 0;
    uint32_t exception_ms = 0;
    uint32_t exception_count = 0;
    bool valid = modbus_tcp_get_last_exception(&meter->connection,
                                               &function_code,
                                               &exception_code,
                                               &exception_ms,
                                               &exception_count);
    meter_data_t next = data_snapshot(meter);
    next.last_modbus_exception_valid = valid;
    next.last_modbus_exception_function = function_code;
    next.last_modbus_exception_code = exception_code;
    next.last_modbus_exception_ms = exception_ms;
    next.modbus_exception_count = exception_count;
    store_data(meter, &next);
}

/* Registers interest in the transport before contending for it, so the poll loop
 * can see that someone else is waiting and stand aside. `announce` is false for
 * the poll task itself: it must not count itself as a waiter, or it would yield
 * to nobody forever. */
static esp_err_t serialized_read_announced(meter_runtime_t *meter,
                                           uint8_t function_code,
                                           uint16_t address,
                                           uint16_t count,
                                           uint16_t *registers,
                                           bool announce);

static esp_err_t serialized_read(meter_runtime_t *meter,
                                 uint8_t function_code,
                                 uint16_t address,
                                 uint16_t count,
                                 uint16_t *registers)
{
    /* Every caller other than the poll loop reaches the transport through here,
     * so this is where interest is announced. */
    return serialized_read_announced(meter, function_code, address, count, registers, true);
}

static esp_err_t serialized_read_announced(meter_runtime_t *meter,
                                           uint8_t function_code,
                                           uint16_t address,
                                           uint16_t count,
                                           uint16_t *registers,
                                           bool announce)
{
    if (!meter || !meter->io_mutex) return ESP_ERR_INVALID_STATE;
    if (announce && meter->io_waiters < UINT8_MAX) meter->io_waiters++;
    if (xSemaphoreTake(meter->io_mutex, pdMS_TO_TICKS(METER_IO_LOCK_TIMEOUT_MS)) != pdTRUE) {
        if (announce && meter->io_waiters > 0U) meter->io_waiters--;
        ESP_LOGW(TAG, "%s: Modbus transaction queue timeout", meter->config.name);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = modbus_tcp_read_registers(&meter->connection,
                                              function_code,
                                              address,
                                              count,
                                              registers);
    capture_modbus_exception(meter);
    xSemaphoreGive(meter->io_mutex);
    if (announce && meter->io_waiters > 0U) meter->io_waiters--;
    return err;
}

static void record_failure(meter_runtime_t *meter, esp_err_t err)
{
    meter_data_t next = data_snapshot(meter);
    next.last_attempt_ms = now_ms();
    next.response_errors++;
    next.consecutive_failures++;
    next.last_error = err;
    update_quality(meter, &next, false);
    next.online = sample_is_fresh(&next, next.last_attempt_ms) && !next.degraded;
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
            meter_data_t delayed = data_snapshot(meter);
            vTaskDelay(pdMS_TO_TICKS(delayed.current_poll_delay_ms));
            continue;
        }

        uint32_t started_ms = now_ms();
        esp_err_t err = serialized_read_announced(meter,
                                                 meter->config.function_code,
                                                 meter->config.active_power_address,
                                                 count,
                                                 registers,
                                                 false);
        uint32_t completed_ms = now_ms();
        meter_data_t next = data_snapshot(meter);
        uint32_t previous_failures = next.consecutive_failures;
        bool was_degraded = next.degraded;
        next.last_attempt_ms = completed_ms;
        next.last_response_time_ms = completed_ms - started_ms;

        float decoded = 0.0f;
        bool success = err == ESP_OK &&
                       modbus_decode_scaled(registers, count,
                                            meter->config.active_power_type,
                                            meter->config.active_power_order,
                                            scale, 0.0f, &decoded) == ESP_OK &&
                       isfinite(decoded);
        if (success) {
            next.active_power_kw = decoded;
            next.last_update_ms = next.last_attempt_ms;
            next.success_count++;
            next.consecutive_failures = 0;
            next.last_error = ESP_OK;
            update_quality(meter, &next, true);
            next.online = !next.degraded;
            if (previous_failures && !next.degraded) {
                ESP_LOGI(TAG, "%s communication recovered after %u failed polls; quality %u%%",
                         meter->config.name, (unsigned)previous_failures,
                         (unsigned)next.recent_success_percent);
            } else if (was_degraded && !next.degraded) {
                ESP_LOGI(TAG, "%s communication quality recovered to %u%%",
                         meter->config.name, (unsigned)next.recent_success_percent);
            }
        } else {
            if (err == ESP_OK) err = ESP_ERR_INVALID_RESPONSE;
            next.response_errors++;
            next.consecutive_failures++;
            next.last_error = err;
            update_quality(meter, &next, false);
            next.online = sample_is_fresh(&next, next.last_attempt_ms) && !next.degraded;
            log_meter_failure(meter, err, next.consecutive_failures);
        }

        if (!was_degraded && next.degraded) {
            ESP_LOGW(TAG, "%s communication degraded: %u%% success over %u requests; control input blocked",
                     meter->config.name, (unsigned)next.recent_success_percent,
                     (unsigned)next.recent_sample_count);
        }

        store_data(meter, &next);

        /* Poll-on-completion when the configured delay is zero: the next request
         * goes out as soon as this one finished, so the sample rate is set by the
         * device and the network rather than by an arbitrary period.
         *
         * Two things still get a turn. A non-zero configured delay is honoured
         * exactly, because an engineer may need to slow the bus for a device or a
         * gateway that cannot sustain the rate. And if any other caller is waiting
         * for this transport, one tick is given up so it can proceed -- without
         * that, a continuously polling task at priority 8 starves every UI and
         * cache reader beneath it and the EM500 pages stop responding.
         *
         * vTaskDelay(0) is a yield, not a sleep, so an equal-priority task on this
         * core still gets a turn even in the zero-delay case. */
        const uint32_t delay_ms = next.current_poll_delay_ms;
        if (delay_ms == 0U && meter->io_waiters > 0U) {
            vTaskDelay(1);
        } else {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
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

    s_meter_count = cfg->meter_count <= APP_MAX_METERS ? cfg->meter_count : APP_MAX_METERS;
    for (uint8_t i = 0; i < s_meter_count; ++i) {
        meter_runtime_t *runtime = &s_meters[i];
        memset(runtime, 0, sizeof(*runtime));
        runtime->index = i;
        runtime->config = cfg->meters[i];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        runtime->io_mutex = xSemaphoreCreateMutex();
        /* No sample has been acquired yet. NaN is intentional: cJSON emits it as
         * null and the control layer rejects it, so unavailable power can never
         * masquerade as a real 0.00 kW measurement. */
        runtime->data.active_power_kw = NAN;
        runtime->data.current_poll_delay_ms = runtime->config.poll_interval_ms < 100U
                                                  ? 100U
                                                  : runtime->config.poll_interval_ms;
        if (!runtime->io_mutex) runtime->data.last_error = ESP_ERR_NO_MEM;
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
        if (!isfinite(runtime->config.active_power_scale)) {
            runtime->data.last_error = ESP_ERR_INVALID_ARG;
            if (first_error == ESP_OK) first_error = ESP_ERR_INVALID_ARG;
            ESP_LOGE(TAG, "meter %u active-power scale is not finite", i);
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
        if (xTaskCreatePinnedToCore(meter_task, task_name, 4096, runtime, 8, NULL,
                                    PVDG_CONTROL_CORE) != pdPASS) {
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
