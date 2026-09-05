#include "local_backend_provider.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"
#include "screen_api.h"
#include "source_detection.h"

/*
 * Native LCD read-model adapter.
 *
 * The screen runs on the same MCU as Product Core. Self-HTTP was proven
 * unreliable on the physical ESP-IDF target, so this adapter reads only
 * existing Core snapshots and projects the subset already consumed by
 * screen_api.c. It performs no Modbus I/O, no control writes and no source or
 * electrical inference.
 *
 * The current operational event/alarm component does not expose an in-process
 * snapshot builder. Those two routes therefore fail closed as unavailable
 * rather than fabricating event history or duplicating the private alarm
 * lifecycle state machine.
 */

typedef struct {
    const char *path;
    size_t capacity;
    char *json;
    bool valid;
    uint32_t consecutive_failures;
} local_api_slot_t;

static local_api_slot_t s_slots[] = {
    {SCREEN_API_LIVE_PATH,       4096U,  NULL, false, 0U},
    {SCREEN_API_STATUS_PATH,     8192U,  NULL, false, 0U},
    {SCREEN_API_METERS_PATH,    12288U,  NULL, false, 0U},
    {SCREEN_API_INVERTERS_PATH, 24576U,  NULL, false, 0U},
    {SCREEN_API_TELEMETRY_PATH,  8192U,  NULL, false, 0U},
    {SCREEN_API_EVENTS_PATH,    49152U,  NULL, false, 0U},
    {SCREEN_API_ALARMS_PATH,    32768U,  NULL, false, 0U},
};

static const char *TAG = "screen_backend";
static bool s_logged_first_success;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static local_api_slot_t *slot_for(const char *path)
{
    if (!path) return NULL;
    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        if (strcmp(path, s_slots[i].path) == 0) return &s_slots[i];
    }
    return NULL;
}

static void note_failure(local_api_slot_t *slot, const char *reason)
{
    if (!slot) return;
    slot->valid = false;
    slot->consecutive_failures++;
    if (slot->consecutive_failures == 1U || (slot->consecutive_failures % 20U) == 0U) {
        ESP_LOGW(TAG, "%s unavailable (%s), consecutive failures=%u",
                 slot->path,
                 reason ? reason : "unknown",
                 (unsigned)slot->consecutive_failures);
    }
}

static bool provider_acquire(void *context, const char *path, const char **json)
{
    (void)context;
    if (!json) return false;
    *json = NULL;
    local_api_slot_t *slot = slot_for(path);
    if (!slot || !slot->valid || !slot->json) return false;
    *json = slot->json;
    return true;
}

static void provider_release(void *context, const char *path, const char *json)
{
    (void)context;
    (void)path;
    (void)json;
}

static bool finish_json(local_api_slot_t *slot, cJSON *root)
{
    if (!slot || !slot->json || !root) {
        cJSON_Delete(root);
        note_failure(slot, "in-process JSON allocation failed");
        return false;
    }

    const cJSON_bool printed = cJSON_PrintPreallocated(
        root, slot->json, (int)slot->capacity, false);
    cJSON_Delete(root);
    if (!printed) {
        slot->json[0] = '\0';
        note_failure(slot, "bounded JSON slot too small");
        return false;
    }

    slot->valid = true;
    slot->consecutive_failures = 0U;
    if (!s_logged_first_success) {
        ESP_LOGI(TAG, "Core read models reachable in-process; screen data path online");
        s_logged_first_success = true;
    }
    return true;
}

static void add_number_or_null(cJSON *object, const char *name, double value, bool valid)
{
    if (valid && isfinite(value)) cJSON_AddNumberToObject(object, name, value);
    else cJSON_AddNullToObject(object, name);
}

static void copy_bounded(char *destination, size_t capacity, const char *source)
{
    if (!destination || capacity == 0U) return;
    if (!source) source = "";
    snprintf(destination, capacity, "%s", source);
}

static uint32_t meter_stale_after_ms(const app_config_t *config, uint8_t index)
{
    const meter_config_t *meter = &config->meters[index];
    if (index == 0U && config->control.meter_stale_timeout_ms > 0U) {
        return config->control.meter_stale_timeout_ms;
    }
    uint64_t derived = (uint64_t)meter->poll_interval_ms * 3ULL;
    if (derived < 1000ULL) derived = 1000ULL;
    if (derived > UINT32_MAX) derived = UINT32_MAX;
    return (uint32_t)derived;
}

