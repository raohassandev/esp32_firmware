#include "device_api.h"

#include <stdlib.h>
#include "cJSON.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "meter_json.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"

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

static uint32_t meter_stale_after_ms(const app_config_t *config, uint8_t index)
{
    const meter_config_t *meter = &config->meters[index];
    if (index == 0 && config->control.meter_stale_timeout_ms > 0) {
        return config->control.meter_stale_timeout_ms;
    }

    uint64_t derived = (uint64_t)meter->poll_interval_ms * 3ULL;
    if (derived < 1000ULL) derived = 1000ULL;
    if (derived > UINT32_MAX) derived = UINT32_MAX;
    return (uint32_t)derived;
}

typedef struct {
    bool runtime_available;
    bool connection_initialized;
    bool initialization_failed;
    bool has_data;
    bool stale;
    bool online;
    uint32_t stale_after_ms;
    uint32_t data_age_ms;
    const char *state;
} meter_health_t;

static meter_health_t meter_health(const app_config_t *config, uint8_t index,
                                   const meter_data_t *data, bool runtime_available,
                                   uint32_t current_ms)
{
    const meter_config_t *meter = &config->meters[index];
    meter_health_t health = {0};
    health.runtime_available = runtime_available;
    health.connection_initialized = runtime_available && data->connection_initialized;
    health.initialization_failed = meter->enabled &&
                                   (!health.connection_initialized ||
                                    (data->last_attempt_ms == 0 && data->last_error != ESP_OK));
    health.has_data = runtime_available && data->last_update_ms != 0;
    health.stale_after_ms = meter_stale_after_ms(config, index);
    health.data_age_ms = health.has_data ? current_ms - data->last_update_ms : 0;
    health.stale = meter->enabled && !health.initialization_failed &&
                   (!health.has_data || health.data_age_ms > health.stale_after_ms);
    health.online = meter->enabled && !health.initialization_failed &&
                    runtime_available && data->online && !health.stale;
    health.state = !meter->enabled ? "disabled" :
                   health.initialization_failed ? "initialization_failed" :
                   health.online ? "online" :
                   health.has_data ? "stale" : "unavailable";
    return health;
}

typedef struct {
    bool runtime_available;
    bool connection_initialized;
    bool initialization_failed;
    bool has_command;
    bool last_write_ok;
    const char *state;
} inverter_health_t;

static inverter_health_t inverter_health(const inverter_config_t *inverter,
                                         const inverter_data_t *data,
                                         bool runtime_available)
{
    inverter_health_t health = {0};
    health.runtime_available = runtime_available;
    health.connection_initialized = runtime_available && data->connection_initialized;
    health.initialization_failed = inverter->enabled && !health.connection_initialized;
    health.has_command = runtime_available && data->has_command;
    health.last_write_ok = health.has_command && data->online;
    health.state = !inverter->enabled ? "disabled" :
                   health.initialization_failed ? "initialization_failed" :
                   !health.has_command ? "not_tested" :
                   health.last_write_ok ? "last_write_ok" : "last_write_failed";
    return health;
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
    uint8_t stale_or_unavailable_count = 0;
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
        meter_health_t health = meter_health(config, index, &data, runtime_available, current_ms);

        if (meter->enabled) enabled_count++;
        if (health.online) online_count++;
        if (meter->enabled && !health.online) stale_or_unavailable_count++;
        if (health.has_data) data_count++;
        if (health.initialization_failed) initialization_failed_count++;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", meter->name);
        cJSON_AddBoolToObject(item, "enabled", meter->enabled);
        /* The commissioned model, reported as both the stored value and its slug
         * so an operator can see what the controller believes is wired -- and
         * see "undeclared" when nothing has been stated. */
        cJSON_AddNumberToObject(item, "model", meter->model);
        cJSON_AddStringToObject(item, "model_name", meter_model_name(meter->model));
        cJSON_AddBoolToObject(item, "model_in_phase_scope",
                              meter_model_in_phase_scope(meter->model));
        add_endpoint(item, &meter->endpoint);

        cJSON *acquisition = cJSON_AddObjectToObject(item, "acquisition");
        cJSON_AddNumberToObject(acquisition, "function", meter->function_code);
        cJSON_AddNumberToObject(acquisition, "pdu_address", meter->active_power_address);
        cJSON_AddNumberToObject(acquisition, "data_type", meter->active_power_type);
        cJSON_AddNumberToObject(acquisition, "word_order", meter->active_power_order);
        cJSON_AddNumberToObject(acquisition, "scale", meter->active_power_scale);
        cJSON_AddNumberToObject(acquisition, "poll_ms", meter->poll_interval_ms);
        cJSON_AddNumberToObject(acquisition, "stale_after_ms", health.stale_after_ms);

        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", health.runtime_available);
        cJSON_AddBoolToObject(runtime, "connection_initialized", health.connection_initialized);
        cJSON_AddBoolToObject(runtime, "initialization_failed", health.initialization_failed);
        cJSON_AddBoolToObject(runtime, "online", health.online);
        cJSON_AddBoolToObject(runtime, "has_data", health.has_data);
        cJSON_AddBoolToObject(runtime, "stale", health.stale);
        if (health.has_data) cJSON_AddNumberToObject(runtime, "active_power_kw", data.active_power_kw);
        else cJSON_AddNullToObject(runtime, "active_power_kw");
        add_age(runtime, "data_age_ms", health.has_data, current_ms, data.last_update_ms);
        add_age(runtime, "last_attempt_age_ms",
                runtime_available && data.last_attempt_ms != 0,
                current_ms, data.last_attempt_ms);
        cJSON_AddNumberToObject(runtime, "success_count", data.success_count);
        cJSON_AddNumberToObject(runtime, "error_count", data.response_errors);
        cJSON_AddNumberToObject(runtime, "consecutive_failures", data.consecutive_failures);
        cJSON_AddNumberToObject(runtime, "last_error", data.last_error);
        cJSON_AddStringToObject(runtime, "last_error_name", esp_err_to_name(data.last_error));
        cJSON_AddStringToObject(runtime, "state", health.state);
        meter_json_add_phase_power(runtime, &data, health.has_data);
        meter_json_add_measurements(item, &data, current_ms);
        meter_json_add_energy(item, &data, current_ms);
        cJSON_AddItemToArray(meters, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "enabled", enabled_count);
    cJSON_AddNumberToObject(summary, "online", online_count);
    cJSON_AddNumberToObject(summary, "stale_or_unavailable", stale_or_unavailable_count);
    cJSON_AddNumberToObject(summary, "initialization_failed", initialization_failed_count);
    cJSON_AddNumberToObject(summary, "with_data", data_count);

    free(config);
    return send_json(request, root);
}

