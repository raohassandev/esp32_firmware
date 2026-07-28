#include "inverter_manager.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"
#include "inverter_profile_decode.h"
#include "esp_check.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "modbus_tcp.h"

static const char *TAG = "inverters";
static const char *DEFAULT_PROFILE_ID = "custom.modbus-percent-v1";

#define INVERTER_TELEMETRY_TASK_STACK 5120
#define INVERTER_TELEMETRY_TASK_PRIORITY 5
#define INVERTER_TELEMETRY_IDLE_MS 100
#define INVERTER_IDENTITY_RECHECK_MS 60000U

typedef struct {
    inverter_config_t config;
    const inverter_profile_t *profile;
    bool write_allowed;
    bool identity_checked;
    uint32_t last_identity_ms;
    uint32_t next_poll_ms;
    modbus_connection_t connection;
    SemaphoreHandle_t io_mutex;
    inverter_data_t data;
    portMUX_TYPE lock;
} inverter_runtime_t;

typedef struct {
    inverter_runtime_t *runtime;
    const inverter_profile_t *profile;
    float rated_kw;
} command_target_t;

static inverter_runtime_t s_inverters[APP_MAX_INVERTERS];
static uint8_t s_inverter_count;
static float s_total_rated_kw;
static portMUX_TYPE s_capacity_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_telemetry_task;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint32_t profile_poll_ms(const inverter_profile_t *profile)
{
    return profile && profile->telemetry_poll_ms >= 100U
        ? profile->telemetry_poll_ms
        : 1000U;
}

static uint32_t profile_stale_ms(const inverter_profile_t *profile)
{
    uint32_t poll = profile_poll_ms(profile);
    return profile && profile->telemetry_stale_timeout_ms >= poll
        ? profile->telemetry_stale_timeout_ms
        : poll * 3U;
}

static uint32_t identity_raw(const uint16_t *words, uint8_t count)
{
    if (!words || count == 0) return 0;
    if (count == 1) return words[0];
    return ((uint32_t)words[0] << 16) | words[1];
}

static bool identity_matches(const inverter_profile_t *profile,
                             const uint16_t *words, uint8_t count)
{
    if (!profile || !profile->has_identity_probe) return true;
    uint32_t mask = profile->identity_mask ? profile->identity_mask : UINT32_MAX;
    return (identity_raw(words, count) & mask) == (profile->identity_expected & mask);
}

static bool identity_is_current(const inverter_runtime_t *runtime, uint32_t timestamp)
{
    if (!runtime->data.identity_supported) return true;
    return runtime->identity_checked && runtime->data.identity_verified &&
           runtime->last_identity_ms != 0U &&
           timestamp - runtime->last_identity_ms <= INVERTER_IDENTITY_RECHECK_MS;
}

static void invalidate_identity(inverter_runtime_t *runtime)
{
    portENTER_CRITICAL(&runtime->lock);
    runtime->identity_checked = false;
    runtime->last_identity_ms = 0U;
    if (runtime->data.identity_supported) runtime->data.identity_verified = false;
    portEXIT_CRITICAL(&runtime->lock);
}

static void recompute_commandable_capacity(void)
{
    float total = 0.0f;
    uint32_t timestamp = now_ms();
    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        bool eligible = runtime->config.enabled && runtime->write_allowed &&
                        runtime->data.connection_initialized && runtime->data.online &&
                        runtime->data.telemetry_valid && !runtime->data.telemetry_stale &&
                        identity_is_current(runtime, timestamp) &&
                        isfinite(runtime->config.rated_power_kw) &&
                        runtime->config.rated_power_kw > 0.0f;
        float rated = runtime->config.rated_power_kw;
        portEXIT_CRITICAL(&runtime->lock);
        if (eligible) total += rated;
    }
    portENTER_CRITICAL(&s_capacity_lock);
    s_total_rated_kw = total;
    portEXIT_CRITICAL(&s_capacity_lock);
}

