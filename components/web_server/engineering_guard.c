/* The component normally replaces URI registration with the engineering
 * gateway. This translation unit implements that gateway and must see the real
 * ESP-IDF declaration before any headers are parsed. */
#ifdef httpd_register_uri_handler
#undef httpd_register_uri_handler
#endif

#include "engineering_auth.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "control_engine.h"
#include "inverter_json.h"
#include "meter_json.h"
#include "meter_manager.h"

#define GATEWAY_MODE_PROTECTED 0
#define GATEWAY_MODE_SAFE_CONFIG 1
#define GATEWAY_MODE_SAFE_METERS 2
#define GATEWAY_MODE_SAFE_INVERTERS 3
#define GATEWAY_MODE_SAFE_INVERTER_TELEMETRY 4

typedef struct {
    httpd_uri_func original;
    uint8_t mode;
} engineering_route_context_t;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, text);
    free(text);
    return err;
}

static esp_err_t safe_config(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    if (config_manager_get_snapshot(config) != ESP_OK) {
        free(config);
        return httpd_resp_send_500(request);
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }
    cJSON_AddNumberToObject(root, "schema", config->version);
    cJSON_AddStringToObject(root, "device_name", config->device_name);
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddBoolToObject(root, "engineering_details_hidden", true);

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *primary = cJSON_AddObjectToObject(wifi, "primary");
    cJSON_AddBoolToObject(primary, "enabled", false);
    cJSON_AddStringToObject(primary, "ssid", "");
    cJSON_AddStringToObject(primary, "password", "");
    cJSON_AddNumberToObject(primary, "ip_mode", 0);
    cJSON *fallback = cJSON_AddObjectToObject(wifi, "fallback");
    cJSON_AddBoolToObject(fallback, "enabled", false);
    cJSON_AddStringToObject(fallback, "ssid", "");
    cJSON_AddStringToObject(fallback, "password", "");
    cJSON_AddNumberToObject(fallback, "ip_mode", 0);

    cJSON *meters = cJSON_AddArrayToObject(root, "meters");
    for (uint8_t i = 0; i < config->meter_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", config->meters[i].name);
        cJSON_AddBoolToObject(item, "enabled", config->meters[i].enabled);
        cJSON_AddItemToArray(meters, item);
    }
    cJSON *inverters = cJSON_AddArrayToObject(root, "inverters");
    for (uint8_t i = 0; i < config->inverter_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", config->inverters[i].name);
        cJSON_AddBoolToObject(item, "enabled", config->inverters[i].enabled);
        cJSON_AddNumberToObject(item, "rated_kw", config->inverters[i].rated_power_kw);
        cJSON_AddItemToArray(inverters, item);
    }
    cJSON *control = cJSON_AddObjectToObject(root, "control");
    cJSON_AddBoolToObject(control, "enabled", config->control.enabled);
    free(config);
    return send_json(request, root);
}

