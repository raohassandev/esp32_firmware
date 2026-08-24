#include "screen_runtime.h"

#include <string.h>

#include "screen_api.h"
#include "screen_app.h"

static screen_api_provider_t s_provider;
static bool s_initialized;

/* Some operator payloads contain bounded row arrays. Keeping these snapshots in
 * module storage prevents a refresh from spending several kilobytes of the LVGL
 * task stack. Only the screen runtime owns them and refreshes are serialized by
 * the board integration/LVGL lock contract. */
static screen_live_snapshot_t s_live;
static screen_status_snapshot_t s_status;
static screen_meters_snapshot_t s_meters;
static screen_inverters_snapshot_t s_inverters;
static screen_telemetry_snapshot_t s_telemetry;
static screen_events_snapshot_t s_events;
static screen_alarms_snapshot_t s_alarms;

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
    memset(&s_live, 0, sizeof(s_live));
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_meters, 0, sizeof(s_meters));
    memset(&s_inverters, 0, sizeof(s_inverters));
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    memset(&s_events, 0, sizeof(s_events));
    memset(&s_alarms, 0, sizeof(s_alarms));
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

    const bool ok = screen_api_parse_live_json(json, &s_live);
    release_payload(SCREEN_API_LIVE_PATH, json);
    if (!ok) {
        screen_app_show_live_unavailable();
        return false;
    }
    screen_app_apply_live(&s_live);
    return true;
}

bool screen_runtime_refresh_status(void)
{
    bool all_ok = true;
    const char *json = NULL;

    if (get_payload(SCREEN_API_STATUS_PATH, &json)) {
        const bool ok = screen_api_parse_status_json(json, &s_status);
        release_payload(SCREEN_API_STATUS_PATH, json);
        if (ok) screen_app_apply_status(&s_status);
        else all_ok = false;
    } else {
        all_ok = false;
    }

    json = NULL;
    if (get_payload(SCREEN_API_TELEMETRY_PATH, &json)) {
        const bool ok = screen_api_parse_telemetry_json(json, &s_telemetry);
        release_payload(SCREEN_API_TELEMETRY_PATH, json);
        if (ok) screen_app_apply_telemetry(&s_telemetry);
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
        const bool ok = screen_api_parse_meters_json(json, &s_meters);
        release_payload(SCREEN_API_METERS_PATH, json);
        if (ok) screen_app_apply_meters(&s_meters);
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
        const bool ok = screen_api_parse_inverters_json(json, &s_inverters);
        release_payload(SCREEN_API_INVERTERS_PATH, json);
        if (ok) screen_app_apply_inverters(&s_inverters);
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
        const bool ok = screen_api_parse_alarms_json(json, &s_alarms);
        release_payload(SCREEN_API_ALARMS_PATH, json);
        if (ok) screen_app_apply_alarms(&s_alarms);
        else all_ok = false;
    } else {
        all_ok = false;
    }

    json = NULL;
    if (get_payload(SCREEN_API_EVENTS_PATH, &json)) {
        const bool ok = screen_api_parse_events_json(json, &s_events);
        release_payload(SCREEN_API_EVENTS_PATH, json);
        if (ok) screen_app_apply_events(&s_events);
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
