#include "solar_grid_config.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "config_types.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define SOLAR_GRID_NAMESPACE "pvdg_grid"
#define SOLAR_GRID_KEY "config"

static const char *TAG = "solar_grid_cfg";

/* Frozen schema 1 layout. Never edit: it exists so a stored blob written before
 * the generator limits were appended can still be recognised by size and
 * upgraded instead of being discarded. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    solar_grid_policy_t policy;
    solar_grid_meter_orientation_t meter_orientation;
    float export_limit_kw;
    float minimum_import_kw;
    solar_grid_signal_config_t grid_available;
    solar_grid_signal_config_t grid_breaker_closed;
    uint32_t evidence_poll_interval_ms;
    uint32_t evidence_stale_timeout_ms;
    uint32_t grid_loss_trip_ms;
    uint32_t grid_recovery_stable_ms;
} legacy_solar_grid_config_v1_t;

/* Frozen schema 2 layout. It adds the original single-generator policy limits
 * and is an exact prefix of schema 3. The new strong-source signals are always
 * disabled during migration so no register address or polarity is invented. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    solar_grid_policy_t policy;
    solar_grid_meter_orientation_t meter_orientation;
    float export_limit_kw;
    float minimum_import_kw;
    solar_grid_signal_config_t grid_available;
    solar_grid_signal_config_t grid_breaker_closed;
    uint32_t evidence_poll_interval_ms;
    uint32_t evidence_stale_timeout_ms;
    uint32_t grid_loss_trip_ms;
    uint32_t grid_recovery_stable_ms;
    float generator_rated_kw;
    float generator_minimum_loading_percent;
    float generator_reserve_kw;
    float generator_reverse_power_margin_kw;
} legacy_solar_grid_config_v2_t;

_Static_assert(sizeof(legacy_solar_grid_config_v2_t) ==
                   sizeof(legacy_solar_grid_config_v1_t) + 4U * sizeof(float),
               "schema 1 must remain a byte-exact prefix of schema 2");
_Static_assert(offsetof(solar_grid_config_t, generator_running) ==
                   sizeof(legacy_solar_grid_config_v2_t),
               "schema 2 must remain a byte-exact prefix of schema 3");

static solar_grid_config_t s_config;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static bool signal_valid(const solar_grid_signal_config_t *signal)
{
    if (!signal) return false;
    if (!signal->enabled) return true;
    return signal->meter_index < APP_MAX_METERS &&
           (signal->function_code == 3U || signal->function_code == 4U) &&
           signal->mask != 0U;
}

static void signal_safe_defaults(solar_grid_signal_config_t *signal)
{
    if (!signal) return;
    memset(signal, 0, sizeof(*signal));
    signal->function_code = 3U;
    signal->mask = 1U;
    signal->active_value = 1U;
}

void solar_grid_config_defaults(solar_grid_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->magic = SOLAR_GRID_CONFIG_MAGIC;
    config->version = SOLAR_GRID_CONFIG_VERSION;
    config->policy = SOLAR_GRID_POLICY_MINIMUM_IMPORT;
    config->meter_orientation = SOLAR_GRID_IMPORT_POSITIVE;
    config->export_limit_kw = 0.0f;
    config->minimum_import_kw = 5.0f;
    signal_safe_defaults(&config->grid_available);
    signal_safe_defaults(&config->grid_breaker_closed);
    signal_safe_defaults(&config->generator_running);
    signal_safe_defaults(&config->generator_breaker_closed);
    signal_safe_defaults(&config->transfer_active);
    signal_safe_defaults(&config->grid_generator_synchronized);
    config->evidence_poll_interval_ms = 500U;
    config->evidence_stale_timeout_ms = 2000U;
    config->grid_loss_trip_ms = 250U;
    config->grid_recovery_stable_ms = 5000U;
}

bool solar_grid_config_evidence_complete(const solar_grid_config_t *config)
{
    return config && config->grid_available.enabled &&
           config->grid_breaker_closed.enabled;
}

bool solar_grid_config_generator_evidence_complete(const solar_grid_config_t *config)
{
    return config && config->generator_running.enabled &&
           config->generator_breaker_closed.enabled;
}

bool solar_grid_config_valid(const solar_grid_config_t *config)
{
    if (!config || config->magic != SOLAR_GRID_CONFIG_MAGIC ||
        config->version != SOLAR_GRID_CONFIG_VERSION ||
        config->policy > SOLAR_GRID_POLICY_MINIMUM_IMPORT ||
        config->meter_orientation > SOLAR_GRID_EXPORT_POSITIVE ||
        !isfinite(config->export_limit_kw) || config->export_limit_kw < 0.0f ||
        config->export_limit_kw > 1000000.0f ||
        !isfinite(config->minimum_import_kw) || config->minimum_import_kw < 0.0f ||
        config->minimum_import_kw > 1000000.0f ||
        !signal_valid(&config->grid_available) ||
        !signal_valid(&config->grid_breaker_closed) ||
        config->grid_available.enabled != config->grid_breaker_closed.enabled ||
        !signal_valid(&config->generator_running) ||
        !signal_valid(&config->generator_breaker_closed) ||
        config->generator_running.enabled != config->generator_breaker_closed.enabled ||
        !signal_valid(&config->transfer_active) ||
        !signal_valid(&config->grid_generator_synchronized) ||
        config->evidence_poll_interval_ms < 100U ||
        config->evidence_poll_interval_ms > 60000U ||
        config->evidence_stale_timeout_ms < config->evidence_poll_interval_ms ||
        config->evidence_stale_timeout_ms > 600000U ||
        config->grid_loss_trip_ms > 60000U ||
        config->grid_recovery_stable_ms > 600000U) {
        return false;
    }
    /* Generator limits. Zero rated kW is the uncommissioned state and is valid:
     * it holds PV at zero while a generator carries the plant rather than
     * rejecting the whole configuration. Negative or non-finite is not. */
    if (!isfinite(config->generator_rated_kw) || config->generator_rated_kw < 0.0f ||
        config->generator_rated_kw > 1000000.0f ||
        !isfinite(config->generator_minimum_loading_percent) ||
        config->generator_minimum_loading_percent < 0.0f ||
        config->generator_minimum_loading_percent > 100.0f ||
        !isfinite(config->generator_reserve_kw) || config->generator_reserve_kw < 0.0f ||
        config->generator_reserve_kw > 1000000.0f ||
        !isfinite(config->generator_reverse_power_margin_kw) ||
        config->generator_reverse_power_margin_kw < 0.0f ||
        config->generator_reverse_power_margin_kw > 1000000.0f) {
        return false;
    }
    return true;
}