static esp_err_t safe_meters(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    if (config_manager_get_snapshot(config) != ESP_OK) {
        free(config);
        return httpd_resp_send_500(request);
    }
    uint32_t current = now_ms();
    uint8_t enabled = 0;
    uint8_t online = 0;
    uint8_t unavailable = 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddNumberToObject(root, "configured_count", config->meter_count);
    cJSON *items = cJSON_AddArrayToObject(root, "meters");
    for (uint8_t i = 0; i < config->meter_count; ++i) {
        meter_data_t data = {0};
        bool have = meter_manager_get_data(i, &data);
        bool fresh = have && data.online && data.last_update_ms && current - data.last_update_ms <= 5000U;
        if (config->meters[i].enabled) enabled++;
        if (fresh) online++;
        else if (config->meters[i].enabled) unavailable++;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddStringToObject(item, "name", config->meters[i].name);
        cJSON_AddBoolToObject(item, "enabled", config->meters[i].enabled);
        /*
         * WHAT THIS METER MEASURES, in the operator view too.
         *
         * The same principle as the measurements below: the gate withholds how
         * the firmware TALKS to the instrument -- hosts, unit ids, register
         * addresses -- not what the instrument is. The declared role is what
         * decides which page a reading is drawn on, and without it every meter
         * landed on one page, so a generator's output appeared under a heading
         * reading "Grid power" -- the screen asserting the plant was importing
         * from the utility while it burned diesel.
         *
         * Withholding it protects nothing and mislabels a measurement, which is
         * the one thing this product must never do.
         */
        cJSON_AddNumberToObject(item, "role", config->meters[i].role);
        cJSON_AddStringToObject(item, "role_name", meter_role_name(config->meters[i].role));
        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", have);
        cJSON_AddBoolToObject(runtime, "online", fresh);
        cJSON_AddBoolToObject(runtime, "has_data", have && data.last_update_ms != 0);
        cJSON_AddBoolToObject(runtime, "stale", have && data.last_update_ms && !fresh);
        if (have && data.last_update_ms) {
            cJSON_AddNumberToObject(runtime, "active_power_kw", data.active_power_kw);
            cJSON_AddNumberToObject(runtime, "data_age_ms", current - data.last_update_ms);
        } else {
            cJSON_AddNullToObject(runtime, "active_power_kw");
            cJSON_AddNullToObject(runtime, "data_age_ms");
        }
        cJSON_AddStringToObject(runtime, "state", fresh ? "online" : "unavailable");
        /*
         * WHAT THE METER SAYS, in the operator view too, through the same
         * serializer the engineering view uses.
         *
         * The gate exists to withhold how the firmware TALKS to the meter --
         * hosts, unit ids, register addresses -- not what the meter reports.
         * Voltage, current, power factor, frequency and the energy counters are
         * printed on the instrument's own front panel and are exactly the
         * evidence a plant owner uses to satisfy themselves the controller is
         * working. Hiding them here would keep the proof from the reader it
         * exists for, while protecting nothing.
         *
         * Shared serializer, not a second copy: two hand-written versions of
         * "volts, per phase, null when absent" drift, and the first symptom is
         * an operator and an engineer reading different numbers off the same
         * instrument while standing at the same panel.
         */
        meter_json_add_phase_power(runtime, &data, have && data.last_update_ms != 0);
        meter_json_add_measurements(item, &data, current);
        meter_json_add_energy(item, &data, current);
        cJSON_AddItemToArray(items, item);
    }
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled);
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "stale_or_unavailable", unavailable);
    cJSON_AddNumberToObject(summary, "initialization_failed", 0);
    cJSON_AddNumberToObject(summary, "with_data", online);
    free(config);
    return send_json(request, root);
}

