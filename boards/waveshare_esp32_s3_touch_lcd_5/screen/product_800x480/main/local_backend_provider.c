#include "local_backend_provider.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "commissioning_gate.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "operational_api.h"
#include "safety_manager.h"
#include "screen_api.h"
#include "source_detection.h"
#include "system_resource_api.h"

/*
 * The native LCD runs on the same MCU as the Product Core.  Self-HTTP looked
 * attractive because it reused the browser routes verbatim, but ESP-IDF HIL
 * proved that neither 127.0.0.1 nor the AP's own address was a reliable
 * controller-to-itself transport in this product: connect/select timed out while
 * the web server itself was healthy.
 *
 * This adapter therefore reads ONLY existing Core snapshots and projects the
 * subset of the established API contracts that screen_api.c already parses.
 * There is no Modbus I/O, no write path, no control decision and no socket/TCP
 * dependency here.  The authoritative source attribution is also not re-derived
 * here: source_detection_attributed_to() is owned by shared Product Core.
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
    /* Exact operational payloads come from the same Core-owned builders as
     * the HTTP API. Events can contain the full 96-entry ring, so keep this
     * slot larger than the LCD's bounded 16-row projection. */
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
    /* Persistent provider-owned PSRAM slot. */
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
        ESP_LOGI(TAG, "Native screen is reading Product Core state in-process");
        s_logged_first_success = true;
    }
    return true;
}

static void copy_bounded(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0U) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void add_number_or_null(cJSON *object, const char *name, double value, bool available)
{
    if (!object || !name) return;
    if (available && isfinite(value)) cJSON_AddNumberToObject(object, name, value);
    else cJSON_AddNullToObject(object, name);
}

static bool source_attribution_available(const source_detection_status_t *source)
{
    return source && source->attributed_to && source->attributed_to[0] != '\0';
}

typedef struct {
    bool enabled;
    bool runtime_available;
    bool online;
    bool stale;
    bool initialization_failed;
    bool has_measurement;
    uint32_t age_ms;
    const char *state;
} meter_view_t;

static meter_view_t meter_view(const app_config_t *config,
                               uint8_t index,
                               const meter_data_t *data,
                               bool runtime_available,
                               uint32_t current_ms)
{
    meter_view_t view = {0};
    if (!config || index >= config->meter_count || index >= APP_MAX_METERS) {
        view.state = "not_configured";
        return view;
    }
    view.enabled = config->meters[index].enabled;
    view.runtime_available = runtime_available;
    view.state = view.enabled ? "waiting" : "disabled";
    if (!view.enabled) return view;
    if (!runtime_available || !data) {
        view.initialization_failed = true;
        view.state = "initialization_failed";
        return view;
    }

    view.has_measurement = data->success_count > 0U && isfinite(data->active_power_kw);
    if (view.has_measurement) view.age_ms = current_ms - data->last_update_ms;
    const uint32_t stale_ms = safety_manager_meter_stale_timeout_ms();
    view.stale = view.has_measurement && stale_ms > 0U && view.age_ms > stale_ms;
    view.online = data->online && view.has_measurement && !view.stale;
    if (view.online) view.state = "online";
    else if (view.stale) view.state = "stale";
    else if (view.has_measurement) view.state = "offline";
    else view.state = "waiting";
    return view;
}

typedef struct {
    bool enabled;
    bool runtime_available;
    bool online;
    bool initialization_failed;
    const char *state;
} inverter_view_t;

static inverter_view_t inverter_view(const inverter_config_t *config,
                                     const inverter_data_t *data,
                                     bool runtime_available)
{
    inverter_view_t view = {0};
    if (!config) {
        view.state = "not_configured";
        return view;
    }
    view.enabled = config->enabled;
    view.runtime_available = runtime_available;
    if (!view.enabled) {
        view.state = "disabled";
        return view;
    }
    if (!runtime_available || !data) {
        view.initialization_failed = true;
        view.state = "initialization_failed";
        return view;
    }
    view.online = data->online;
    view.state = data->state[0] ? data->state : (view.online ? "online" : "offline");
    return view;
}

