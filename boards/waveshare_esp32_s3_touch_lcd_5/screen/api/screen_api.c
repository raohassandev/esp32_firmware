#include "screen_api.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"

static void copy_text(char *destination, size_t capacity, const cJSON *item)
{
    if (!destination || capacity == 0U) return;
    destination[0] = '\0';
    if (!cJSON_IsString(item) || !item->valuestring) return;
    size_t length = strlen(item->valuestring);
    if (length >= capacity) length = capacity - 1U;
    memcpy(destination, item->valuestring, length);
    destination[length] = '\0';
}

static bool read_number(const cJSON *root, const char *name, double *value)
{
    if (!cJSON_IsObject(root)) return false;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    if (value) *value = item->valuedouble;
    return true;
}

static uint32_t read_u32(const cJSON *root, const char *name, uint32_t fallback)
{
    double value = 0.0;
    if (!read_number(root, name, &value) || value < 0.0 || value > 4294967295.0) return fallback;
    return (uint32_t)value;
}

static bool read_bool(const cJSON *root, const char *name, bool fallback)
{
    if (!cJSON_IsObject(root)) return fallback;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsBool(item)) return fallback;
    return cJSON_IsTrue(item);
}

static cJSON *parse_object(const char *json)
{
    if (!json) return NULL;
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

bool screen_api_parse_live_json(const char *json, screen_live_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_object(json);
    if (!root) return false;

    out->has_grid_kw = read_number(root, "grid_kw", &out->grid_kw);
    out->has_solar_kw = read_number(root, "solar_kw", &out->solar_kw);
    out->has_requested_pv_kw = read_number(root, "requested_pv_kw", &out->requested_pv_kw);
    out->has_applied_pv_kw = read_number(root, "applied_pv_kw", &out->applied_pv_kw);
    out->has_commandable_kw = read_number(root, "commandable_kw", &out->commandable_kw);
    out->control_enabled = read_bool(root, "control_enabled", false);
    out->meter_online = read_bool(root, "meter_online", false);

    copy_text(out->mode_label, sizeof(out->mode_label),
              cJSON_GetObjectItemCaseSensitive(root, "mode_label"));
    copy_text(out->inhibit_reason, sizeof(out->inhibit_reason),
              cJSON_GetObjectItemCaseSensitive(root, "inhibit_reason"));
    copy_text(out->source, sizeof(out->source),
              cJSON_GetObjectItemCaseSensitive(root, "source"));

    const cJSON *command = cJSON_GetObjectItemCaseSensitive(root, "command");
    if (cJSON_IsObject(command)) {
        out->has_command_percent = read_number(command, "percent", &out->command_percent);
        out->command_in_force = read_bool(command, "in_force", false);
        copy_text(out->command_blocked_by, sizeof(out->command_blocked_by),
                  cJSON_GetObjectItemCaseSensitive(command, "blocked_by"));
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool screen_api_parse_status_json(const char *json, screen_status_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    /* Unknown status is conservative until the payload proves otherwise. */
    out->meter_stale = true;

    cJSON *root = parse_object(json);
    if (!root) return false;

    out->network_online = read_bool(root, "network_online", false);
    out->meter_online = read_bool(root, "meter_online", false);
    out->meter_has_data = read_bool(root, "meter_has_data", false);
    out->meter_stale = read_bool(root, "meter_stale", true);

    double number = 0.0;
    if (read_number(root, "rssi", &number)) out->rssi = (int)number;
    if (read_number(root, "alarms", &number) && number >= 0.0) out->alarms = (uint32_t)number;

    copy_text(out->firmware_version, sizeof(out->firmware_version),
              cJSON_GetObjectItemCaseSensitive(root, "firmware_version"));

    const cJSON *names = cJSON_GetObjectItemCaseSensitive(root, "alarm_names");
    if (cJSON_IsArray(names)) {
        const int count = cJSON_GetArraySize(names);
        for (int i = 0; i < count && out->alarm_name_count < SCREEN_API_MAX_ALARM_NAMES; ++i) {
            const cJSON *item = cJSON_GetArrayItem(names, i);
            if (!cJSON_IsString(item)) continue;
            copy_text(out->alarm_names[out->alarm_name_count], SCREEN_API_ALARM_NAME_MAX, item);
            if (out->alarm_names[out->alarm_name_count][0] != '\0') out->alarm_name_count++;
        }
    }

    const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
    if (cJSON_IsObject(source)) {
        copy_text(out->source_attributed_to, sizeof(out->source_attributed_to),
                  cJSON_GetObjectItemCaseSensitive(source, "attributed_to"));
    }

    const cJSON *controller = cJSON_GetObjectItemCaseSensitive(root, "controller");
    if (cJSON_IsObject(controller)) {
        if (read_number(controller, "uptime_ms", &number) && number >= 0.0) {
            out->controller_uptime_ms = (uint64_t)number;
        }
        copy_text(out->controller_state, sizeof(out->controller_state),
                  cJSON_GetObjectItemCaseSensitive(controller, "state"));
        out->last_reboot_unexpected = read_bool(controller, "last_reboot_unexpected", false);
    }

    const cJSON *authority = cJSON_GetObjectItemCaseSensitive(root, "control_authority");
    if (cJSON_IsObject(authority)) {
        copy_text(out->control_mode_label, sizeof(out->control_mode_label),
                  cJSON_GetObjectItemCaseSensitive(authority, "mode_label"));
        copy_text(out->control_inhibit_reason, sizeof(out->control_inhibit_reason),
                  cJSON_GetObjectItemCaseSensitive(authority, "inhibit_reason"));
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool screen_api_parse_meters_json(const char *json, screen_meters_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_object(json);
    if (!root) return false;

    out->configured_count = read_u32(root, "configured_count", 0U);
    const cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "summary");
    if (cJSON_IsObject(summary)) {
        out->enabled_count = read_u32(summary, "enabled", 0U);
        out->online_count = read_u32(summary, "online", 0U);
        out->stale_or_unavailable_count = read_u32(summary, "stale_or_unavailable", 0U);
        out->initialization_failed_count = read_u32(summary, "initialization_failed", 0U);
    }

    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "meters");
    if (cJSON_IsArray(items)) {
        const int count = cJSON_GetArraySize(items);
        out->truncated = count > (int)SCREEN_API_MAX_METERS;
        for (int i = 0; i < count && out->row_count < SCREEN_API_MAX_METERS; ++i) {
            const cJSON *item = cJSON_GetArrayItem(items, i);
            if (!cJSON_IsObject(item)) continue;
            screen_meter_row_t *row = &out->rows[out->row_count];
            row->index = (uint8_t)read_u32(item, "index", (uint32_t)i);
            row->enabled = read_bool(item, "enabled", false);
            copy_text(row->name, sizeof(row->name), cJSON_GetObjectItemCaseSensitive(item, "name"));
            copy_text(row->role_name, sizeof(row->role_name),
                      cJSON_GetObjectItemCaseSensitive(item, "role_name"));

            const cJSON *runtime = cJSON_GetObjectItemCaseSensitive(item, "runtime");
            if (cJSON_IsObject(runtime)) {
                row->online = read_bool(runtime, "online", false);
                row->stale = read_bool(runtime, "stale", true);
                copy_text(row->state, sizeof(row->state),
                          cJSON_GetObjectItemCaseSensitive(runtime, "state"));
                row->has_power_kw = read_number(runtime, "active_power_kw", &row->power_kw);
                double age = 0.0;
                row->has_data_age_ms = read_number(runtime, "data_age_ms", &age) && age >= 0.0;
                if (row->has_data_age_ms) row->data_age_ms = (uint32_t)age;
            }
            out->row_count++;
        }
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool screen_api_parse_inverters_json(const char *json, screen_inverters_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_object(json);
    if (!root) return false;

    out->configured_count = read_u32(root, "configured_count", 0U);
    const cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "summary");
    if (cJSON_IsObject(summary)) {
        out->enabled_count = read_u32(summary, "enabled", 0U);
        out->online_count = read_u32(summary, "online", 0U);
        out->initialization_failed_count = read_u32(summary, "initialization_failed", 0U);
        out->has_configured_rated_kw = read_number(summary, "configured_rated_kw", &out->configured_rated_kw);
        out->has_enabled_rated_kw = read_number(summary, "enabled_rated_kw", &out->enabled_rated_kw);
        out->has_commandable_rated_kw = read_number(summary, "commandable_rated_kw", &out->commandable_rated_kw);
    }

    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "inverters");
    if (cJSON_IsArray(items)) {
        const int count = cJSON_GetArraySize(items);
        out->truncated = count > (int)SCREEN_API_MAX_INVERTERS;
        for (int i = 0; i < count && out->row_count < SCREEN_API_MAX_INVERTERS; ++i) {
            const cJSON *item = cJSON_GetArrayItem(items, i);
            if (!cJSON_IsObject(item)) continue;
            screen_inverter_row_t *row = &out->rows[out->row_count];
            row->index = (uint8_t)read_u32(item, "index", (uint32_t)i);
            row->enabled = read_bool(item, "enabled", false);
            row->telemetry_supported = read_bool(item, "telemetry_supported", false);
            copy_text(row->name, sizeof(row->name), cJSON_GetObjectItemCaseSensitive(item, "name"));
            row->has_measured_power_kw = read_number(item, "measured_power_kw", &row->measured_power_kw);
            double age = 0.0;
            row->has_measured_age_ms = read_number(item, "measured_age_ms", &age) && age >= 0.0;
            if (row->has_measured_age_ms) row->measured_age_ms = (uint32_t)age;

            const cJSON *runtime = cJSON_GetObjectItemCaseSensitive(item, "runtime");
            if (cJSON_IsObject(runtime)) {
                copy_text(row->state, sizeof(row->state),
                          cJSON_GetObjectItemCaseSensitive(runtime, "state"));
                row->has_commanded_percent = read_number(runtime, "commanded_percent", &row->commanded_percent);
            }
            out->row_count++;
        }
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool screen_api_parse_telemetry_json(const char *json, screen_telemetry_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_object(json);
    if (!root) return false;

    const cJSON *network = cJSON_GetObjectItemCaseSensitive(root, "network");
    if (cJSON_IsObject(network)) {
        out->network_online = read_bool(network, "online", false);
        double rssi = 0.0;
        if (read_number(network, "rssi", &rssi)) out->rssi = (int)rssi;
    }

    const cJSON *grid = cJSON_GetObjectItemCaseSensitive(root, "grid_meter");
    if (cJSON_IsObject(grid)) {
        copy_text(out->grid_state, sizeof(out->grid_state),
                  cJSON_GetObjectItemCaseSensitive(grid, "state"));
        out->has_grid_power_kw = read_number(grid, "active_power_kw", &out->grid_power_kw);
    }

    const cJSON *meters = cJSON_GetObjectItemCaseSensitive(root, "meters");
    if (cJSON_IsObject(meters)) {
        out->meters_configured = read_u32(meters, "configured", 0U);
        out->meters_enabled = read_u32(meters, "enabled", 0U);
        out->meters_online = read_u32(meters, "online", 0U);
        out->meters_initialization_failed = read_u32(meters, "initialization_failed", 0U);
    }

    const cJSON *inverters = cJSON_GetObjectItemCaseSensitive(root, "inverters");
    if (cJSON_IsObject(inverters)) {
        out->inverters_configured = read_u32(inverters, "configured", 0U);
        out->inverters_enabled = read_u32(inverters, "enabled", 0U);
        out->inverters_initialization_failed = read_u32(inverters, "initialization_failed", 0U);
    }

    const cJSON *availability = cJSON_GetObjectItemCaseSensitive(root, "availability");
    if (cJSON_IsObject(availability)) {
        out->monitoring_ready = read_bool(availability, "monitoring_ready", false);
        out->command_path_ready = read_bool(availability, "command_path_ready", false);
        out->automatic_control_active = read_bool(availability, "automatic_control_active", false);
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool screen_api_parse_events_json(const char *json, screen_events_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_object(json);
    if (!root) return false;

    const cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "summary");
    if (cJSON_IsObject(summary)) {
        out->active_critical = read_u32(summary, "active_critical", 0U);
        out->active_warning = read_u32(summary, "active_warning", 0U);
        out->stored_events = read_u32(summary, "stored_events", 0U);
    }

    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "events");
    if (cJSON_IsArray(items)) {
        const int count = cJSON_GetArraySize(items);
        out->truncated = count > (int)SCREEN_API_MAX_EVENTS;
        for (int i = 0; i < count && out->row_count < SCREEN_API_MAX_EVENTS; ++i) {
            const cJSON *item = cJSON_GetArrayItem(items, i);
            if (!cJSON_IsObject(item)) continue;
            screen_event_row_t *row = &out->rows[out->row_count];
            row->sequence = read_u32(item, "sequence", 0U);
            row->age_ms = read_u32(item, "age_ms", 0U);
            row->active = read_bool(item, "active", false);
            copy_text(row->severity, sizeof(row->severity), cJSON_GetObjectItemCaseSensitive(item, "severity"));
            copy_text(row->kind, sizeof(row->kind), cJSON_GetObjectItemCaseSensitive(item, "kind"));
            copy_text(row->state, sizeof(row->state), cJSON_GetObjectItemCaseSensitive(item, "state"));
            copy_text(row->title, sizeof(row->title), cJSON_GetObjectItemCaseSensitive(item, "title"));
            copy_text(row->detail, sizeof(row->detail), cJSON_GetObjectItemCaseSensitive(item, "detail"));
            copy_text(row->recommended_action, sizeof(row->recommended_action),
                      cJSON_GetObjectItemCaseSensitive(item, "recommended_action"));
            out->row_count++;
        }
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}

bool screen_api_parse_alarms_json(const char *json, screen_alarms_snapshot_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_object(json);
    if (!root) return false;

    const cJSON *summary = cJSON_GetObjectItemCaseSensitive(root, "summary");
    if (cJSON_IsObject(summary)) {
        out->active_count = read_u32(summary, "active", 0U);
        out->unacknowledged_count = read_u32(summary, "unacknowledged", 0U);
        out->primary_active_count = read_u32(summary, "primary_active", 0U);
        out->consequential_active_count = read_u32(summary, "consequential_active", 0U);
    }

    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "alarms");
    if (cJSON_IsArray(items)) {
        const int count = cJSON_GetArraySize(items);
        out->truncated = count > (int)SCREEN_API_MAX_ALARMS;
        for (int i = 0; i < count && out->row_count < SCREEN_API_MAX_ALARMS; ++i) {
            const cJSON *item = cJSON_GetArrayItem(items, i);
            if (!cJSON_IsObject(item)) continue;
            screen_alarm_row_t *row = &out->rows[out->row_count];
            row->code = read_u32(item, "code", 0U);
            row->present = read_bool(item, "present", false);
            row->acknowledged = read_bool(item, "acknowledged", false);
            row->stale = read_bool(item, "stale", false);
            row->shelved = read_bool(item, "shelved", false);
            row->suppressed_by_design = read_bool(item, "suppressed_by_design", false);
            row->out_of_service = read_bool(item, "out_of_service", false);
            copy_text(row->id, sizeof(row->id), cJSON_GetObjectItemCaseSensitive(item, "id"));
            copy_text(row->title, sizeof(row->title), cJSON_GetObjectItemCaseSensitive(item, "title"));
            copy_text(row->severity, sizeof(row->severity), cJSON_GetObjectItemCaseSensitive(item, "severity"));
            copy_text(row->priority, sizeof(row->priority), cJSON_GetObjectItemCaseSensitive(item, "priority"));
            copy_text(row->state, sizeof(row->state), cJSON_GetObjectItemCaseSensitive(item, "state"));
            copy_text(row->role, sizeof(row->role), cJSON_GetObjectItemCaseSensitive(item, "role"));
            copy_text(row->caused_by, sizeof(row->caused_by), cJSON_GetObjectItemCaseSensitive(item, "caused_by"));
            copy_text(row->recommended_action, sizeof(row->recommended_action),
                      cJSON_GetObjectItemCaseSensitive(item, "recommended_action"));
            out->row_count++;
        }
    }

    out->valid = true;
    cJSON_Delete(root);
    return true;
}