static bool reset_was_unexpected(esp_reset_reason_t reason)
{
    return reason == ESP_RST_PANIC ||
           reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT ||
           reason == ESP_RST_PWR_GLITCH ||
           reason == ESP_RST_CPU_LOCKUP;
}

static const char *controller_resource_state(void)
{
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const double fragmentation = free_internal > 0U
                                     ? 1.0 - ((double)largest_internal / (double)free_internal)
                                     : 1.0;
    const bool reset_attention = reset_was_unexpected(esp_reset_reason());
    const bool heap_warning = free_internal < 65536U || largest_internal < 32768U ||
                              fragmentation > 0.70;
    const bool heap_critical = free_internal < 32768U || largest_internal < 16384U ||
                               fragmentation > 0.85;
    return heap_critical ? "critical" : (heap_warning || reset_attention) ? "review" : "healthy";
}

typedef struct {
    bool runtime_available;
    bool connection_initialized;
    bool initialization_failed;
    bool has_data;
    bool stale;
    bool online;
    uint32_t data_age_ms;
    const char *state;
} meter_view_t;

static meter_view_t meter_view(const app_config_t *config,
                               uint8_t index,
                               const meter_data_t *data,
                               bool runtime_available,
                               uint32_t current_ms)
{
    const meter_config_t *meter = &config->meters[index];
    meter_view_t view = {0};
    view.runtime_available = runtime_available;
    view.connection_initialized = runtime_available && data->connection_initialized;
    view.initialization_failed = meter->enabled &&
        (!view.connection_initialized ||
         (data->last_attempt_ms == 0U && data->last_error != ESP_OK));
    view.has_data = runtime_available && data->last_update_ms != 0U;
    view.data_age_ms = view.has_data ? current_ms - data->last_update_ms : 0U;
    const uint32_t stale_after = meter_stale_after_ms(config, index);
    view.stale = meter->enabled && !view.initialization_failed &&
                 (!view.has_data || view.data_age_ms > stale_after);
    view.online = meter->enabled && !view.initialization_failed &&
                  runtime_available && data->online && !view.stale;
    view.state = !meter->enabled ? "disabled" :
                 view.initialization_failed ? "initialization_failed" :
                 view.online ? "online" :
                 view.has_data ? "stale" : "unavailable";
    return view;
}

typedef struct {
    bool runtime_available;
    bool connection_initialized;
    bool initialization_failed;
    bool has_command;
    bool last_write_ok;
    bool online;
    const char *state;
} inverter_view_t;

static inverter_view_t inverter_view(const inverter_config_t *config,
                                     const inverter_data_t *data,
                                     bool runtime_available)
{
    inverter_view_t view = {0};
    view.runtime_available = runtime_available;
    view.connection_initialized = runtime_available && data->connection_initialized;
    view.initialization_failed = config->enabled && !view.connection_initialized;
    view.has_command = runtime_available && data->has_command;
    view.last_write_ok = view.has_command && data->online;
    view.online = runtime_available && config->enabled && data->online;
    view.state = !config->enabled ? "disabled" :
                 view.initialization_failed ? "initialization_failed" :
                 !view.has_command ? "not_tested" :
                 view.last_write_ok ? "last_write_ok" : "last_write_failed";
    return view;
}

