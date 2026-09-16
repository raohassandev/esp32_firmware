#include "pvdg_ui_source_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *text)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", text ? text : "");
}

static void signal_defaults(pvdg_ui_source_signal_t *signal)
{
    if (!signal) return;
    memset(signal, 0, sizeof(*signal));
    signal->function_code = 3U;
    signal->mask = 1U;
    signal->active_value = 1U;
}

static void generator_defaults(pvdg_ui_source_generator_t *generator)
{
    if (!generator) return;
    memset(generator, 0, sizeof(*generator));
    signal_defaults(&generator->running);
    signal_defaults(&generator->breaker_closed);
}

void pvdg_ui_source_config_defaults(pvdg_ui_source_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->schema = PVDG_UI_SOURCE_SCHEMA;
    config->policy = PVDG_UI_SOURCE_MINIMUM_IMPORT;
    config->meter_orientation = PVDG_UI_SOURCE_IMPORT_POSITIVE;
    config->minimum_import_kw = 5.0;
    signal_defaults(&config->grid_available);
    signal_defaults(&config->grid_breaker_closed);
    signal_defaults(&config->transfer_active);
    signal_defaults(&config->grid_generator_synchronized);
    for (uint8_t i = 0U; i < PVDG_UI_SOURCE_MAX_GENERATORS; ++i) {
        generator_defaults(&config->generators[i]);
    }
    config->evidence_poll_interval_ms = 500U;
    config->evidence_stale_timeout_ms = 2000U;
    config->grid_loss_trip_ms = 250U;
    config->grid_recovery_stable_ms = 5000U;
}

bool pvdg_ui_source_signal_valid(const pvdg_ui_source_signal_t *signal)
{
    if (!signal) return false;
    if (!signal->enabled) return true;
    return signal->meter_index < PVDG_UI_SOURCE_MAX_METERS &&
           (signal->function_code == 3U || signal->function_code == 4U) &&
           signal->mask != 0U;
}

static bool finite_range(double value, double minimum, double maximum)
{
    return isfinite(value) && value >= minimum && value <= maximum;
}

static bool generator_valid(const pvdg_ui_source_generator_t *generator)
{
    return generator &&
           finite_range(generator->rated_kw, 0.0, PVDG_UI_SOURCE_KW_MAX) &&
           finite_range(generator->minimum_loading_percent, 0.0, 100.0) &&
           finite_range(generator->reserve_kw, 0.0, PVDG_UI_SOURCE_KW_MAX) &&
           finite_range(generator->reverse_power_margin_kw, 0.0, PVDG_UI_SOURCE_KW_MAX) &&
           pvdg_ui_source_signal_valid(&generator->running) &&
           pvdg_ui_source_signal_valid(&generator->breaker_closed) &&
           generator->running.enabled == generator->breaker_closed.enabled;
}

bool pvdg_ui_source_grid_evidence_complete(const pvdg_ui_source_config_t *config)
{
    return config && config->grid_available.enabled && config->grid_breaker_closed.enabled;
}

bool pvdg_ui_source_generator_evidence_complete(const pvdg_ui_source_config_t *config,
                                                uint8_t generator_index)
{
    return config && generator_index < PVDG_UI_SOURCE_MAX_GENERATORS &&
           config->generators[generator_index].running.enabled &&
           config->generators[generator_index].breaker_closed.enabled;
}

pvdg_ui_source_config_result_t pvdg_ui_source_config_validate(
    const pvdg_ui_source_config_t *config, char *error, size_t error_size)
{
    set_error(error, error_size, "");
    if (!config) {
        set_error(error, error_size, "Source configuration is missing.");
        return PVDG_UI_SOURCE_CONFIG_INVALID_ARGUMENT;
    }
    if (config->schema != PVDG_UI_SOURCE_SCHEMA) {
        set_error(error, error_size, "Unsupported Solar-Grid schema; firmware schema 4 is required.");
        return PVDG_UI_SOURCE_CONFIG_SCHEMA_MISMATCH;
    }
    if (config->policy > PVDG_UI_SOURCE_MINIMUM_IMPORT) {
        set_error(error, error_size, "Export policy must be Zero Export, Limited Export, or Minimum Import.");
        return PVDG_UI_SOURCE_CONFIG_POLICY_INVALID;
    }
    if (config->meter_orientation > PVDG_UI_SOURCE_EXPORT_POSITIVE) {
        set_error(error, error_size, "Meter orientation is invalid.");
        return PVDG_UI_SOURCE_CONFIG_ORIENTATION_INVALID;
    }
    if (!finite_range(config->export_limit_kw, 0.0, PVDG_UI_SOURCE_KW_MAX) ||
        !finite_range(config->minimum_import_kw, 0.0, PVDG_UI_SOURCE_KW_MAX)) {
        set_error(error, error_size, "Export/minimum-import limits must be finite values from 0 to 1,000,000 kW.");
        return PVDG_UI_SOURCE_CONFIG_LIMIT_INVALID;
    }
    if (!pvdg_ui_source_signal_valid(&config->grid_available) ||
        !pvdg_ui_source_signal_valid(&config->grid_breaker_closed) ||
        !pvdg_ui_source_signal_valid(&config->transfer_active) ||
        !pvdg_ui_source_signal_valid(&config->grid_generator_synchronized)) {
        set_error(error, error_size, "Enabled source evidence requires meter 1-4, FC03/FC04, and a non-zero mask.");
        return PVDG_UI_SOURCE_CONFIG_SIGNAL_INVALID;
    }
    if (config->grid_available.enabled != config->grid_breaker_closed.enabled) {
        set_error(error, error_size, "Grid available and grid breaker evidence must be enabled or disabled as a pair.");
        return PVDG_UI_SOURCE_CONFIG_SIGNAL_PAIR_INVALID;
    }
    if (config->evidence_poll_interval_ms < PVDG_UI_SOURCE_POLL_MIN_MS ||
        config->evidence_poll_interval_ms > PVDG_UI_SOURCE_POLL_MAX_MS ||
        config->evidence_stale_timeout_ms < config->evidence_poll_interval_ms ||
        config->evidence_stale_timeout_ms > PVDG_UI_SOURCE_STALE_MAX_MS ||
        config->grid_loss_trip_ms > PVDG_UI_SOURCE_GRID_LOSS_MAX_MS ||
        config->grid_recovery_stable_ms > PVDG_UI_SOURCE_GRID_RECOVERY_MAX_MS) {
        set_error(error, error_size, "Evidence timing is invalid: poll 100-60000 ms, stale >= poll and <=600000 ms, loss <=60000 ms, recovery <=600000 ms.");
        return PVDG_UI_SOURCE_CONFIG_TIMING_INVALID;
    }
    for (uint8_t i = 0U; i < PVDG_UI_SOURCE_MAX_GENERATORS; ++i) {
        if (!generator_valid(&config->generators[i])) {
            set_error(error, error_size, "Generator limits are invalid or running/breaker evidence is not configured as a pair.");
            return PVDG_UI_SOURCE_CONFIG_GENERATOR_INVALID;
        }
    }
    return PVDG_UI_SOURCE_CONFIG_OK;
}