static void set_active(const solar_grid_config_t *config)
{
    portENTER_CRITICAL(&s_lock);
    s_config = *config;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t solar_grid_config_get_snapshot(solar_grid_config_t *out_config)
{
    if (!out_config) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    *out_config = s_config;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t solar_grid_config_save(const solar_grid_config_t *config)
{
    if (!solar_grid_config_valid(config)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SOLAR_GRID_NAMESPACE, NVS_READWRITE, &handle),
                        TAG, "NVS open failed");
    esp_err_t error = nvs_set_blob(handle, SOLAR_GRID_KEY, config, sizeof(*config));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK) return error;

    solar_grid_config_t verify = {0};
    size_t size = sizeof(verify);
    ESP_RETURN_ON_ERROR(nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &handle),
                        TAG, "NVS verify open failed");
    error = nvs_get_blob(handle, SOLAR_GRID_KEY, &verify, &size);
    nvs_close(handle);
    if (error != ESP_OK) return error;
    if (size != sizeof(verify) || memcmp(&verify, config, sizeof(verify)) != 0) {
        return ESP_ERR_INVALID_CRC;
    }

    set_active(config);
    return ESP_OK;
}

static void migrate_v1(const legacy_solar_grid_config_v1_t *legacy,
                       solar_grid_config_t *loaded)
{
    solar_grid_config_defaults(loaded);
    memcpy(loaded, legacy, sizeof(*legacy));
    loaded->version = SOLAR_GRID_CONFIG_VERSION;
    loaded->generator_rated_kw = 0.0f;
    loaded->generator_minimum_loading_percent = 0.0f;
    loaded->generator_reserve_kw = 0.0f;
    loaded->generator_reverse_power_margin_kw = 0.0f;
    signal_safe_defaults(&loaded->generator_running);
    signal_safe_defaults(&loaded->generator_breaker_closed);
    signal_safe_defaults(&loaded->transfer_active);
    signal_safe_defaults(&loaded->grid_generator_synchronized);
}

