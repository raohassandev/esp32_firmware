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

/* Frozen schema 2 layout: the single-generator policy, before per-engine limits.
 * Never edit, and never let it reference the live solar_grid_config_t or the live
 * solar_grid_generator_limits_t. Appending a field to either would silently
 * change this struct's size, no stored blob would match any known schema, and a
 * commissioned site would fall back to defaults -- losing its grid policy and
 * its generator rating in one step. */
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

/* Prefix proofs, stated with offsetof rather than arithmetic on field sizes: the
 * appended block may be preceded by alignment padding, and a hand-computed size
 * sum would then be wrong in a way that only shows up as a discarded
 * configuration on a commissioned unit. */
_Static_assert(offsetof(legacy_solar_grid_config_v2_t, generator_rated_kw) ==
                   sizeof(legacy_solar_grid_config_v1_t),
               "schema 1 must remain a byte-exact prefix of schema 2");
_Static_assert(offsetof(solar_grid_config_t, generator_rated_kw) ==
                   offsetof(legacy_solar_grid_config_v2_t, generator_rated_kw),
               "schema 2 generator limits must stay at the same offset in schema 3");
_Static_assert(offsetof(solar_grid_config_t, generator_extra) ==
                   sizeof(legacy_solar_grid_config_v2_t),
               "schema 2 must remain a byte-exact prefix of schema 3");
/* Each schema must be distinguishable by blob size, because that is how a stored
 * blob is recognised on load. */
_Static_assert(sizeof(legacy_solar_grid_config_v2_t) > sizeof(legacy_solar_grid_config_v1_t),
               "schema 2 must stay distinguishable from schema 1 by blob size");
_Static_assert(sizeof(solar_grid_config_t) > sizeof(legacy_solar_grid_config_v2_t),
               "schema 3 must stay distinguishable from schema 2 by blob size");
_Static_assert(SOLAR_GRID_MAX_GENERATORS == APP_MAX_GENERATORS,
               "generator limit slots and meter generator slots must agree");

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

solar_grid_generator_limits_t solar_grid_config_generator(const solar_grid_config_t *config,
                                                          uint8_t slot)
{
    solar_grid_generator_limits_t limits;
    memset(&limits, 0, sizeof(limits));
    if (!config || slot >= SOLAR_GRID_MAX_GENERATORS) return limits;
    if (slot > 0U) return config->generator_extra[slot - 1U];

    /* Slot 0 lives in the legacy scalar fields. It is in service exactly when its
     * rating is positive, which is what "commissioned" has meant for the
     * single-generator configuration since schema 2. An enabled-but-unrated slot 0
     * is therefore unrepresentable, and the gate reports the same unmet
     * prerequisite it always did. */
    limits.rated_kw = config->generator_rated_kw;
    limits.minimum_loading_percent = config->generator_minimum_loading_percent;
    limits.reserve_kw = config->generator_reserve_kw;
    limits.reverse_power_margin_kw = config->generator_reverse_power_margin_kw;
    limits.enabled = isfinite(limits.rated_kw) && limits.rated_kw > 0.0f;
    return limits;
}

uint8_t solar_grid_config_enabled_generator_count(const solar_grid_config_t *config)
{
    uint8_t count = 0U;
    for (uint8_t slot = 0U; slot < SOLAR_GRID_MAX_GENERATORS; ++slot) {
        if (solar_grid_config_generator(config, slot).enabled) count++;
    }
    return count;
}

/* Bounds only. Zero rated kW with the slot disabled is the uncommissioned state
 * and is valid: it holds PV at zero rather than rejecting the whole
 * configuration and discarding a commissioned grid policy with it. An enabled
 * slot carrying no rating is also stored rather than rejected -- refusing it here
 * would lose the rest of the configuration, and the commissioning gate is the
 * place that keeps control closed until an engineer supplies the number. */
