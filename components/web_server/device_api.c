#include "device_api.h"
#include <stdlib.h>
#include "cJSON.h"
#include "config_manager.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "profile_manager.h"

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static void add_endpoint(cJSON *parent, const modbus_endpoint_t *endpoint)
{
    cJSON *object = cJSON_AddObjectToObject(parent, "endpoint");
    cJSON_AddStringToObject(object, "host", endpoint->host);
    cJSON_AddNumberToObject(object, "port", endpoint->port);
    cJSON_AddNumberToObject(object, "unit_id", endpoint->unit_id);
    cJSON_AddNumberToObject(object, "timeout_ms", endpoint->timeout_ms);
}

static void add_age(cJSON *parent, const char *name, bool available,
                    uint32_t current_ms, uint32_t event_ms)
{
    if (available) cJSON_AddNumberToObject(parent, name, current_ms - event_ms);
    else cJSON_AddNullToObject(parent, name);
}

static uint32_t stale_after_ms(uint32_t poll_interval_ms)
{
    uint64_t derived = (uint64_t)poll_interval_ms * 3ULL;
    if (derived < 1000ULL) derived = 1000ULL;
    if (derived > UINT32_MAX) derived = UINT32_MAX;
    return (uint32_t)derived;
}

static uint32_t meter_stale_after_ms(const app_config_t *config, uint8_t index)
{
    if (index == 0 && config->control.meter_stale_timeout_ms > 0) {
        return config->control.meter_stale_timeout_ms;
    }
    return stale_after_ms(config->meters[index].poll_interval_ms);
}