static esp_err_t inverters_get(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    esp_err_t err = config_manager_get_snapshot(config);
    if (err != ESP_OK) {
        free(config);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Inverter configuration unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }

    uint32_t current_ms = now_ms();
    uint8_t enabled_count = 0;
    uint8_t command_tested_count = 0;
    uint8_t last_write_ok_count = 0;
    uint8_t initialization_failed_count = 0;
    float configured_rated_kw = 0.0f;
    float enabled_rated_kw = 0.0f;

    cJSON_AddNumberToObject(root, "generated_ms", current_ms);
    cJSON_AddNumberToObject(root, "configured_count", config->inverter_count);
    cJSON_AddBoolToObject(root, "measured_power_supported", false);
    cJSON *inverters = cJSON_AddArrayToObject(root, "inverters");

    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        const inverter_config_t *inverter = &config->inverters[index];
        inverter_data_t data = {0};
        bool runtime_available = index < inverter_manager_get_count() &&
                                 inverter_manager_get_data(index, &data);
        inverter_health_t health = inverter_health(inverter, &data, runtime_available);

        configured_rated_kw += inverter->rated_power_kw;
        if (inverter->enabled) {
            enabled_count++;
            enabled_rated_kw += inverter->rated_power_kw;
        }
        if (health.has_command) command_tested_count++;
        if (health.last_write_ok) last_write_ok_count++;
        if (health.initialization_failed) initialization_failed_count++;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", inverter->name);
        cJSON_AddBoolToObject(item, "enabled", inverter->enabled);
        cJSON_AddNumberToObject(item, "rated_kw", inverter->rated_power_kw);
        cJSON_AddBoolToObject(item, "telemetry_supported", false);
        cJSON_AddNullToObject(item, "measured_power_kw");
        add_endpoint(item, &inverter->endpoint);

        cJSON *command = cJSON_AddObjectToObject(item, "command");
        cJSON_AddNumberToObject(command, "limit_pdu_address", inverter->power_limit_address);
        cJSON_AddNumberToObject(command, "function", inverter->power_limit_function);
        cJSON_AddNumberToObject(command, "raw_units_per_percent", inverter->raw_units_per_percent);
        cJSON_AddNumberToObject(command, "minimum_percent", inverter->minimum_percent);
        cJSON_AddNumberToObject(command, "maximum_percent", inverter->maximum_percent);

        cJSON *runtime = cJSON_AddObjectToObject(item, "runtime");
        cJSON_AddBoolToObject(runtime, "available", health.runtime_available);
        cJSON_AddBoolToObject(runtime, "connection_initialized", health.connection_initialized);
        cJSON_AddBoolToObject(runtime, "initialization_failed", health.initialization_failed);
        cJSON_AddBoolToObject(runtime, "has_command", health.has_command);
        if (health.has_command) {
            cJSON_AddBoolToObject(runtime, "last_write_ok", health.last_write_ok);
            cJSON_AddNumberToObject(runtime, "commanded_percent", data.commanded_percent);
            cJSON_AddNumberToObject(runtime, "commanded_power_kw", data.commanded_power_kw);
        } else {
            cJSON_AddNullToObject(runtime, "last_write_ok");
            cJSON_AddNullToObject(runtime, "commanded_percent");
            cJSON_AddNullToObject(runtime, "commanded_power_kw");
        }
        add_age(runtime, "last_command_age_ms", health.has_command,
                current_ms, data.last_command_ms);
        cJSON_AddNumberToObject(runtime, "write_successes", data.write_successes);
        cJSON_AddNumberToObject(runtime, "write_errors", data.write_errors);
        cJSON_AddNumberToObject(runtime, "last_error", data.last_error);
        cJSON_AddStringToObject(runtime, "last_error_name", esp_err_to_name(data.last_error));
        cJSON_AddStringToObject(runtime, "state", health.state);
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

    free(config);
    return send_json(request, root);
}