static bool build_live(local_api_slot_t *slot)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);

    control_status_t control = {0};
    control_engine_get_status(&control);
    add_number_or_null(root, "grid_kw", control.grid_power_kw, isfinite(control.grid_power_kw));
    add_number_or_null(root, "requested_pv_kw", control.requested_pv_kw,
                       isfinite(control.requested_pv_kw));
    add_number_or_null(root, "applied_pv_kw", control.applied_pv_kw,
                       isfinite(control.applied_pv_kw));
    cJSON_AddBoolToObject(root, "control_enabled", control.enabled);
    cJSON_AddStringToObject(root, "mode_label",
                            !control.enabled ? "Monitoring only" :
                            control.command_authority ? "Commanding" : "Inhibited");
    cJSON_AddStringToObject(root, "inhibit_reason", control.inhibit_reason);

    source_detection_status_t source = {0};
    if (source_detection_get_status(&source) == ESP_OK) {
        cJSON_AddStringToObject(root, "source", source_detection_state_name(source.state));
    }

    float solar_kw = 0.0f;
    bool solar_known = false;
    const uint8_t inverter_count = inverter_manager_get_count();
    bool have_command = false;
    bool all_commanded_online = inverter_count > 0U;
    float highest_command_percent = 0.0f;
    for (uint8_t i = 0U; i < inverter_count; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) {
            all_commanded_online = false;
            continue;
        }
        if (data.telemetry_valid && !data.telemetry_stale && isfinite(data.measured_power_kw)) {
            solar_kw += data.measured_power_kw;
            solar_known = true;
        }
        if (data.has_command && isfinite(data.commanded_percent)) {
            if (!have_command || data.commanded_percent > highest_command_percent) {
                highest_command_percent = data.commanded_percent;
            }
            have_command = true;
        } else {
            all_commanded_online = false;
        }
        if (!data.online) all_commanded_online = false;
    }
    add_number_or_null(root, "solar_kw", solar_kw, solar_known);
    add_number_or_null(root, "commandable_kw", inverter_manager_get_total_rated_kw(), true);

    cJSON *command = cJSON_AddObjectToObject(root, "command");
    if (command && have_command) {
        cJSON_AddNumberToObject(command, "percent", round((double)highest_command_percent));
        cJSON_AddBoolToObject(command, "in_force",
                              control.command_authority && all_commanded_online);
        if (!control.command_authority && control.inhibit_reason[0] != '\0') {
            cJSON_AddStringToObject(command, "blocked_by", control.inhibit_reason);
        }
    }

    meter_data_t meter = {0};
    cJSON_AddBoolToObject(root, "meter_online",
                          meter_manager_get_data(0U, &meter) && meter.online);
    return finish_json(slot, root);
}

static bool build_status(local_api_slot_t *slot)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);

    network_status_t network = {0};
    network_manager_get_status(&network);
    cJSON_AddBoolToObject(root, "network_online", network.network_ready);
    cJSON_AddNumberToObject(root, "rssi", network.rssi);

    const esp_app_desc_t *build = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware_version", build ? build->version : "");

    source_detection_status_t source = {0};
    cJSON *source_json = cJSON_AddObjectToObject(root, "source");
    if (source_detection_get_status(&source) == ESP_OK) {
        cJSON_AddStringToObject(source_json, "attributed_to",
                                source_detection_state_name(source.state));
    } else {
        cJSON_AddStringToObject(source_json, "attributed_to", "unknown");
    }

    cJSON *controller = cJSON_AddObjectToObject(root, "controller");
    cJSON_AddNumberToObject(controller, "uptime_ms", (double)now_ms());
    cJSON_AddStringToObject(controller, "state", controller_resource_state());
    cJSON_AddBoolToObject(controller, "last_reboot_unexpected",
                          reset_was_unexpected(esp_reset_reason()));

    app_config_t config = {0};
    const bool have_config = config_manager_get_snapshot(&config) == ESP_OK;
    meter_data_t meter = {0};
    const bool have_meter = meter_manager_get_data(0U, &meter);
    const uint32_t current_ms = now_ms();
    const bool meter_has_data = have_meter && meter.last_update_ms != 0U &&
                                isfinite(meter.active_power_kw);
    const uint32_t meter_age_ms = meter_has_data ? current_ms - meter.last_update_ms : 0U;
    const uint32_t stale_after = have_config && config.meter_count > 0U
                                     ? meter_stale_after_ms(&config, 0U)
                                     : 1000U;
    const bool meter_stale = !meter_has_data || meter_age_ms > stale_after;
    cJSON_AddBoolToObject(root, "meter_online", have_meter && meter.online);
    cJSON_AddBoolToObject(root, "meter_has_data", meter_has_data);
    cJSON_AddBoolToObject(root, "meter_stale", meter_stale);

    control_status_t control = {0};
    control_engine_get_status(&control);
    cJSON *authority = cJSON_AddObjectToObject(root, "control_authority");
    cJSON_AddStringToObject(authority, "mode_label",
                            !control.enabled ? "Monitoring only" :
                            control.command_authority ? "Commanding" : "Inhibited");
    cJSON_AddStringToObject(authority, "inhibit_reason", control.inhibit_reason);

    const uint32_t alarms = safety_manager_get_alarm_flags();
    cJSON_AddNumberToObject(root, "alarms", alarms);
    cJSON *alarm_names = cJSON_AddArrayToObject(root, "alarm_names");
    if (alarms & SAFETY_ALARM_METER_OFFLINE) {
        cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Meter offline"));
    }
    if (alarms & SAFETY_ALARM_METER_STALE) {
        cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Meter data stale"));
    }
    return finish_json(slot, root);
}