static esp_err_t meters_get(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    esp_err_t err = config_manager_get_snapshot(config);
    if (err != ESP_OK) {
        free(config);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Meter configuration unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }

    uint32_t current_ms = now_ms();
    uint8_t enabled_count = 0;
    uint8_t online_count = 0;
    uint8_t stale_count = 0;
    uint8_t data_count = 0;
    uint8_t initialization_failed_count = 0;

    cJSON_AddNumberToObject(root, "generated_ms", current_ms);
    cJSON_AddNumberToObject(root, "configured_count", config->meter_count);
    cJSON *meters = cJSON_AddArrayToObject(root, "meters");

    for (uint8_t index = 0; index < config->meter_count; ++index) {
        const meter_config_t *meter = &config->meters[index];
        meter_data_t data = {0};
        bool runtime_available = index < meter_manager_get_count() &&
                                 meter_manager_get_data(index, &data);
        bool enabled = meter->enabled;
        bool connection_initialized = runtime_available && data.connection_initialized;
        bool initialization_failed = enabled &&
                                     (!connection_initialized ||
                                      (data.last_attempt_ms == 0 && data.last_error != ESP_OK));
        bool has_data = runtime_available && data.last_update_ms != 0;
        uint32_t stale_limit_ms = meter_stale_after_ms(config, index);
        uint32_t age_ms = has_data ? current_ms - data.last_update_ms : 0;
        bool stale = enabled && !initialization_failed &&
                     (!has_data || age_ms > stale_limit_ms);
        bool online = enabled && !initialization_failed && runtime_available &&
                      data.online && !stale;

        if (enabled) enabled_count++;
        if (online) online_count++;
        if (stale) stale_count++;
        if (has_data) data_count++;
        if (initialization_failed) initialization_failed_count++;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", meter->name);
        cJSON_AddBoolToObject(item, "enabled", enabled);
        add_endpoint(item, &meter->endpoint);

        cJSON *acquisition = cJSON_AddObjectToObject(item, "acquisition");
        cJSON_AddNumberToObject(acquisition, "function", meter->function_code);
        cJSON_AddNumberToObject(acquisition, "pdu_address", meter->active_power_address);
        cJSON_AddNumberToObject(acquisition, "data_type", meter->active_power_type);
        cJSON_AddNumberToObject(acquisition, "word_order", meter->active_power_order);
        cJSON_AddNumberToObject(acquisition, "scale", meter->active_power_scale);
        cJSON_AddNumberToObject(acquisition, "poll_ms", meter->poll_interval_ms);
        cJSON_AddNumberToObject(acquisition, "stale_after_ms", stale_limit_ms);

        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", runtime_available);
        cJSON_AddBoolToObject(runtime, "connection_initialized", connection_initialized);
        cJSON_AddBoolToObject(runtime, "initialization_failed", initialization_failed);
        cJSON_AddBoolToObject(runtime, "online", online);
        cJSON_AddBoolToObject(runtime, "has_data", has_data);
        cJSON_AddBoolToObject(runtime, "stale", stale);
        if (has_data) cJSON_AddNumberToObject(runtime, "active_power_kw", data.active_power_kw);
        else cJSON_AddNullToObject(runtime, "active_power_kw");
        add_age(runtime, "data_age_ms", has_data, current_ms, data.last_update_ms);
        add_age(runtime, "last_attempt_age_ms",
                runtime_available && data.last_attempt_ms != 0,
                current_ms, data.last_attempt_ms);
        cJSON_AddNumberToObject(runtime, "success_count", data.success_count);
        cJSON_AddNumberToObject(runtime, "error_count", data.response_errors);
        cJSON_AddNumberToObject(runtime, "consecutive_failures", data.consecutive_failures);
        cJSON_AddNumberToObject(runtime, "last_error", data.last_error);
        cJSON_AddStringToObject(runtime, "last_error_name", esp_err_to_name(data.last_error));
        cJSON_AddStringToObject(runtime, "state",
                                !enabled ? "disabled" :
                                initialization_failed ? "initialization_failed" :
                                online ? "online" : has_data ? "stale" : "unavailable");
        cJSON_AddItemToArray(meters, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled_count);
    cJSON_AddNumberToObject(summary, "online", online_count);
    cJSON_AddNumberToObject(summary, "stale_or_unavailable",
                            stale_count + initialization_failed_count);
    cJSON_AddNumberToObject(summary, "initialization_failed", initialization_failed_count);
    cJSON_AddNumberToObject(summary, "with_data", data_count);

    free(config);
    return send_json(request, root);
}

static void add_telemetry_mapping(cJSON *item,
                                  const inverter_telemetry_profile_t *profile,
                                  uint32_t stale_limit_ms)
{
    cJSON *telemetry = cJSON_AddObjectToObject(item, "telemetry");
    cJSON_AddBoolToObject(telemetry, "enabled", profile->enabled);
    cJSON_AddNumberToObject(telemetry, "function",
                            profile->active_power.function_code);
    cJSON_AddNumberToObject(telemetry, "pdu_address",
                            profile->active_power.address);
    cJSON_AddNumberToObject(telemetry, "data_type",
                            profile->active_power.data_type);
    cJSON_AddNumberToObject(telemetry, "word_order",
                            profile->active_power.word_order);
    cJSON_AddNumberToObject(telemetry, "scale",
                            profile->active_power.scale);
    cJSON_AddNumberToObject(telemetry, "offset",
                            profile->active_power.offset);
    cJSON_AddNumberToObject(telemetry, "poll_ms",
                            profile->active_power.poll_interval_ms);
    cJSON_AddNumberToObject(telemetry, "stale_after_ms", stale_limit_ms);
}

static esp_err_t inverters_get(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    inverter_telemetry_profile_set_t *profile_set = malloc(sizeof(*profile_set));
    if (!config || !profile_set) {
        free(config);
        free(profile_set);
        return httpd_resp_send_500(request);
    }
    esp_err_t err = config_manager_get_snapshot(config);
    if (err == ESP_OK) {
        err = profile_manager_get_inverter_telemetry_set(profile_set);
    }
    if (err != ESP_OK) {
        free(config);
        free(profile_set);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Inverter configuration unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        free(profile_set);
        return httpd_resp_send_500(request);
    }

    uint32_t current_ms = now_ms();
    uint8_t enabled_count = 0;
    uint8_t command_tested_count = 0;
    uint8_t last_write_ok_count = 0;
    uint8_t initialization_failed_count = 0;
    uint8_t telemetry_enabled_count = 0;
    uint8_t telemetry_online_count = 0;
    uint8_t telemetry_problem_count = 0;
    uint8_t telemetry_data_count = 0;
    float configured_rated_kw = 0.0f;
    float enabled_rated_kw = 0.0f;
    float fresh_measured_power_kw = 0.0f;

    cJSON_AddNumberToObject(root, "generated_ms", current_ms);
    cJSON_AddNumberToObject(root, "configured_count", config->inverter_count);
    cJSON_AddBoolToObject(root, "measured_power_supported", true);
    cJSON *inverters = cJSON_AddArrayToObject(root, "inverters");

    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        const inverter_config_t *inverter = &config->inverters[index];
        const inverter_telemetry_profile_t *profile = &profile_set->inverters[index];
        inverter_data_t data = {0};
        bool runtime_available = index < inverter_manager_get_count() &&
                                 inverter_manager_get_data(index, &data);
        bool enabled = inverter->enabled;
        bool connection_initialized = runtime_available && data.connection_initialized;
        bool initialization_failed = enabled && !connection_initialized;
        bool has_command = runtime_available && data.has_command;
        bool last_write_ok = has_command && data.online;

        bool telemetry_enabled = enabled && profile->enabled;
        bool telemetry_has_data = runtime_available &&
                                  data.telemetry_last_update_ms != 0;
        uint32_t telemetry_stale_limit = stale_after_ms(
            profile->active_power.poll_interval_ms);
        uint32_t telemetry_age_ms = telemetry_has_data
            ? current_ms - data.telemetry_last_update_ms : 0;
        bool telemetry_stale = telemetry_enabled && !initialization_failed &&
            (!telemetry_has_data || telemetry_age_ms > telemetry_stale_limit);
        bool telemetry_online = telemetry_enabled && !initialization_failed &&
            data.telemetry_online && !telemetry_stale;

        configured_rated_kw += inverter->rated_power_kw;
        if (enabled) {
            enabled_count++;
            enabled_rated_kw += inverter->rated_power_kw;
        }
        if (has_command) command_tested_count++;
        if (last_write_ok) last_write_ok_count++;
        if (initialization_failed) initialization_failed_count++;
        if (telemetry_enabled) telemetry_enabled_count++;
        if (telemetry_online) {
            telemetry_online_count++;
            fresh_measured_power_kw += data.active_power_kw;
        }
        if (telemetry_enabled && !telemetry_online) telemetry_problem_count++;
        if (telemetry_has_data) telemetry_data_count++;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", inverter->name);
        cJSON_AddBoolToObject(item, "enabled", enabled);
        cJSON_AddNumberToObject(item, "rated_kw", inverter->rated_power_kw);
        cJSON_AddBoolToObject(item, "telemetry_supported", true);
        if (telemetry_has_data) {
            cJSON_AddNumberToObject(item, "measured_power_kw", data.active_power_kw);
        } else {
            cJSON_AddNullToObject(item, "measured_power_kw");
        }
        cJSON_AddBoolToObject(item, "measured_power_stale", telemetry_stale);
        add_endpoint(item, &inverter->endpoint);
        add_telemetry_mapping(item, profile, telemetry_stale_limit);

        cJSON *telemetry_runtime = cJSON_AddObjectToObject(item, "telemetry_runtime");
        cJSON_AddBoolToObject(telemetry_runtime, "online", telemetry_online);
        cJSON_AddBoolToObject(telemetry_runtime, "has_data", telemetry_has_data);
        cJSON_AddBoolToObject(telemetry_runtime, "stale", telemetry_stale);
        add_age(telemetry_runtime, "data_age_ms", telemetry_has_data,
                current_ms, data.telemetry_last_update_ms);
        add_age(telemetry_runtime, "last_attempt_age_ms",
                runtime_available && data.telemetry_last_attempt_ms != 0,
                current_ms, data.telemetry_last_attempt_ms);
        cJSON_AddNumberToObject(telemetry_runtime, "success_count",
                                data.telemetry_successes);
        cJSON_AddNumberToObject(telemetry_runtime, "error_count",
                                data.telemetry_errors);
        cJSON_AddNumberToObject(telemetry_runtime, "consecutive_failures",
                                data.telemetry_consecutive_failures);
        cJSON_AddNumberToObject(telemetry_runtime, "last_error",
                                data.telemetry_last_error);
        cJSON_AddStringToObject(telemetry_runtime, "last_error_name",
                                esp_err_to_name(data.telemetry_last_error));
        cJSON_AddStringToObject(telemetry_runtime, "state",
            !enabled ? "device_disabled" :
            !profile->enabled ? "profile_disabled" :
            initialization_failed ? "initialization_failed" :
            telemetry_online ? "online" :
            telemetry_has_data ? "stale" : "unavailable");

        cJSON *command = cJSON_AddObjectToObject(item, "command");
        cJSON_AddNumberToObject(command, "limit_pdu_address", inverter->power_limit_address);
        cJSON_AddNumberToObject(command, "function", inverter->power_limit_function);
        cJSON_AddNumberToObject(command, "raw_units_per_percent", inverter->raw_units_per_percent);
        cJSON_AddNumberToObject(command, "minimum_percent", inverter->minimum_percent);
        cJSON_AddNumberToObject(command, "maximum_percent", inverter->maximum_percent);

        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", runtime_available);
        cJSON_AddBoolToObject(runtime, "connection_initialized", connection_initialized);
        cJSON_AddBoolToObject(runtime, "initialization_failed", initialization_failed);
        cJSON_AddBoolToObject(runtime, "has_command", has_command);
        if (has_command) {
            cJSON_AddBoolToObject(runtime, "last_write_ok", last_write_ok);
            cJSON_AddNumberToObject(runtime, "commanded_percent", data.commanded_percent);
            cJSON_AddNumberToObject(runtime, "commanded_power_kw", data.commanded_power_kw);
        } else {
            cJSON_AddNullToObject(runtime, "last_write_ok");
            cJSON_AddNullToObject(runtime, "commanded_percent");
            cJSON_AddNullToObject(runtime, "commanded_power_kw");
        }
        add_age(runtime, "last_command_age_ms", has_command,
                current_ms, data.last_command_ms);
        cJSON_AddNumberToObject(runtime, "write_successes", data.write_successes);
        cJSON_AddNumberToObject(runtime, "write_errors", data.write_errors);
        cJSON_AddNumberToObject(runtime, "last_error", data.last_error);
        cJSON_AddStringToObject(runtime, "last_error_name", esp_err_to_name(data.last_error));
        cJSON_AddStringToObject(runtime, "state",
                                !enabled ? "disabled" :
                                initialization_failed ? "initialization_failed" :
                                !has_command ? "not_tested" :
                                last_write_ok ? "last_write_ok" : "last_write_failed");
        cJSON_AddItemToArray(inverters, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled_count);
    cJSON_AddNumberToObject(summary, "configured_rated_kw", configured_rated_kw);
    cJSON_AddNumberToObject(summary, "enabled_rated_kw", enabled_rated_kw);
    cJSON_AddNumberToObject(summary, "commandable_rated_kw", inverter_manager_get_total_rated_kw());
    cJSON_AddNumberToObject(summary, "command_tested", command_tested_count);
    cJSON_AddNumberToObject(summary, "last_write_ok", last_write_ok_count);
    cJSON_AddNumberToObject(summary, "initialization_failed", initialization_failed_count);
    cJSON_AddNumberToObject(summary, "telemetry_enabled", telemetry_enabled_count);
    cJSON_AddNumberToObject(summary, "telemetry_online", telemetry_online_count);
    cJSON_AddNumberToObject(summary, "telemetry_stale_or_unavailable", telemetry_problem_count);
    cJSON_AddNumberToObject(summary, "telemetry_with_data", telemetry_data_count);
    if (telemetry_online_count > 0) {
        cJSON_AddNumberToObject(summary, "fresh_measured_power_kw",
                                fresh_measured_power_kw);
    } else {
        cJSON_AddNullToObject(summary, "fresh_measured_power_kw");
    }

    free(config);
    free(profile_set);
    return send_json(request, root);
}

esp_err_t device_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/meters", .method = HTTP_GET, .handler = meters_get},
        {.uri = "/api/inverters", .method = HTTP_GET, .handler = inverters_get}
    };

    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            "device_api", "handler registration failed");
    }
    return ESP_OK;
}
