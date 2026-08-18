#include "screen_runtime.h"

#include <string.h>

#include "screen_api.h"
#include "screen_app.h"

static screen_api_provider_t s_provider;
static bool s_initialized;

typedef bool (*parse_fn_t)(const char *json, void *out);

static bool get_payload(const char *path, const char **json)
{
    if (!s_initialized || !s_provider.acquire || !path || !json) return false;
    *json = NULL;
    return s_provider.acquire(s_provider.context, path, json) && *json != NULL;
}

static void release_payload(const char *path, const char *json)
{
    if (s_provider.release && json) s_provider.release(s_provider.context, path, json);
}

bool screen_runtime_init(const screen_api_provider_t *provider)
{
    memset(&s_provider, 0, sizeof(s_provider));
    s_initialized = false;
    if (!provider || !provider->acquire) return false;
    s_provider = *provider;
    s_initialized = true;
    return true;
}

bool screen_runtime_refresh_fast(void)
{
    const char *json = NULL;
    if (!get_payload(SCREEN_API_LIVE_PATH, &json)) {
        screen_app_show_live_unavailable();
        return false;
    }

    screen_live_snapshot_t snapshot;
    const bool ok = screen_api_parse_live_json(json, &snapshot);
    release_payload(SCREEN_API_LIVE_PATH, json);
    if (!ok) {
        screen_app_show_live_unavailable();
        return false;
    }
    screen_app_apply_live(&snapshot);
    return true;
}

bool screen_runtime_refresh_status(void)
{
    bool all_ok = true;
    const char *json = NULL;

    if (get_payload(SCREEN_API_STATUS_PATH, &json)) {
        screen_status_snapshot_t status;
        const bool ok = screen_api_parse_status_json(json, &status);
        release_payload(SCREEN_API_STATUS_PATH, json);
        if (ok) screen_app_apply_status(&status);
        else all_ok = false;
    } else {
        all_ok = false;
    }

    json = NULL;
    if (get_payload(SCREEN_API_TELEMETRY_PATH, &json)) {
        screen_telemetry_snapshot_t telemetry;
        const bool ok = screen_api_parse_telemetry_json(json, &telemetry);
        release_payload(SCREEN_API_TELEMETRY_PATH, json);
        if (ok) screen_app_apply_telemetry(&telemetry);
        else all_ok = false;
    } else {
        all_ok = false;
    }

    if (!all_ok) screen_app_show_readiness_unavailable();
    return all_ok;
}

bool screen_runtime_refresh_devices(void)
{
    bool all_ok = true;
    const char *json = NULL;

    if (get_payload(SCREEN_API_METERS_PATH, &json)) {
        screen_meters_snapshot_t meters;
        const bool ok = screen_api_parse_meters_json(json, &meters);
        release_payload(SCREEN_API_METERS_PATH, json);
        if (ok) screen_app_apply_meters(&meters);
        else {
            screen_app_show_meters_unavailable();
            all_ok = false;
        }
    } else {
        screen_app_show_meters_unavailable();
        all_ok = false;
    }

    json = NULL;
    if (get_payload(SCREEN_API_INVERTERS_PATH, &json)) {
        screen_inverters_snapshot_t inverters;
        const bool ok = screen_api_parse_inverters_json(json, &inverters);
        release_payload(SCREEN_API_INVERTERS_PATH, json);
        if (ok) screen_app_apply_inverters(&inverters);
        else {
            screen_app_show_inverters_unavailable();
            all_ok = false;
        }
    } else {
        screen_app_show_inverters_unavailable();
        all_ok = false;
    }
    return all_ok;
}

bool screen_runtime_refresh_operations(void)
{
    bool all_ok = true;
    const char *json = NULL;

    if (get_payload(SCREEN_API_ALARMS_PATH, &json)) {
        screen_alarms_snapshot_t alarms;
        const bool ok = screen_api_parse_alarms_json(json, &alarms);
        release_payload(SCREEN_API_ALARMS_PATH, json);
        if (ok) screen_app_apply_alarms(&alarms);
        else all_ok = false;
    } else {
        all_ok = false;
    }

    json = NULL;
    if (get_payload(SCREEN_API_EVENTS_PATH, &json)) {
        screen_events_snapshot_t events;
        const bool ok = screen_api_parse_events_json(json, &events);
        release_payload(SCREEN_API_EVENTS_PATH, json);
        if (ok) screen_app_apply_events(&events);
        else all_ok = false;
    } else {
        all_ok = false;
    }

    if (!all_ok) screen_app_show_operations_unavailable();
    return all_ok;
}

bool screen_runtime_refresh_all(void)
{
    const bool fast = screen_runtime_refresh_fast();
    const bool status = screen_runtime_refresh_status();
    const bool devices = screen_runtime_refresh_devices();
    const bool operations = screen_runtime_refresh_operations();
    return fast && status && devices && operations;
}