static bool generator_limits_valid(const solar_grid_generator_limits_t *limits)
{
    return isfinite(limits->rated_kw) && limits->rated_kw >= 0.0f &&
           limits->rated_kw <= 1000000.0f &&
           isfinite(limits->minimum_loading_percent) &&
           limits->minimum_loading_percent >= 0.0f &&
           limits->minimum_loading_percent <= 100.0f &&
           isfinite(limits->reserve_kw) && limits->reserve_kw >= 0.0f &&
           limits->reserve_kw <= 1000000.0f &&
           isfinite(limits->reverse_power_margin_kw) &&
           limits->reverse_power_margin_kw >= 0.0f &&
           limits->reverse_power_margin_kw <= 1000000.0f;
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
    config->grid_available.function_code = 3U;
    config->grid_available.mask = 1U;
    config->grid_available.active_value = 1U;
    config->grid_breaker_closed.function_code = 3U;
    config->grid_breaker_closed.mask = 1U;
    config->grid_breaker_closed.active_value = 1U;
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
    /* Engine slots 1.. are bounded the same way. Slot 0 is checked above through
     * its legacy scalar fields, which is where it is stored. */
    for (uint8_t index = 0U; index < SOLAR_GRID_MAX_GENERATORS - 1U; ++index) {
        if (!generator_limits_valid(&config->generator_extra[index])) return false;
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
        ESP_LOGI(TAG, "Loaded persisted Solar-Grid policy '%s'; explicit grid evidence %s; "
                      "%u of %u generator engine slots in service",
                 solar_grid_policy_name(loaded.policy),
                 solar_grid_config_evidence_complete(&loaded) ? "configured" : "not configured",
                 (unsigned)solar_grid_config_enabled_generator_count(&loaded),
                 (unsigned)SOLAR_GRID_MAX_GENERATORS);
        return ESP_OK;
    }

    /* Schema 2 predates the per-engine limits. It is a byte-exact prefix, so the
     * upgrade is an append of zeroes: engine slots 1.. arrive disabled and
     * unrated, and slot 0 keeps the commissioned rating and minimum loading it
     * already had, in the same fields, at the same offsets. A commissioned
     * single-generator unit therefore behaves exactly as it did before the
     * upgrade -- it does not silently become a multi-engine plant, and it does not
     * fall back to defaults and lose its grid policy. */
    if (error == ESP_OK && size == sizeof(legacy_solar_grid_config_v2_t)) {
        legacy_solar_grid_config_v2_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        nvs_handle_t legacy_handle;
        if (nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &legacy_handle) == ESP_OK) {
            const esp_err_t legacy_error =
                nvs_get_blob(legacy_handle, SOLAR_GRID_KEY, &legacy, &legacy_size);
            nvs_close(legacy_handle);
            if (legacy_error == ESP_OK && legacy.magic == SOLAR_GRID_CONFIG_MAGIC &&
                legacy.version == 2u) {
                memset(&loaded, 0, sizeof(loaded));
                memcpy(&loaded, &legacy, sizeof(legacy));
                loaded.version = SOLAR_GRID_CONFIG_VERSION;
                memset(loaded.generator_extra, 0, sizeof(loaded.generator_extra));
                if (solar_grid_config_valid(&loaded)) {
                    set_active(&loaded);
                    ESP_LOGI(TAG, "Migrated Solar-Grid configuration schema 2 to schema %u; "
                                  "generator slot 0 keeps its commissioned limits and "
                                  "slots 1..%u are not in service",
                             SOLAR_GRID_CONFIG_VERSION, SOLAR_GRID_MAX_GENERATORS - 1u);
                    return solar_grid_config_save(&loaded);
                }
                ESP_LOGW(TAG, "Schema 2 Solar-Grid migration produced an invalid configuration");
            }
        }
    }

    /* Schema 1 predates the generator limits. Upgrade it rather than falling
     * back to defaults, which would discard a commissioned grid policy. The
     * generator fields start at zero, which reads as "not commissioned" and
     * holds PV at zero while a generator carries the plant. */
    if (error == ESP_OK && size == sizeof(legacy_solar_grid_config_v1_t)) {
        legacy_solar_grid_config_v1_t legacy = {0};
        size_t legacy_size = sizeof(legacy);
        nvs_handle_t legacy_handle;
        if (nvs_open(SOLAR_GRID_NAMESPACE, NVS_READONLY, &legacy_handle) == ESP_OK) {
            const esp_err_t legacy_error =
                nvs_get_blob(legacy_handle, SOLAR_GRID_KEY, &legacy, &legacy_size);
            nvs_close(legacy_handle);
            if (legacy_error == ESP_OK && legacy.magic == SOLAR_GRID_CONFIG_MAGIC &&
                legacy.version == 1u) {
                memset(&loaded, 0, sizeof(loaded));
                memcpy(&loaded, &legacy, sizeof(legacy));
                loaded.version = SOLAR_GRID_CONFIG_VERSION;
                loaded.generator_rated_kw = 0.0f;
                loaded.generator_minimum_loading_percent = 0.0f;
                loaded.generator_reserve_kw = 0.0f;
                loaded.generator_reverse_power_margin_kw = 0.0f;
                if (solar_grid_config_valid(&loaded)) {
                    set_active(&loaded);
                    ESP_LOGI(TAG, "Migrated Solar-Grid configuration schema 1 to schema %u; "
                                  "generator limits are not commissioned",
                             SOLAR_GRID_CONFIG_VERSION);
                    return solar_grid_config_save(&loaded);
                }
                ESP_LOGW(TAG, "Schema 1 Solar-Grid migration produced an invalid configuration");
            }
        }
    }

    solar_grid_config_defaults(&loaded);
    set_active(&loaded);
    ESP_LOGW(TAG, "No valid Solar-Grid configuration; safe defaults loaded with control evidence disabled");
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
