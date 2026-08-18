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
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    if (value) *value = item->valuedouble;
    return true;
}

static bool read_bool(const cJSON *root, const char *name, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsBool(item)) return fallback;
    return cJSON_IsTrue(item);
}

bool screen_api_parse_live_json(const char *json, screen_live_snapshot_t *out)
{
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

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
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    out->network_online = read_bool(root, "network_online", false);
    out->meter_online = read_bool(root, "meter_online", false);
    out->meter_has_data = read_bool(root, "meter_has_data", false);
    out->meter_stale = read_bool(root, "meter_stale", true);

    double number = 0.0;
    if (read_number(root, "rssi", &number)) out->rssi = (int)number;
    if (read_number(root, "alarms", &number) && number >= 0.0) out->alarms = (uint32_t)number;

    copy_text(out->firmware_version, sizeof(out->firmware_version),
              cJSON_GetObjectItemCaseSensitive(root, "firmware_version"));

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
