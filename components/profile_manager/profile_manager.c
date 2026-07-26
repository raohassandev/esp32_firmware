#include "profile_manager.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define PROFILE_NAMESPACE "pvdg_profiles"
#define INVERTER_PROFILE_KEY "inv_telemetry"

static const char *TAG = "profiles";
static inverter_telemetry_profile_set_t s_inverter_profiles;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized;

static void profile_defaults(inverter_telemetry_profile_set_t *profile_set)
{
    memset(profile_set, 0, sizeof(*profile_set));
    profile_set->magic = INVERTER_TELEMETRY_PROFILE_MAGIC;
    profile_set->version = INVERTER_TELEMETRY_PROFILE_VERSION;

    for (uint8_t index = 0; index < PROFILE_MAX_INVERTERS; ++index) {
        inverter_telemetry_profile_t *profile = &profile_set->inverters[index];
        profile->enabled = false;
        strlcpy(profile->active_power.key, "active_power",
                sizeof(profile->active_power.key));
        profile->active_power.function_code = 3;
        profile->active_power.address = 0;
        profile->active_power.data_type = MODBUS_DATA_INT32;
        profile->active_power.word_order = MODBUS_ORDER_ABCD;
        profile->active_power.scale = 1.0f;
        profile->active_power.offset = 0.0f;
        profile->active_power.poll_interval_ms = 1000;
        profile->active_power.writable = false;
    }
}

esp_err_t profile_manager_validate_point(const register_point_t *point)
{
    if (!point || !point->key[0]) return ESP_ERR_INVALID_ARG;
    if (point->function_code != 3 && point->function_code != 4 &&
        point->function_code != 6 && point->function_code != 16) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (point->data_type > MODBUS_DATA_FLOAT32 ||
        point->word_order > MODBUS_ORDER_DCBA) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!isfinite(point->scale) || !isfinite(point->offset)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point->poll_interval_ms && point->poll_interval_ms < 50) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t profile_manager_validate_inverter_telemetry_profile(
    const inverter_telemetry_profile_t *profile)
{
    if (!profile) return ESP_ERR_INVALID_ARG;
    const register_point_t *point = &profile->active_power;
    esp_err_t err = profile_manager_validate_point(point);
    if (err != ESP_OK) return err;
    if (point->function_code != 3 && point->function_code != 4) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (point->writable || point->scale == 0.0f) return ESP_ERR_INVALID_ARG;
    if (point->poll_interval_ms < 100 || point->poll_interval_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t validate_set(const inverter_telemetry_profile_set_t *profile_set)
{
    if (!profile_set ||
        profile_set->magic != INVERTER_TELEMETRY_PROFILE_MAGIC ||
        profile_set->version != INVERTER_TELEMETRY_PROFILE_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    for (uint8_t index = 0; index < PROFILE_MAX_INVERTERS; ++index) {
        esp_err_t err = profile_manager_validate_inverter_telemetry_profile(
            &profile_set->inverters[index]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static void set_active(const inverter_telemetry_profile_set_t *profile_set)
{
    portENTER_CRITICAL(&s_lock);
    s_inverter_profiles = *profile_set;
    s_initialized = true;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t profile_manager_save_inverter_telemetry_set(
    const inverter_telemetry_profile_set_t *profile_set)
{
    esp_err_t err = validate_set(profile_set);
    if (err != ESP_OK) return err;

    nvs_handle_t handle;
    err = nvs_open(PROFILE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, INVERTER_PROFILE_KEY,
                       profile_set, sizeof(*profile_set));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;

    inverter_telemetry_profile_set_t verify;
    memset(&verify, 0, sizeof(verify));
    size_t verify_size = sizeof(verify);
    err = nvs_open(PROFILE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(handle, INVERTER_PROFILE_KEY,
                           &verify, &verify_size);
        nvs_close(handle);
    }
    if (err != ESP_OK) return err;
    if (verify_size != sizeof(verify) ||
        memcmp(&verify, profile_set, sizeof(verify)) != 0) {
        return ESP_ERR_INVALID_CRC;
    }

    set_active(profile_set);
    return ESP_OK;
}

esp_err_t profile_manager_init(void)
{
    inverter_telemetry_profile_set_t safe_defaults;
    inverter_telemetry_profile_set_t candidate;
    profile_defaults(&safe_defaults);
    candidate = safe_defaults;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROFILE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t stored_size = 0;
        err = nvs_get_blob(handle, INVERTER_PROFILE_KEY, NULL, &stored_size);
        if (err == ESP_OK && stored_size != sizeof(candidate)) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        }
        if (err == ESP_OK) {
            size_t read_size = sizeof(candidate);
            err = nvs_get_blob(handle, INVERTER_PROFILE_KEY,
                               &candidate, &read_size);
            if (err == ESP_OK) err = validate_set(&candidate);
        }
        nvs_close(handle);
    }

    if (err == ESP_OK) {
        set_active(&candidate);
        ESP_LOGI(TAG, "Loaded inverter telemetry profile version %u",
                 (unsigned)candidate.version);
        return ESP_OK;
    }

    set_active(&safe_defaults);
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_INVALID_LENGTH &&
        err != ESP_ERR_INVALID_VERSION && err != ESP_ERR_INVALID_ARG &&
        err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "Telemetry profile storage unavailable: %s; using safe in-memory defaults",
                 esp_err_to_name(err));
        return err;
    }

    esp_err_t save_err = profile_manager_save_inverter_telemetry_set(&safe_defaults);
    if (save_err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to persist safe telemetry defaults after %s: %s",
                 esp_err_to_name(err), esp_err_to_name(save_err));
        return save_err;
    }
    ESP_LOGW(TAG, "Initialized safe inverter telemetry defaults after %s; all profiles disabled",
             esp_err_to_name(err));
    return ESP_OK;
}

esp_err_t profile_manager_get_inverter_telemetry_set(
    inverter_telemetry_profile_set_t *out_set)
{
    if (!out_set) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    bool initialized = s_initialized;
    if (initialized) *out_set = s_inverter_profiles;
    portEXIT_CRITICAL(&s_lock);
    return initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool profile_manager_get_inverter_telemetry(
    uint8_t inverter_index, inverter_telemetry_profile_t *out_profile)
{
    if (!out_profile || inverter_index >= PROFILE_MAX_INVERTERS) return false;
    portENTER_CRITICAL(&s_lock);
    bool initialized = s_initialized;
    if (initialized) *out_profile = s_inverter_profiles.inverters[inverter_index];
    portEXIT_CRITICAL(&s_lock);
    return initialized;
}