static void migrate_v2(const legacy_solar_grid_config_v2_t *legacy,
                       solar_grid_config_t *loaded)
{
    solar_grid_config_defaults(loaded);
    memcpy(loaded, legacy, sizeof(*legacy));
    loaded->version = SOLAR_GRID_CONFIG_VERSION;
    signal_safe_defaults(&loaded->generator_running);
    signal_safe_defaults(&loaded->generator_breaker_closed);
    signal_safe_defaults(&loaded->transfer_active);
    signal_safe_defaults(&loaded->grid_generator_synchronized);
}

esp_err_t solar_grid_config_init(void)
{
    solar_grid_config_t loaded = {0};
    size_t size = sizeof(loaded);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, SOLAR_GRID_KEY, &loaded, &size);
        nvs_close(handle);
    }

    if (error == ESP_OK && size == sizeof(loaded) && solar_grid_config_valid(&loaded)) {
        set_active(&loaded);
        ESP_LOGI(TAG, "Loaded persisted Solar-Grid policy '%s'; grid evidence %s; generator evidence %s",
                 solar_grid_policy_name(loaded.policy),
                 solar_grid_config_evidence_complete(&loaded) ? "configured" : "not configured",
                 solar_grid_config_generator_evidence_complete(&loaded) ? "configured" : "not configured");
        return ESP_OK;
    }

    if (error == ESP_OK && size == sizeof(legacy_solar_grid_config_v2_t)) {
        legacy_solar_grid_config_v2_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        nvs_handle_t legacy_handle;
        if (nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &legacy_handle) == ESP_OK) {
            const esp_err_t legacy_error = nvs_get_blob(legacy_handle, SOLAR_GRID_KEY,
                                                       &legacy, &legacy_size);
            nvs_close(legacy_handle);
            if (legacy_error == ESP_OK && legacy.magic == SOLAR_GRID_CONFIG_MAGIC &&
                legacy.version == 2u) {
                migrate_v2(&legacy, &loaded);
                if (solar_grid_config_valid(&loaded)) {
                    set_active(&loaded);
                    ESP_LOGI(TAG, "Migrated Solar-Grid configuration schema 2 to schema %u; strong generator/transfer evidence remains uncommissioned",
                             SOLAR_GRID_CONFIG_VERSION);
                    return solar_grid_config_save(&loaded);
                }
                ESP_LOGW(TAG, "Schema 2 Solar-Grid migration produced an invalid configuration");
            }
        }
    }

    if (error == ESP_OK && size == sizeof(legacy_solar_grid_config_v1_t)) {
        legacy_solar_grid_config_v1_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        nvs_handle_t legacy_handle;
        if (nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &legacy_handle) == ESP_OK) {
            const esp_err_t legacy_error = nvs_get_blob(legacy_handle, SOLAR_GRID_KEY,
                                                       &legacy, &legacy_size);
            nvs_close(legacy_handle);
            if (legacy_error == ESP_OK && legacy.magic == SOLAR_GRID_CONFIG_MAGIC &&
                legacy.version == 1u) {
                migrate_v1(&legacy, &loaded);
                if (solar_grid_config_valid(&loaded)) {
                    set_active(&loaded);
                    ESP_LOGI(TAG, "Migrated Solar-Grid configuration schema 1 to schema %u; generator limits and strong evidence are not commissioned",
                             SOLAR_GRID_CONFIG_VERSION);
                    return solar_grid_config_save(&loaded);
                }
                ESP_LOGW(TAG, "Schema 1 Solar-Grid migration produced an invalid configuration");
            }
        }
    }

    solar_grid_config_defaults(&loaded);
    set_active(&loaded);
    ESP_LOGW(TAG, "No valid Solar-Grid configuration; safe defaults loaded with source evidence disabled");
    return solar_grid_config_save(&loaded);
}

const char *solar_grid_policy_name(solar_grid_policy_t policy)
{
    switch (policy) {
    case SOLAR_GRID_POLICY_ZERO_EXPORT: return "zero_export";
    case SOLAR_GRID_POLICY_LIMITED_EXPORT: return "limited_export";
    case SOLAR_GRID_POLICY_MINIMUM_IMPORT: return "minimum_import";
    default: return "invalid";
    }
}

const char *solar_grid_orientation_name(solar_grid_meter_orientation_t orientation)
{
    switch (orientation) {
    case SOLAR_GRID_IMPORT_POSITIVE: return "import_positive";
    case SOLAR_GRID_EXPORT_POSITIVE: return "export_positive";
    default: return "invalid";
    }
}