static bool build_live(local_api_slot_t *slot)
{
    /* Existing Core status only; no control computation here. */
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        note_failure(slot, "Core configuration snapshot unavailable");
        return false;
    }

    control_status_t control = {0};
    control_engine_get_status(&control);
    const uint32_t current_ms = now_ms();

    meter_data_t grid = {0};
    const bool grid_runtime = config.meter_count > 0U && meter_manager_get_count() > 0U &&
                              meter_manager_get_data(0U, &grid);
    const meter_view_t grid_view = meter_view(&config, 0U, &grid, grid_runtime, current_ms);

    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);
    add_number_or_null(root, "grid_kw", grid.active_power_kw, grid_view.online);
    add_number_or_null(root, "solar_kw", inverter_manager_get_total_measured_kw(),
                       inverter_manager_has_measured_power());
    add_number_or_null(root, "requested_pv_kw", control.requested_pv_kw,
                       isfinite(control.requested_pv_kw));
    add_number_or_null(root, "applied_pv_kw", control.applied_pv_kw,
                       isfinite(control.applied_pv_kw));
    add_number_or_null(root, "commandable_kw", inverter_manager_get_commandable_rated_kw(), true);
    cJSON_AddBoolToObject(root, "control_enabled", control.enabled);
    cJSON_AddStringToObject(root, "mode_label", control.mode_label ? control.mode_label : "unknown");
    cJSON_AddStringToObject(root, "inhibit_reason",
                            control.inhibit_reason ? control.inhibit_reason : "");
    cJSON_AddStringToObject(root, "source", control.source ? control.source : "unknown");
    cJSON_AddBoolToObject(root, "meter_online", grid_view.online);
    add_number_or_null(root, "command_percent", control.command_percent,
                       isfinite(control.command_percent));
    cJSON_AddBoolToObject(root, "command_in_force", control.command_in_force);
    cJSON_AddStringToObject(root, "command_blocked_by",
                            control.command_blocked_by ? control.command_blocked_by : "");
    return finish_json(slot, root);
}

static bool build_status(local_api_slot_t *slot)
{
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        note_failure(slot, "Core configuration snapshot unavailable");
        return false;
    }

    const uint32_t current_ms = now_ms();
    network_status_t network = {0};
    network_manager_get_status(&network);
    control_status_t control = {0};
    control_engine_get_status(&control);
    source_detection_status_t source = {0};
    source_detection_get_status(&source);

    meter_data_t primary = {0};
    const bool primary_runtime = config.meter_count > 0U && meter_manager_get_count() > 0U &&
                                 meter_manager_get_data(0U, &primary);
    const meter_view_t primary_view = meter_view(&config, 0U, &primary, primary_runtime, current_ms);

    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);
    cJSON *network_json = cJSON_AddObjectToObject(root, "network");
    cJSON_AddBoolToObject(network_json, "online", network.network_ready);
    cJSON_AddNumberToObject(network_json, "rssi", network.rssi);

    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *firmware = cJSON_AddObjectToObject(root, "firmware");
    cJSON_AddStringToObject(firmware, "version", app ? app->version : "unknown");

    cJSON *source_json = cJSON_AddObjectToObject(root, "source");
    if (source_attribution_available(&source)) {
        cJSON_AddStringToObject(source_json, "attributed_to", source.attributed_to);
    } else {
        cJSON_AddStringToObject(source_json, "attributed_to", "unknown");
    }

    cJSON *controller = cJSON_AddObjectToObject(root, "controller");
    cJSON_AddNumberToObject(controller, "uptime_ms", current_ms);
    cJSON_AddStringToObject(controller, "state", control.controller_state ? control.controller_state : "unknown");
    cJSON_AddBoolToObject(controller, "last_reboot_unexpected", control.last_reboot_unexpected);

    cJSON *meter = cJSON_AddObjectToObject(root, "meter");
    cJSON_AddBoolToObject(meter, "online", primary_view.online);
    cJSON_AddBoolToObject(meter, "has_data", primary_view.has_measurement);
    cJSON_AddBoolToObject(meter, "stale", primary_view.stale);

    const uint32_t alarms = safety_manager_get_alarm_flags();
    cJSON_AddNumberToObject(root, "alarms", alarms);
    cJSON *alarm_names = cJSON_AddArrayToObject(root, "alarm_names");
    if (alarms != 0U) {
        if (alarms & SAFETY_ALARM_METER_OFFLINE) cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Meter offline"));
        if (alarms & SAFETY_ALARM_METER_STALE) cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Meter data stale"));
        if (alarms & SAFETY_ALARM_REVERSE_POWER) cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Reverse power"));
        if (alarms & SAFETY_ALARM_GENERATOR_MIN_LOAD) cJSON_AddItemToArray(alarm_names, cJSON_CreateString("Generator minimum loading"));
    }

    cJSON *control_json = cJSON_AddObjectToObject(root, "control");
    cJSON_AddStringToObject(control_json, "mode_label", control.mode_label ? control.mode_label : "unknown");
    cJSON_AddStringToObject(control_json, "inhibit_reason",
                            control.inhibit_reason ? control.inhibit_reason : "");
    return finish_json(slot, root);
}