static esp_err_t read_profile_block(inverter_runtime_t *runtime,
                                    uint8_t function_code,
                                    uint16_t address,
                                    uint8_t count,
                                    uint16_t *words)
{
    if (!runtime || !runtime->io_mutex || !words || count == 0) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(runtime->io_mutex,
                       pdMS_TO_TICKS(runtime->config.endpoint.timeout_ms + 100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = modbus_tcp_read_registers(&runtime->connection, function_code,
                                              address, count, words);
    xSemaphoreGive(runtime->io_mutex);
    return err;
}

static esp_err_t verify_identity(inverter_runtime_t *runtime)
{
    const inverter_profile_t *profile = runtime->profile;
    uint32_t timestamp = now_ms();
    if (!profile || !profile->has_identity_probe) {
        portENTER_CRITICAL(&runtime->lock);
        runtime->identity_checked = true;
        runtime->last_identity_ms = timestamp;
        runtime->data.identity_supported = false;
        runtime->data.identity_verified = true;
        portEXIT_CRITICAL(&runtime->lock);
        return ESP_OK;
    }

    uint16_t words[INVERTER_PROBE_MAX_REGISTERS] = {0};
    uint8_t count = profile->identity_words;
    if (count == 0 || count > INVERTER_PROBE_MAX_REGISTERS) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = read_profile_block(runtime, profile->identity_function,
                                       profile->identity_address, count, words);
    bool matched = err == ESP_OK && identity_matches(profile, words, count);

    portENTER_CRITICAL(&runtime->lock);
    runtime->identity_checked = matched;
    runtime->last_identity_ms = matched ? timestamp : 0U;
    runtime->data.identity_supported = true;
    runtime->data.identity_verified = matched;
    runtime->data.last_error = err == ESP_OK && !matched ? ESP_ERR_INVALID_RESPONSE : err;
    portEXIT_CRITICAL(&runtime->lock);

    return matched ? ESP_OK : (err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err);
}

static esp_err_t poll_active_power(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!profile || !profile->has_active_power) return ESP_ERR_NOT_SUPPORTED;

    uint16_t words[INVERTER_PROBE_MAX_REGISTERS] = {0};
    uint8_t count = profile->active_power_words;
    if (count == 0 || count > INVERTER_PROBE_MAX_REGISTERS) return ESP_ERR_INVALID_SIZE;

    esp_err_t err = read_profile_block(runtime, profile->active_power_function,
                                       profile->active_power_address, count, words);
    float power_kw = 0.0f;
    if (err == ESP_OK) {
        err = inverter_profile_decode_value(words, count,
                                            profile->active_power_type,
                                            profile->active_power_word_order,
                                            profile->active_power_scale,
                                            &power_kw);
        if (err == ESP_OK && !isfinite(power_kw)) err = ESP_ERR_INVALID_RESPONSE;
    }

    portENTER_CRITICAL(&runtime->lock);
    runtime->data.telemetry_supported = true;
    if (err == ESP_OK) {
        runtime->data.measured_power_kw = power_kw;
        runtime->data.telemetry_valid = true;
        runtime->data.telemetry_stale = false;
        runtime->data.online = true;
        runtime->data.last_telemetry_ms = timestamp;
        runtime->data.read_successes++;
        runtime->data.consecutive_read_failures = 0;
        runtime->data.last_error = ESP_OK;
    } else {
        runtime->data.read_errors++;
        runtime->data.consecutive_read_failures++;
        runtime->data.last_error = err;
        runtime->data.online = false;
        runtime->data.telemetry_valid = false;
        runtime->data.telemetry_stale = true;
        runtime->identity_checked = false;
        runtime->last_identity_ms = 0U;
        if (runtime->data.identity_supported) runtime->data.identity_verified = false;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

static esp_err_t poll_readback(inverter_runtime_t *runtime, uint32_t timestamp)
{
    const inverter_profile_t *profile = runtime->profile;
    if (!profile || !profile->has_power_limit_readback) return ESP_ERR_NOT_SUPPORTED;

    uint16_t words[INVERTER_PROBE_MAX_REGISTERS] = {0};
    uint8_t count = profile->power_limit_readback_words;
    if (count == 0 || count > INVERTER_PROBE_MAX_REGISTERS) return ESP_ERR_INVALID_SIZE;

    esp_err_t err = read_profile_block(runtime, profile->power_limit_readback_function,
                                       profile->power_limit_readback_address, count, words);
    float readback_percent = 0.0f;
    if (err == ESP_OK) {
        err = inverter_profile_decode_value(words, count,
                                            profile->power_limit_readback_type,
                                            profile->power_limit_readback_word_order,
                                            profile->power_limit_readback_scale,
                                            &readback_percent);
        if (err == ESP_OK && !isfinite(readback_percent)) err = ESP_ERR_INVALID_RESPONSE;
    }

    portENTER_CRITICAL(&runtime->lock);
    if (err == ESP_OK) {
        runtime->data.readback_percent = readback_percent;
        runtime->data.has_readback = true;
        runtime->data.last_readback_ms = timestamp;
        bool mismatch = runtime->data.has_command &&
            !inverter_profile_readback_matches(runtime->data.commanded_percent,
                                               readback_percent,
                                               profile->readback_tolerance_percent);
        runtime->data.command_mismatch = mismatch;
        if (mismatch) runtime->data.mismatch_count++;
    }
    portEXIT_CRITICAL(&runtime->lock);
    return err;
}

static void update_stale_state(inverter_runtime_t *runtime, uint32_t timestamp)
{
    portENTER_CRITICAL(&runtime->lock);
    if (runtime->data.telemetry_supported && runtime->data.telemetry_valid) {
        uint32_t age = timestamp - runtime->data.last_telemetry_ms;
        if (age > profile_stale_ms(runtime->profile)) {
            runtime->data.telemetry_stale = true;
            runtime->data.telemetry_valid = false;
            runtime->data.online = false;
            runtime->identity_checked = false;
            runtime->last_identity_ms = 0U;
            if (runtime->data.identity_supported) runtime->data.identity_verified = false;
        }
    }
    portEXIT_CRITICAL(&runtime->lock);
}

static void inverter_telemetry_task(void *argument)
{
    (void)argument;
    for (;;) {
        uint32_t timestamp = now_ms();
        for (uint8_t i = 0; i < s_inverter_count; ++i) {
            inverter_runtime_t *runtime = &s_inverters[i];
            if (!runtime->config.enabled || !runtime->data.connection_initialized ||
                !runtime->profile || !inverter_profile_allows_read(runtime->profile)) {
                continue;
            }
            update_stale_state(runtime, timestamp);
            if ((int32_t)(timestamp - runtime->next_poll_ms) < 0) continue;
            runtime->next_poll_ms = timestamp + profile_poll_ms(runtime->profile);

            if (!identity_is_current(runtime, timestamp) && verify_identity(runtime) != ESP_OK) {
                portENTER_CRITICAL(&runtime->lock);
                runtime->data.online = false;
                runtime->data.telemetry_valid = false;
                runtime->data.telemetry_stale = true;
                portEXIT_CRITICAL(&runtime->lock);
                continue;
            }

            esp_err_t telemetry_err = poll_active_power(runtime, timestamp);
            if (telemetry_err == ESP_OK && runtime->profile->has_power_limit_readback) {
                (void)poll_readback(runtime, timestamp);
            }
        }
        recompute_commandable_capacity();
        vTaskDelay(pdMS_TO_TICKS(INVERTER_TELEMETRY_IDLE_MS));
    }
}

esp_err_t inverter_manager_init(void)
{
    ESP_RETURN_ON_ERROR(inverter_profile_store_init(), TAG,
                        "inverter profile assignment store unavailable");

    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err != ESP_OK) {
        free(cfg);
        ESP_LOGE(TAG, "configuration unavailable: %s", esp_err_to_name(err));
        return err;
    }

    s_inverter_count = cfg->inverter_count <= APP_MAX_INVERTERS
                           ? cfg->inverter_count
                           : APP_MAX_INVERTERS;
    portENTER_CRITICAL(&s_capacity_lock);
    s_total_rated_kw = 0.0f;
    portEXIT_CRITICAL(&s_capacity_lock);

    esp_err_t first_error = ESP_OK;
    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        memset(runtime, 0, sizeof(*runtime));
        runtime->config = cfg->inverters[i];
        runtime->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
        runtime->data.rated_power_kw = runtime->config.rated_power_kw;

        char profile_id[INVERTER_PROFILE_ID_MAX] = {0};
        if (inverter_profile_store_get(i, profile_id, sizeof(profile_id)) != ESP_OK) {
            strlcpy(profile_id, DEFAULT_PROFILE_ID, sizeof(profile_id));
        }
        runtime->profile = inverter_profiles_find(profile_id);
        if (!runtime->profile) runtime->profile = inverter_profiles_find(DEFAULT_PROFILE_ID);
        runtime->write_allowed = inverter_profile_allows_write(runtime->profile);
        runtime->data.identity_supported = runtime->profile && runtime->profile->has_identity_probe;
        runtime->data.telemetry_supported = runtime->profile && runtime->profile->has_active_power;
        runtime->next_poll_ms = now_ms();

        if (!runtime->config.enabled) continue;
        if (!isfinite(runtime->config.rated_power_kw) || runtime->config.rated_power_kw <= 0.0f) {
            runtime->data.last_error = ESP_ERR_INVALID_ARG;
            if (first_error == ESP_OK) first_error = ESP_ERR_INVALID_ARG;
            continue;
        }
        runtime->io_mutex = xSemaphoreCreateMutex();
        if (!runtime->io_mutex) {
            runtime->data.last_error = ESP_ERR_NO_MEM;
            if (first_error == ESP_OK) first_error = ESP_ERR_NO_MEM;
            continue;
        }

        esp_err_t init_err = modbus_tcp_connection_init(&runtime->connection, &runtime->config.endpoint);
        if (init_err != ESP_OK) {
            runtime->data.last_error = init_err;
            if (first_error == ESP_OK) first_error = init_err;
            ESP_LOGE(TAG, "inverter %u connection init failed: %s", i, esp_err_to_name(init_err));
            continue;
        }
        runtime->data.connection_initialized = true;

        if (!runtime->write_allowed) {
            ESP_LOGW(TAG,
                     "inverter %u profile '%s' is not production-approved; command path remains locked",
                     i, runtime->profile ? runtime->profile->id : "missing");
        }
    }
    free(cfg);

    if (!s_telemetry_task) {
        BaseType_t created = xTaskCreate(inverter_telemetry_task, "inv_telemetry",
                                         INVERTER_TELEMETRY_TASK_STACK, NULL,
                                         INVERTER_TELEMETRY_TASK_PRIORITY,
                                         &s_telemetry_task);
        if (created != pdPASS) return ESP_ERR_NO_MEM;
    }
    return first_error;
}

uint8_t inverter_manager_get_count(void)
{
    return s_inverter_count;
}

float inverter_manager_get_total_rated_kw(void)
{
    portENTER_CRITICAL(&s_capacity_lock);
    float total = s_total_rated_kw;
    portEXIT_CRITICAL(&s_capacity_lock);
    return total;
}

static esp_err_t encode_command(const inverter_profile_t *profile, float percent,
                                uint16_t *words, uint8_t *word_count)
{
    if (!profile || !words || !word_count || !isfinite(percent) || percent < 0.0f ||
        !isfinite(profile->raw_units_per_percent) || profile->raw_units_per_percent <= 0.0f ||
        !isfinite(profile->minimum_percent) || !isfinite(profile->maximum_percent) ||
        profile->minimum_percent < 0.0f || profile->maximum_percent < profile->minimum_percent ||
        (profile->power_limit_words != 1U && profile->power_limit_words != 2U)) {
        return ESP_ERR_INVALID_ARG;
    }

    double raw_value = (double)percent * (double)profile->raw_units_per_percent;
    double maximum_raw = profile->power_limit_words == 1U ? UINT16_MAX : UINT32_MAX;
    if (!isfinite(raw_value) || raw_value < 0.0 || raw_value > maximum_raw) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t raw = (uint32_t)llround(raw_value);
    if (profile->power_limit_words == 2U) {
        words[0] = (uint16_t)(raw >> 16);
        words[1] = (uint16_t)raw;
        *word_count = 2U;
    } else {
        words[0] = (uint16_t)raw;
        *word_count = 1U;
    }
    return ESP_OK;
}

esp_err_t inverter_manager_set_total_power_kw(float target_kw)
{
    if (!isfinite(target_kw) || target_kw < 0.0f) return ESP_ERR_INVALID_ARG;

    command_target_t targets[APP_MAX_INVERTERS] = {0};
    uint8_t target_count = 0;
    float total_rated_kw = 0.0f;
    uint32_t timestamp = now_ms();

    for (uint8_t i = 0; i < s_inverter_count; ++i) {
        inverter_runtime_t *runtime = &s_inverters[i];
        portENTER_CRITICAL(&runtime->lock);
        bool eligible = runtime->config.enabled && runtime->data.connection_initialized &&
                        runtime->write_allowed && runtime->data.online &&
                        runtime->data.telemetry_valid && !runtime->data.telemetry_stale &&
                        identity_is_current(runtime, timestamp) &&
                        isfinite(runtime->config.rated_power_kw) &&
                        runtime->config.rated_power_kw > 0.0f;
        float rated = runtime->config.rated_power_kw;
        const inverter_profile_t *profile = runtime->profile;
        portEXIT_CRITICAL(&runtime->lock);
        if (!eligible || !profile || target_count >= APP_MAX_INVERTERS) continue;
        targets[target_count++] = (command_target_t){
            .runtime = runtime,
            .profile = profile,
            .rated_kw = rated
        };
        total_rated_kw += rated;
    }

    if (target_count == 0 || !isfinite(total_rated_kw) || total_rated_kw <= 0.0f) {
        ESP_LOGW(TAG, "power command rejected: no online production-approved inverter profile is commandable");
        return ESP_ERR_INVALID_STATE;
    }
    if (target_kw > total_rated_kw) target_kw = total_rated_kw;

    esp_err_t final_result = ESP_OK;
    float commanded_total_kw = 0.0f;
    for (uint8_t i = 0; i < target_count; ++i) {
        inverter_runtime_t *runtime = targets[i].runtime;
        const inverter_profile_t *profile = targets[i].profile;
        float share_kw = target_kw * targets[i].rated_kw / total_rated_kw;
        float percent = target_kw <= 0.0f ? 0.0f : 100.0f * share_kw / targets[i].rated_kw;
        if (!isfinite(percent)) {
            final_result = ESP_ERR_INVALID_ARG;
            continue;
        }
        if (percent > 0.0f && percent < profile->minimum_percent) percent = 0.0f;
        if (percent > profile->maximum_percent) percent = profile->maximum_percent;
        float commanded_kw = targets[i].rated_kw * percent / 100.0f;
        if (!isfinite(commanded_kw) || commanded_total_kw + commanded_kw > target_kw + 0.01f) {
            final_result = ESP_ERR_INVALID_ARG;
            continue;
        }

        uint16_t words[2] = {0};
        uint8_t word_count = 0;
        esp_err_t encode_err = encode_command(profile, percent, words, &word_count);
        if (encode_err != ESP_OK) {
            final_result = encode_err;
            continue;
        }

        if (xSemaphoreTake(runtime->io_mutex,
                           pdMS_TO_TICKS(runtime->config.endpoint.timeout_ms + 100U)) != pdTRUE) {
            final_result = ESP_ERR_TIMEOUT;
            continue;
        }
        esp_err_t command_err;
        if (profile->power_limit_function == 16) {
            command_err = modbus_tcp_write_multiple(&runtime->connection,
                                                     profile->power_limit_address,
                                                     words, word_count);
        } else if (profile->power_limit_function == 6 && word_count == 1U) {
            command_err = modbus_tcp_write_single(&runtime->connection,
                                                   profile->power_limit_address,
                                                   words[0]);
        } else {
            command_err = ESP_ERR_NOT_SUPPORTED;
        }
        xSemaphoreGive(runtime->io_mutex);

        portENTER_CRITICAL(&runtime->lock);
        runtime->data.last_error = command_err;
        if (command_err == ESP_OK) {
            runtime->data.commanded_percent = percent;
            runtime->data.commanded_power_kw = commanded_kw;
            runtime->data.has_command = true;
            runtime->data.last_command_ms = now_ms();
            runtime->data.write_successes++;
        } else {
            runtime->data.write_errors++;
        }
        portEXIT_CRITICAL(&runtime->lock);

        if (command_err == ESP_OK) {
            commanded_total_kw += commanded_kw;
        } else {
            final_result = command_err;
            invalidate_identity(runtime);
            ESP_LOGW(TAG, "%s command failed: %s", runtime->config.name,
                     esp_err_to_name(command_err));
        }
    }
    return final_result;
}

esp_err_t inverter_manager_probe_read_only(uint8_t inverter_index,
                                           inverter_probe_result_t *result)
{
    if (!result || inverter_index >= s_inverter_count) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));

    inverter_runtime_t *runtime = &s_inverters[inverter_index];
    result->connection_initialized = runtime->data.connection_initialized;
    result->profile_read_allowed = inverter_profile_allows_read(runtime->profile);

    if (!runtime->config.enabled || !runtime->data.connection_initialized) return ESP_ERR_INVALID_STATE;
    if (!result->profile_read_allowed || !runtime->profile) return ESP_ERR_NOT_SUPPORTED;

    bool attempted = false;
    esp_err_t final_error = ESP_OK;

    if (runtime->profile->has_identity_probe) {
        attempted = true;
        result->identity_attempted = true;
        result->identity_count = runtime->profile->identity_words;
        if (result->identity_count > INVERTER_PROBE_MAX_REGISTERS) {
            result->identity_count = INVERTER_PROBE_MAX_REGISTERS;
        }
        result->identity_error = read_profile_block(runtime,
            runtime->profile->identity_function,
            runtime->profile->identity_address,
            result->identity_count,
            result->identity_registers);
        result->identity_ok = result->identity_error == ESP_OK &&
            identity_matches(runtime->profile, result->identity_registers,
                             result->identity_count);
        if (!result->identity_ok) {
            final_error = result->identity_error == ESP_OK
                ? ESP_ERR_INVALID_RESPONSE : result->identity_error;
        }
    }

    if (runtime->profile->has_active_power) {
        attempted = true;
        result->active_power_attempted = true;
        result->active_power_count = runtime->profile->active_power_words;
        if (result->active_power_count > INVERTER_PROBE_MAX_REGISTERS) {
            result->active_power_count = INVERTER_PROBE_MAX_REGISTERS;
        }
        result->active_power_error = read_profile_block(runtime,
            runtime->profile->active_power_function,
            runtime->profile->active_power_address,
            result->active_power_count,
            result->active_power_registers);
        result->active_power_ok = result->active_power_error == ESP_OK;
        if (!result->active_power_ok && final_error == ESP_OK) {
            final_error = result->active_power_error;
        }
    }

    if (!attempted) return ESP_ERR_NOT_SUPPORTED;
    return final_error;
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
