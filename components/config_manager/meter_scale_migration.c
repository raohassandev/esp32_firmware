#include "config_manager.h"

#include <math.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#define CONFIG_NS "pvdg"
#define SCALE_MIGRATION_KEY "meter_scale_v1"
#define LEGACY_EM500_SCALE 0.01f
#define EXPLICIT_EM500_SCALE 0.00001f

static const char *TAG = "meter_scale_migrate";

esp_err_t config_manager_init_schema6(void);

static bool legacy_scale_fingerprint(const meter_config_t *config)
{
    if (!config) return false;
    return config->active_power_type == MODBUS_DATA_INT32 &&
           config->active_power_order == MODBUS_ORDER_ABCD &&
           (config->active_power_address == 57U || config->active_power_address == 58U) &&
           fabsf(config->active_power_scale - LEGACY_EM500_SCALE) < 0.000001f;
}

static esp_err_t migration_marker_read(bool *complete)
{
    if (!complete) return ESP_ERR_INVALID_ARG;
    *complete = false;

    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NS, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;

    uint8_t value = 0U;
    error = nvs_get_u8(handle, SCALE_MIGRATION_KEY, &value);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    *complete = value == 1U;
    return ESP_OK;
}

static esp_err_t migration_marker_write(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NS, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, SCALE_MIGRATION_KEY, 1U);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t config_manager_init(void)
{
    esp_err_t error = config_manager_init_schema6();
    if (error != ESP_OK) return error;

    bool complete = false;
    error = migration_marker_read(&complete);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "meter-scale migration marker read failed: %s", esp_err_to_name(error));
        return error;
    }
    if (complete) return ESP_OK;

    app_config_t config;
    error = config_manager_get_snapshot(&config);
    if (error != ESP_OK) return error;

    uint8_t migrated_count = 0U;
    for (uint8_t index = 0; index < config.meter_count && index < APP_MAX_METERS; ++index) {
        if (!legacy_scale_fingerprint(&config.meters[index])) continue;
        /* This does not infer the meter manufacturer. It only freezes the exact
         * effective behavior the previous runtime compatibility shim already
         * applied to this stored tuple. Once persisted, future configurations
         * using 0.01 remain literal and are never silently rewritten at runtime. */
        config.meters[index].active_power_scale = EXPLICIT_EM500_SCALE;
        migrated_count++;
    }

    if (migrated_count > 0U) {
        error = config_manager_save(&config);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "explicit meter-scale persistence failed: %s", esp_err_to_name(error));
            return error;
        }
        ESP_LOGW(TAG,
                 "Persisted explicit 0.00001 scale for %u legacy compatibility meter mapping(s)",
                 (unsigned)migrated_count);
    }

    error = migration_marker_write();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "meter-scale migration marker persistence failed: %s", esp_err_to_name(error));
        return error;
    }
    return ESP_OK;
}