static bool build_meters(local_api_slot_t *slot)
{
    app_config_t config = {0};
    if (config_manager_get_snapshot(&config) != ESP_OK) {
        note_failure(slot, "Core configuration snapshot unavailable");
        return false;
    }

    const uint32_t current_ms = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) return finish_json(slot, NULL);
    cJSON *items = cJSON_AddArrayToObject(root, "meters");
    uint32_t enabled = 0U;
    uint32_t online = 0U;
    uint32_t stale_or_unavailable = 0U;
    uint32_t initialization_failed = 0U;

    for (uint8_t i = 0U; i < config.meter_count && i < APP_MAX_METERS; ++i) {
        meter_data_t data = {0};
        const bool runtime_available = i < meter_manager_get_count() &&
                                       meter_manager_get_data(i, &data);
        const meter_view_t view = meter_view(&config, i, &data, runtime_available, current_ms);
        if (view.enabled) enabled++;
        if (view.online) online++;
        if (view.enabled && !view.online) stale_or_unavailable++;
        if (view.initialization_failed) initialization_failed++;

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddBoolToObject(item, "enabled", config.meters[i].enabled);
        cJSON_AddStringToObject(item, "name", config.meters[i].name);
        cJSON_AddStringToObject(item, "role_name", meter_role_name(config.meters[i].role));
        cJSON_AddStringToObject(item, "state", view.state ? view.state : "unknown");
        cJSON_AddBoolToObject(item, "online", view.online);
        cJSON_AddBoolToObject(item, "stale", view.stale);
        add_number_or_null(item, "active_power_kw", data.active_power_kw, view.online);
        if (view.has_measurement) cJSON_AddNumberToObject(item, "data_age_ms", view.age_ms);
        else cJSON_AddNullToObject(item, "data_age_ms");
        cJSON_AddItemToArray(items, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "configured", config.meter_count);
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
    cJSON *items = cJSON_AddArrayToObject(root, "inverters");
    uint32_t enabled = 0U;
    uint32_t online = 0U;
    uint32_t initialization_failed = 0U;
    double configured_rated_kw = 0.0;
    double enabled_rated_kw = 0.0;

    for (uint8_t i = 0U; i < config.inverter_count && i < APP_MAX_INVERTERS; ++i) {
        const inverter_config_t *cfg = &config.inverters[i];
        inverter_data_t data = {0};
        const bool runtime_available = i < inverter_manager_get_count() &&
                                       inverter_manager_get_data(i, &data);
        const inverter_view_t view = inverter_view(cfg, &data, runtime_available);
        if (isfinite(cfg->rated_power_kw) && cfg->rated_power_kw > 0.0f) configured_rated_kw += cfg->rated_power_kw;
        if (cfg->enabled) {
            enabled++;
            if (isfinite(cfg->rated_power_kw) && cfg->rated_power_kw > 0.0f) enabled_rated_kw += cfg->rated_power_kw;
        }
        if (view.online) online++;
        if (view.initialization_failed) initialization_failed++;

        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddBoolToObject(item, "enabled", cfg->enabled);
        cJSON_AddStringToObject(item, "name", cfg->name);
        cJSON_AddBoolToObject(item, "telemetry_supported", data.telemetry_supported);
        add_number_or_null(item, "measured_power_kw", data.measured_power_kw,
                           data.telemetry_supported && data.telemetry_valid && isfinite(data.measured_power_kw));
        if (data.telemetry_supported && data.telemetry_valid) {
            cJSON_AddNumberToObject(item, "measured_age_ms", data.measured_age_ms);
        } else {
            cJSON_AddNullToObject(item, "measured_age_ms");
        }
        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddStringToObject(runtime, "state", view.state ? view.state : "unknown");
        add_number_or_null(runtime, "commanded_percent", data.commanded_percent,
                           data.has_commanded_percent && isfinite(data.commanded_percent));
        cJSON_AddItemToArray(items, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "configured", config.inverter_count);
    cJSON_AddNumberToObject(summary, "enabled", enabled);
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "initialization_failed", initialization_failed);
    cJSON_AddNumberToObject(summary, "configured_rated_kw", configured_rated_kw);
    cJSON_AddNumberToObject(summary, "enabled_rated_kw", enabled_rated_kw);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw",
                            inverter_manager_get_commandable_rated_kw());
    return finish_json(slot, root);
}