static esp_err_t safe_inverters(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    if (config_manager_get_snapshot(config) != ESP_OK) {
        free(config);
        return httpd_resp_send_500(request);
    }
    uint8_t enabled = 0;
    uint8_t online = 0;
    uint8_t command_tested = 0;
    uint8_t last_write_ok = 0;
    float configured_kw = 0.0f;
    float enabled_kw = 0.0f;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddNumberToObject(root, "configured_count", config->inverter_count);
    /* Filled in below from what the profiles actually describe. It was
     * hardcoded true, which claims a measurement the machine may never
     * have been asked for. */
    uint8_t measured_supported = 0;
    cJSON *items = cJSON_AddArrayToObject(root, "inverters");
    for (uint8_t i = 0; i < config->inverter_count; ++i) {
        inverter_data_t data = {0};
        bool have = inverter_manager_get_data(i, &data);
        configured_kw += config->inverters[i].rated_power_kw;
        if (config->inverters[i].enabled) {
            enabled++;
            enabled_kw += config->inverters[i].rated_power_kw;
        }
        if (have && data.online) online++;
        /* Counted, not assumed. Both figures below were literal zeroes. */
        if (have && data.has_command) {
            command_tested++;
            if (data.online) last_write_ok++;
        }
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddStringToObject(item, "name", config->inverters[i].name);
        cJSON_AddBoolToObject(item, "enabled", config->inverters[i].enabled);
        cJSON_AddNumberToObject(item, "rated_kw", config->inverters[i].rated_power_kw);
        if (have && data.telemetry_supported) measured_supported++;
        cJSON_AddBoolToObject(item, "telemetry_supported", have && data.telemetry_supported);
        if (have && data.telemetry_valid) cJSON_AddNumberToObject(item, "measured_power_kw", data.measured_power_kw);
        else cJSON_AddNullToObject(item, "measured_power_kw");
        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", have);
        cJSON_AddBoolToObject(runtime, "online", have && data.online);
        cJSON_AddBoolToObject(runtime, "telemetry_valid", have && data.telemetry_valid);
        cJSON_AddStringToObject(runtime, "state", have && data.online ? "online" : config->inverters[i].enabled ? "unavailable" : "disabled");
        /* What the MACHINE reports, in the operator view too. An operator page
         * that shows only the commanded percentage shows this firmware's own
         * belief and calls it a plant reading; the measured block is the only
         * thing on the page that is evidence. Same serializer as the
         * engineering view -- see inverter_json.h. */
        inverter_json_add_measurements(item, &data, now_ms());

        /*
         * WHAT THE CONTROLLER WOULD SEND, in the operator view too -- but only
         * the PERCENTAGE.
         *
         * The register address, the function code and the raw word are how the
         * firmware talks to the machine, and that is exactly what this gate
         * exists to withhold. The percentage is not: it is what the controller
         * has decided this inverter should produce, and whether it will actually
         * be sent. A plant owner asking "is it going to curtail?" is asking that
         * question, and it was answerable nowhere.
         */
        control_status_t control_status = {0};
        control_engine_get_status(&control_status);
        inverter_command_preview_t preview = {0};
        /* requested, not applied: the preview must answer before automatic
         * control is armed. See the same choice in device_api.c. */
        if (inverter_manager_preview_command(i, control_status.requested_pv_kw, &preview)) {
            cJSON *would = cJSON_AddObjectToObject(item, "command_preview");
            cJSON_AddBoolToObject(would, "available", preview.available);
            if (preview.available) {
                cJSON_AddNumberToObject(would, "percent", preview.percent);
                cJSON_AddNumberToObject(would, "share_kw", preview.share_kw);
            }
            cJSON_AddBoolToObject(would, "would_write", preview.would_write);
            if (preview.blocked_by) {
                cJSON_AddStringToObject(would, "blocked_by", preview.blocked_by);
            } else {
                cJSON_AddNullToObject(would, "blocked_by");
            }
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddBoolToObject(root, "measured_power_supported", measured_supported > 0);

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled);
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "configured_rated_kw", configured_kw);
    cJSON_AddNumberToObject(summary, "enabled_rated_kw", enabled_kw);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw", inverter_manager_get_total_rated_kw());
    /*
     * MEASURED, AFTER BEING HARDCODED TO ZERO.
     *
     * These two were written as literal zeroes in the operator projection, so a
     * plant whose setpoint the machine confirmed on every pass -- verdict
     * CONFIRMED on setpoint readback, twice a second, with production tracking
     * the limit -- still reported "0 command tested, 0 last write ok" to anyone
     * not logged in as engineering. The one screen an owner actually looks at
     * said the commands had never been proven, and nothing in the firmware
     * disagreed with it out loud.
     *
     * Withholding these was never what this gate is for. It hides the register
     * address, the function code and the raw word; whether a setpoint was
     * confirmed is a plant fact, and the same argument is already made above for
     * the commanded percentage.
     */
    cJSON_AddNumberToObject(summary, "command_tested", command_tested);
    cJSON_AddNumberToObject(summary, "last_write_ok", last_write_ok);
    cJSON_AddNumberToObject(summary, "initialization_failed", 0);
    free(config);
    return send_json(request, root);
}