static bool build_meters(local_api_slot_t *slot)
{
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        note_failure(slot, "Core configuration snapshot unavailable");
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);
    const uint32_t current_ms = now_ms();
    uint32_t enabled = 0U;
    uint32_t online = 0U;
    uint32_t stale_or_unavailable = 0U;
    uint32_t initialization_failed = 0U;

    cJSON_AddNumberToObject(root, "configured_count", config.meter_count);
    cJSON *items = cJSON_AddArrayToObject(root, "meters");
    for (uint8_t i = 0U; i < config.meter_count && i < APP_MAX_METERS; ++i) {
        const meter_config_t *meter = &config.meters[i];
        meter_data_t data = {0};
        const bool runtime_available = i < meter_manager_get_count() &&
                                       meter_manager_get_data(i, &data);
        const meter_view_t view = meter_view(&config, i, &data, runtime_available, current_ms);
        if (meter->enabled) enabled++;
        if (view.online) online++;
        if (meter->enabled && !view.online) stale_or_unavailable++;
        if (view.initialization_failed) initialization_failed++;

        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddBoolToObject(item, "enabled", meter->enabled);
        cJSON_AddStringToObject(item, "name", meter->name);
        cJSON_AddStringToObject(item, "role_name", meter_role_name(meter->role));
        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "online", view.online);
        cJSON_AddBoolToObject(runtime, "stale", view.stale);
        cJSON_AddStringToObject(runtime, "state", view.state);
        add_number_or_null(runtime, "active_power_kw", data.active_power_kw,
                           view.has_data && isfinite(data.active_power_kw));
        add_number_or_null(runtime, "data_age_ms", view.data_age_ms, view.has_data);
        cJSON_AddItemToArray(items, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled);
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "stale_or_unavailable", stale_or_unavailable);
    cJSON_AddNumberToObject(summary, "initialization_failed", initialization_failed);
    return finish_json(slot, root);
}

static bool build_inverters(local_api_slot_t *slot)
{
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        note_failure(slot, "Core configuration snapshot unavailable");
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);
    const uint32_t current_ms = now_ms();
    uint32_t enabled = 0U;
    uint32_t online = 0U;
    uint32_t initialization_failed = 0U;
    double configured_rated_kw = 0.0;
    double enabled_rated_kw = 0.0;

    cJSON_AddNumberToObject(root, "configured_count", config.inverter_count);
    cJSON *items = cJSON_AddArrayToObject(root, "inverters");
    for (uint8_t i = 0U; i < config.inverter_count && i < APP_MAX_INVERTERS; ++i) {
        const inverter_config_t *inverter = &config.inverters[i];
        inverter_data_t data = {0};
        const bool runtime_available = i < inverter_manager_get_count() &&
                                       inverter_manager_get_data(i, &data);
        const inverter_view_t view = inverter_view(inverter, &data, runtime_available);
        configured_rated_kw += inverter->rated_power_kw;
        if (inverter->enabled) {
            enabled++;
            enabled_rated_kw += inverter->rated_power_kw;
        }
        if (view.online) online++;
        if (view.initialization_failed) initialization_failed++;

        cJSON *item = cJSON_CreateObject();
        if (!item) break;
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddBoolToObject(item, "enabled", inverter->enabled);
        cJSON_AddStringToObject(item, "name", inverter->name);
        cJSON_AddBoolToObject(item, "telemetry_supported", data.telemetry_supported);
        const bool measured_valid = runtime_available && data.telemetry_valid &&
                                    !data.telemetry_stale && isfinite(data.measured_power_kw);
        add_number_or_null(item, "measured_power_kw", data.measured_power_kw, measured_valid);
        add_number_or_null(item, "measured_age_ms",
                           data.last_telemetry_ms ? current_ms - data.last_telemetry_ms : 0U,
                           data.last_telemetry_ms != 0U);
        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddStringToObject(runtime, "state", view.state);
        add_number_or_null(runtime, "commanded_percent", data.commanded_percent, view.has_command);
        cJSON_AddItemToArray(items, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled);
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "initialization_failed", initialization_failed);
    cJSON_AddNumberToObject(summary, "configured_rated_kw", configured_rated_kw);
    cJSON_AddNumberToObject(summary, "enabled_rated_kw", enabled_rated_kw);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw",
                            inverter_manager_get_total_rated_kw());
    return finish_json(slot, root);
}

