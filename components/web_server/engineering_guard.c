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
    float configured_kw = 0.0f;
    float enabled_kw = 0.0f;
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "operator_view", true);
    cJSON_AddNumberToObject(root, "configured_count", config->inverter_count);
    cJSON_AddBoolToObject(root, "measured_power_supported", true);
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
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddStringToObject(item, "name", config->inverters[i].name);
        cJSON_AddBoolToObject(item, "enabled", config->inverters[i].enabled);
        cJSON_AddNumberToObject(item, "rated_kw", config->inverters[i].rated_power_kw);
        cJSON_AddBoolToObject(item, "telemetry_supported", have && data.telemetry_supported);
        if (have && data.telemetry_valid) cJSON_AddNumberToObject(item, "measured_power_kw", data.measured_power_kw);
        else cJSON_AddNullToObject(item, "measured_power_kw");
        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", have);
        cJSON_AddBoolToObject(runtime, "online", have && data.online);
        cJSON_AddBoolToObject(runtime, "telemetry_valid", have && data.telemetry_valid);
        cJSON_AddStringToObject(runtime, "state", have && data.online ? "online" : config->inverters[i].enabled ? "unavailable" : "disabled");
        cJSON_AddItemToArray(items, item);
    }
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled);
    cJSON_AddNumberToObject(summary, "online", online);
    cJSON_AddNumberToObject(summary, "configured_rated_kw", configured_kw);
    cJSON_AddNumberToObject(summary, "enabled_rated_kw", enabled_kw);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw", inverter_manager_get_total_rated_kw());
    cJSON_AddNumberToObject(summary, "command_tested", 0);
    cJSON_AddNumberToObject(summary, "last_write_ok", 0);
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
    return strcmp(uri, "/") == 0 || strcmp(uri, "/app.css") == 0 || strcmp(uri, "/app.js") == 0 ||
           strcmp(uri, "/api/status") == 0 || strcmp(uri, "/api/telemetry") == 0 ||
           strncmp(uri, "/api/engineering/", 17) == 0;
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