static bool build_operational(local_api_slot_t *slot, bool alarms)
{
    cJSON *root = alarms ? operational_api_build_alarms_json()
                         : operational_api_build_events_json();
    if (!root) {
        note_failure(slot, alarms ? "Core alarm snapshot unavailable"
                                  : "Core event snapshot unavailable");
        return false;
    }
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

bool local_backend_provider_init(screen_api_provider_t *provider)
{
    if (!provider) return false;
    s_logged_first_success = false;

    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        local_api_slot_t *slot = &s_slots[i];
        slot->valid = false;
        slot->consecutive_failures = 0U;
        if (slot->capacity == 0U) continue;
        if (!slot->json) {
            /* These buffers are native-HMI transport storage, not safety/control
             * state. Never fall back to scarce internal DRAM if PSRAM allocation
             * fails: the correct failure is an unavailable LCD read model while
             * Product Core continues headless/fail-closed. */
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
    if (!slot) return false;
    if (!slot->json) return false;
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

    /* Exact authority behind GET /api/commissioning/gate. This adapter projects
     * the result; it never evaluates prerequisites itself. */
    commissioning_status_t status = {0};
    control_engine_get_commissioning(&status);
    control_status_t control = {0};
    control_engine_get_status(&control);

    out->commissioned = status.commissioned;
    copy_bounded(out->scope, sizeof(out->scope), commissioning_scope_label(status.scope));
    out->production_qualified = status.scope == COMMISSIONING_SCOPE_PRODUCTION;
    out->automatic_control_permitted = status.commissioned && control.command_authority;
    out->command_authority = control.command_authority;
    out->prerequisite_count = COMMISSIONING_PREREQ_COUNT;
    out->satisfied_count = status.satisfied_count;
    out->unmet_count = status.unmet_count;
    copy_bounded(out->summary, sizeof(out->summary), commissioning_gate_summary(&status));
    copy_bounded(out->inhibit_reason, sizeof(out->inhibit_reason), control.inhibit_reason);

    if (!status.commissioned && status.first_unmet < COMMISSIONING_PREREQ_COUNT) {
        copy_bounded(out->first_unmet, sizeof(out->first_unmet),
                     commissioning_prereq_id(status.first_unmet));
        copy_bounded(out->first_unmet_title, sizeof(out->first_unmet_title),
                     commissioning_prereq_title(status.first_unmet));
        copy_bounded(out->first_unmet_detail, sizeof(out->first_unmet_detail),
                     commissioning_reason_message(status.results[status.first_unmet].reason));
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