static bool build_telemetry(local_api_slot_t *slot)
{
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        note_failure(slot, "Core configuration snapshot unavailable");
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);
    const uint32_t current_ms = now_ms();

    network_status_t network = {0};
    network_manager_get_status(&network);
    cJSON *network_json = cJSON_AddObjectToObject(root, "network");
    cJSON_AddBoolToObject(network_json, "online", network.network_ready);
    cJSON_AddNumberToObject(network_json, "rssi", network.rssi);

    uint32_t enabled_meters = 0U;
    uint32_t online_meters = 0U;
    uint32_t initialization_failed_meters = 0U;
    bool primary_meter_fresh = false;
    float primary_meter_kw = 0.0f;
    const char *primary_meter_state = config.meter_count > 0U ? "unavailable" : "not_configured";
    for (uint8_t i = 0U; i < config.meter_count && i < APP_MAX_METERS; ++i) {
        meter_data_t data = {0};
        const bool runtime_available = i < meter_manager_get_count() &&
                                       meter_manager_get_data(i, &data);
        const meter_view_t view = meter_view(&config, i, &data, runtime_available, current_ms);
        if (config.meters[i].enabled) enabled_meters++;
        if (view.online) online_meters++;
        if (view.initialization_failed) initialization_failed_meters++;
        if (i == 0U) {
            primary_meter_fresh = view.online;
            primary_meter_kw = data.active_power_kw;
            primary_meter_state = view.state;
        }
    }
    cJSON *meters = cJSON_AddObjectToObject(root, "meters");
    cJSON_AddNumberToObject(meters, "configured", config.meter_count);
    cJSON_AddNumberToObject(meters, "enabled", enabled_meters);
    cJSON_AddNumberToObject(meters, "online", online_meters);
    cJSON_AddNumberToObject(meters, "initialization_failed", initialization_failed_meters);

    cJSON *grid = cJSON_AddObjectToObject(root, "grid_meter");
    cJSON_AddStringToObject(grid, "state", primary_meter_state);
    add_number_or_null(grid, "active_power_kw", primary_meter_kw,
                       primary_meter_fresh && isfinite(primary_meter_kw));

    uint32_t enabled_inverters = 0U;
    uint32_t online_inverters = 0U;
    uint32_t initialization_failed_inverters = 0U;
    for (uint8_t i = 0U; i < config.inverter_count && i < APP_MAX_INVERTERS; ++i) {
        inverter_data_t data = {0};
        const bool runtime_available = i < inverter_manager_get_count() &&
                                       inverter_manager_get_data(i, &data);
        const inverter_view_t view = inverter_view(&config.inverters[i], &data, runtime_available);
        if (config.inverters[i].enabled) enabled_inverters++;
        if (view.online) online_inverters++;
        if (view.initialization_failed) initialization_failed_inverters++;
    }
    cJSON *inverters = cJSON_AddObjectToObject(root, "inverters");
    cJSON_AddNumberToObject(inverters, "configured", config.inverter_count);
    cJSON_AddNumberToObject(inverters, "enabled", enabled_inverters);
    cJSON_AddNumberToObject(inverters, "online", online_inverters);
    cJSON_AddNumberToObject(inverters, "initialization_failed", initialization_failed_inverters);

    control_status_t control = {0};
    control_engine_get_status(&control);
    cJSON *availability = cJSON_AddObjectToObject(root, "availability");
    cJSON_AddBoolToObject(availability, "monitoring_ready",
                          network.network_ready && primary_meter_fresh);
    cJSON_AddBoolToObject(availability, "command_path_ready",
                          inverter_manager_get_total_rated_kw() > 0.0f);
    cJSON_AddBoolToObject(availability, "automatic_control_active", control.enabled);
    return finish_json(slot, root);
}