static esp_err_t safe_inverter_telemetry(httpd_req_t *request)
{
    uint8_t count = inverter_manager_get_count();
    uint8_t online = 0;
    uint8_t valid = 0;
    uint8_t stale = 0;
    float total = 0.0f;
    uint32_t current = now_ms();
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddBoolToObject(root, "read_only_endpoint", true);
    cJSON_AddBoolToObject(root, "writes_issued", false);
    cJSON_AddNumberToObject(root, "count", count);
    cJSON *items = cJSON_AddArrayToObject(root, "inverters");
    for (uint8_t i = 0; i < count; ++i) {
        inverter_data_t data = {0};
        if (!inverter_manager_get_data(i, &data)) continue;
        if (data.online) online++;
        if (data.telemetry_valid) {
            valid++;
            total += data.measured_power_kw;
        }
        if (data.telemetry_stale) stale++;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddBoolToObject(item, "online", data.online);
        cJSON_AddBoolToObject(item, "telemetry_valid", data.telemetry_valid);
        cJSON_AddBoolToObject(item, "telemetry_stale", data.telemetry_stale);
        if (data.telemetry_valid) {
            cJSON_AddNumberToObject(item, "measured_power_kw", data.measured_power_kw);
            cJSON_AddNumberToObject(item, "telemetry_age_ms", current - data.last_telemetry_ms);
        } else {
            cJSON_AddNullToObject(item, "measured_power_kw");
            cJSON_AddNullToObject(item, "telemetry_age_ms");
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "telemetry_valid", valid);
    cJSON_AddNumberToObject(summary, "stale", stale);
    cJSON_AddNumberToObject(summary, "measured_total_kw", total);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw", inverter_manager_get_total_rated_kw());
    return send_json(request, root);
}

esp_err_t engineering_auth_guarded_handler(httpd_req_t *request)
{
    engineering_route_context_t *context = (engineering_route_context_t *)request->user_ctx;
    if (!context || !context->original) return httpd_resp_send_500(request);
    if (engineering_auth_is_authorized(request)) return context->original(request);
    switch (context->mode) {
        case GATEWAY_MODE_SAFE_CONFIG: return safe_config(request);
        case GATEWAY_MODE_SAFE_METERS: return safe_meters(request);
        case GATEWAY_MODE_SAFE_INVERTERS: return safe_inverters(request);
        case GATEWAY_MODE_SAFE_INVERTER_TELEMETRY: return safe_inverter_telemetry(request);
        default: return engineering_auth_require(request);
    }
}

static bool public_uri(const char *uri)
{
    return strcmp(uri, "/") == 0 || strcmp(uri, "/favicon.ico") == 0 ||
           strcmp(uri, "/app.css") == 0 || strcmp(uri, "/app.js") == 0 ||
           strcmp(uri, "/api/status") == 0 || strcmp(uri, "/api/telemetry") == 0 ||
           /* The operator screens' fast path. Every value on it is already on
            * /api/status, which is public for the same reason: these are the
            * figures the plant overview draws, and a controller whose own
            * overview cannot draw itself without a session is not an operator
            * interface. It carries no endpoint, no register, no credential and
            * no configuration -- see live_api.c for what is deliberately left
            * out. */
           strcmp(uri, "/api/live") == 0 ||
           strncmp(uri, "/api/engineering/", 17) == 0 ||
           /* The two operator network controls. Deliberately the narrowest pair
            * that lets a site owner move their own controller onto a different
            * Wi-Fi: list what is in range, and join one. The recovery AP
            * passphrase, static addressing and the fallback profile all stay
            * behind an engineering session, so a unit can be moved but never
            * locked away. See network_join_post() for what each exclusion
            * prevents. */
           strcmp(uri, "/api/network/scan") == 0 ||
           strcmp(uri, "/api/network/join") == 0;
}

esp_err_t engineering_register_uri_handler(httpd_handle_t server, const httpd_uri_t *uri_handler)
{
    if (!server || !uri_handler || !uri_handler->uri || !uri_handler->handler) return ESP_ERR_INVALID_ARG;
    if (public_uri(uri_handler->uri)) return httpd_register_uri_handler(server, uri_handler);

    uint8_t mode = GATEWAY_MODE_PROTECTED;
    if (uri_handler->method == HTTP_GET && strcmp(uri_handler->uri, "/api/config") == 0) mode = GATEWAY_MODE_SAFE_CONFIG;
    else if (uri_handler->method == HTTP_GET && strcmp(uri_handler->uri, "/api/meters") == 0) mode = GATEWAY_MODE_SAFE_METERS;
    else if (uri_handler->method == HTTP_GET && strcmp(uri_handler->uri, "/api/inverters") == 0) mode = GATEWAY_MODE_SAFE_INVERTERS;
    else if (uri_handler->method == HTTP_GET && strcmp(uri_handler->uri, "/api/inverter-telemetry") == 0) mode = GATEWAY_MODE_SAFE_INVERTER_TELEMETRY;

    engineering_route_context_t *context = calloc(1, sizeof(*context));
    if (!context) return ESP_ERR_NO_MEM;
    context->original = uri_handler->handler;
    context->mode = mode;

    httpd_uri_t guarded = *uri_handler;
    guarded.handler = engineering_auth_guarded_handler;
    guarded.user_ctx = context;
    esp_err_t err = httpd_register_uri_handler(server, &guarded);
    if (err != ESP_OK) free(context);
    return err;
}