static esp_err_t telemetry_get(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    esp_err_t err = config_manager_get_snapshot(config);
    if (err != ESP_OK) {
        free(config);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Operational configuration unavailable");
    }

    uint32_t current_ms = now_ms();
    network_status_t network = {0};
    control_status_t control = {0};
    network_manager_get_status(&network);
    control_engine_get_status(&control);

    uint8_t enabled_meters = 0;
    uint8_t online_meters = 0;
    uint8_t initialization_failed_meters = 0;
    bool primary_meter_fresh = false;
    bool primary_meter_has_data = false;
    uint32_t primary_meter_age_ms = 0;
    float primary_meter_value_kw = 0.0f;
    const char *primary_meter_state = config->meter_count > 0 ? "unavailable" : "not_configured";

    for (uint8_t index = 0; index < config->meter_count; ++index) {
        meter_data_t data = {0};
        bool runtime_available = index < meter_manager_get_count() &&
                                 meter_manager_get_data(index, &data);
        meter_health_t health = meter_health(config, index, &data, runtime_available, current_ms);
        if (config->meters[index].enabled) enabled_meters++;
        if (health.online) online_meters++;
        if (health.initialization_failed) initialization_failed_meters++;
        if (index == 0) {
            primary_meter_fresh = health.online;
            primary_meter_has_data = health.has_data;
            primary_meter_age_ms = health.data_age_ms;
            primary_meter_value_kw = data.active_power_kw;
            primary_meter_state = health.state;
        }
    }

    uint8_t enabled_inverters = 0;
    uint8_t command_tested_inverters = 0;
    uint8_t last_write_ok_inverters = 0;
    uint8_t initialization_failed_inverters = 0;
    float configured_rated_kw = 0.0f;
    float enabled_rated_kw = 0.0f;
    float total_commanded_kw = 0.0f;
    bool any_command = false;

    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        const inverter_config_t *inverter = &config->inverters[index];
        inverter_data_t data = {0};
        bool runtime_available = index < inverter_manager_get_count() &&
                                 inverter_manager_get_data(index, &data);
        inverter_health_t health = inverter_health(inverter, &data, runtime_available);

        configured_rated_kw += inverter->rated_power_kw;
        if (inverter->enabled) {
            enabled_inverters++;
            enabled_rated_kw += inverter->rated_power_kw;
        }
        if (health.has_command) {
            command_tested_inverters++;
            total_commanded_kw += data.commanded_power_kw;
            any_command = true;
        }
        if (health.last_write_ok) last_write_ok_inverters++;
        if (health.initialization_failed) initialization_failed_inverters++;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }

    cJSON_AddNumberToObject(root, "generated_ms", current_ms);

    cJSON *network_json = cJSON_AddObjectToObject(root, "network");
    cJSON_AddBoolToObject(network_json, "online", network.network_ready);
    cJSON_AddNumberToObject(network_json, "state", network.state);
    cJSON_AddStringToObject(network_json, "ssid", network.ssid);
    cJSON_AddStringToObject(network_json, "ip", network.ip);
    cJSON_AddNumberToObject(network_json, "rssi", network.rssi);
    cJSON_AddBoolToObject(network_json, "using_fallback_sta", network.using_fallback_sta);
    cJSON_AddBoolToObject(network_json, "recovery_ap_active", network.fallback_ap_active);

    cJSON *control_json = cJSON_AddObjectToObject(root, "control");
    cJSON_AddBoolToObject(control_json, "enabled", control.enabled);
    cJSON_AddNumberToObject(control_json, "mode", control.mode);
    add_age(control_json, "last_cycle_age_ms", control.last_cycle_ms != 0,
            current_ms, control.last_cycle_ms);
    cJSON_AddNumberToObject(control_json, "requested_pv_kw", control.requested_pv_kw);
    cJSON_AddNumberToObject(control_json, "applied_pv_kw", control.applied_pv_kw);
    cJSON_AddNumberToObject(control_json, "grid_target_kw", control.grid_target_kw);
    cJSON_AddNumberToObject(control_json, "error_kw", control.error_kw);
    cJSON_AddNumberToObject(control_json, "alarm_flags", safety_manager_get_alarm_flags());

    cJSON *grid = cJSON_AddObjectToObject(root, "grid_meter");
    cJSON_AddStringToObject(grid, "state", primary_meter_state);
    cJSON_AddBoolToObject(grid, "fresh", primary_meter_fresh);
    if (primary_meter_fresh) cJSON_AddNumberToObject(grid, "active_power_kw", primary_meter_value_kw);
    else cJSON_AddNullToObject(grid, "active_power_kw");
    if (primary_meter_has_data) {
        cJSON_AddNumberToObject(grid, "retained_active_power_kw", primary_meter_value_kw);
        cJSON_AddNumberToObject(grid, "data_age_ms", primary_meter_age_ms);
    } else {
        cJSON_AddNullToObject(grid, "retained_active_power_kw");
        cJSON_AddNullToObject(grid, "data_age_ms");
    }

    cJSON *meters = cJSON_AddObjectToObject(root, "meters");
    cJSON_AddNumberToObject(meters, "configured", config->meter_count);
    cJSON_AddNumberToObject(meters, "enabled", enabled_meters);
    cJSON_AddNumberToObject(meters, "online", online_meters);
    cJSON_AddNumberToObject(meters, "initialization_failed", initialization_failed_meters);

    cJSON *inverters = cJSON_AddObjectToObject(root, "inverters");
    cJSON_AddNumberToObject(inverters, "configured", config->inverter_count);
    cJSON_AddNumberToObject(inverters, "enabled", enabled_inverters);
    cJSON_AddNumberToObject(inverters, "configured_rated_kw", configured_rated_kw);
    cJSON_AddNumberToObject(inverters, "enabled_rated_kw", enabled_rated_kw);
    cJSON_AddNumberToObject(inverters, "commandable_rated_kw", inverter_manager_get_total_rated_kw());
    cJSON_AddNumberToObject(inverters, "command_tested", command_tested_inverters);
    cJSON_AddNumberToObject(inverters, "last_write_ok", last_write_ok_inverters);
    cJSON_AddNumberToObject(inverters, "initialization_failed", initialization_failed_inverters);
    if (any_command) cJSON_AddNumberToObject(inverters, "total_commanded_kw", total_commanded_kw);
    else cJSON_AddNullToObject(inverters, "total_commanded_kw");
    cJSON_AddBoolToObject(inverters, "measured_power_supported", false);
    cJSON_AddNullToObject(inverters, "measured_power_kw");

    cJSON *availability = cJSON_AddObjectToObject(root, "availability");
    cJSON_AddBoolToObject(availability, "monitoring_ready",
                          network.network_ready && primary_meter_fresh);
    cJSON_AddBoolToObject(availability, "command_path_ready",
                          inverter_manager_get_total_rated_kw() > 0.0f);
    cJSON_AddBoolToObject(availability, "automatic_control_active", control.enabled);
    cJSON_AddBoolToObject(availability, "generator_telemetry_supported", false);
    cJSON_AddNullToObject(availability, "generator_power_kw");
    cJSON_AddBoolToObject(availability, "facility_load_supported", false);
    cJSON_AddNullToObject(availability, "facility_load_kw");

    free(config);
    return send_json(request, root);
}

esp_err_t device_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/meters", .method = HTTP_GET, .handler = meters_get},
        {.uri = "/api/inverters", .method = HTTP_GET, .handler = inverters_get},
        {.uri = "/api/telemetry", .method = HTTP_GET, .handler = telemetry_get}
    };

    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            "device_api", "handler registration failed");
    }
    return ESP_OK;
}