static bool build_operational(local_api_slot_t *slot, bool alarms)
{
    note_failure(slot,
                 alarms
                     ? "Core alarm lifecycle has no public in-process snapshot API"
                     : "Core event history has no public in-process snapshot API");
    return false;
}

bool local_backend_provider_init(screen_api_provider_t *provider)
{
    if (!provider) return false;
    s_logged_first_success = false;

    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        local_api_slot_t *slot = &s_slots[i];
        slot->valid = false;
        slot->consecutive_failures = 0U;
        if (!slot->json) {
            slot->json = heap_caps_malloc(slot->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!slot->json) {
            ESP_LOGE(TAG, "Unable to allocate %u PSRAM bytes for %s",
                     (unsigned)slot->capacity, slot->path);
            local_backend_provider_deinit();
            return false;
        }
        slot->json[0] = '\0';
    }

    provider->context = NULL;
    provider->acquire = provider_acquire;
    provider->release = provider_release;
    ESP_LOGI(TAG, "Read-only in-process Core provider ready; socket/TCP self-transport removed");
    return true;
}

bool local_backend_provider_fetch(const char *path)
{
    local_api_slot_t *slot = slot_for(path);
    if (!slot || !slot->json) return false;
    slot->valid = false;
    slot->json[0] = '\0';

    if (strcmp(path, SCREEN_API_LIVE_PATH) == 0) return build_live(slot);
    if (strcmp(path, SCREEN_API_STATUS_PATH) == 0) return build_status(slot);
    if (strcmp(path, SCREEN_API_METERS_PATH) == 0) return build_meters(slot);
    if (strcmp(path, SCREEN_API_INVERTERS_PATH) == 0) return build_inverters(slot);
    if (strcmp(path, SCREEN_API_TELEMETRY_PATH) == 0) return build_telemetry(slot);
    if (strcmp(path, SCREEN_API_EVENTS_PATH) == 0) return build_operational(slot, false);
    if (strcmp(path, SCREEN_API_ALARMS_PATH) == 0) return build_operational(slot, true);
    note_failure(slot, "unsupported in-process read model");
    return false;
}

bool local_backend_provider_read_commissioning(screen_commissioning_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    /* Current Core no longer has the historical commissioning_gate API.
     * Project only Core's already-evaluated runtime authority and inhibit
     * reason. Never infer production qualification from runtime state. */
    control_status_t control = {0};
    control_engine_get_status(&control);

    out->commissioned = control.command_authority;
    copy_bounded(out->scope, sizeof(out->scope), "current_core_runtime");
    out->production_qualified = false;
    out->automatic_control_permitted = control.command_authority;
    out->command_authority = control.command_authority;
    out->prerequisite_count = 1U;
    out->satisfied_count = control.command_authority ? 1U : 0U;
    out->unmet_count = control.command_authority ? 0U : 1U;
    copy_bounded(out->inhibit_reason, sizeof(out->inhibit_reason), control.inhibit_reason);

    if (control.command_authority) {
        copy_bounded(out->summary, sizeof(out->summary),
                     "Current Core command authority active. LCD does not infer production qualification.");
    } else {
        copy_bounded(out->first_unmet, sizeof(out->first_unmet), "current_core_authority");
        copy_bounded(out->first_unmet_title, sizeof(out->first_unmet_title),
                     "Current Core command authority");
        copy_bounded(out->first_unmet_detail, sizeof(out->first_unmet_detail),
                     control.inhibit_reason[0] != '\0'
                         ? control.inhibit_reason
                         : (control.enabled ? "Current Core has not granted command authority."
                                            : "Automatic control is disabled."));
        copy_bounded(out->summary, sizeof(out->summary),
                     "Current Core authority is inhibited. Production qualification is not inferred by the LCD.");
    }

    out->valid = true;
    return true;
}

void local_backend_provider_deinit(void)
{
    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        free(s_slots[i].json);
        s_slots[i].json = NULL;
        s_slots[i].valid = false;
        s_slots[i].consecutive_failures = 0U;
    }
    s_logged_first_success = false;
}
