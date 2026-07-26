#include "operational_api.h"

#include <stdlib.h>
#include "cJSON.h"
#include "config_manager.h"
#include "control_engine.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"

static const char *meter_quality(bool enabled, bool has_data, bool online, bool stale)
{
    if (!enabled) return "disabled";
    if (!has_data) return "unavailable";
    if (!online || stale) return "stale";
    return "fresh";
}

static esp_err_t telemetry_get(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    if (config_manager_get_snapshot(config) != ESP_OK) {
        free(config);
        return httpd_resp_send_500(request);
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    network_status_t network = {0};
    control_status_t control = {0};
    network_manager_get_status(&network);
    control_engine_get_status(&control);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }

    cJSON_AddNumberToObject(root, "uptime_ms", now_ms);
    cJSON_AddBoolToObject(root, "network_online", network.network_ready);
    cJSON_AddBoolToObject(root, "control_enabled", control.enabled);
    cJSON_AddNumberToObject(root, "control_mode", control.mode);
    cJSON_AddNumberToObject(root, "control_cycle_age_ms",
                            control.last_cycle_ms ? (double)(now_ms - control.last_cycle_ms) : -1);
    cJSON_AddNumberToObject(root, "requested_pv_kw", control.requested_pv_kw);
    cJSON_AddNumberToObject(root, "applied_pv_kw", control.applied_pv_kw);
    cJSON_AddNumberToObject(root, "grid_target_kw", control.grid_target_kw);
    cJSON_AddNumberToObject(root, "control_error_kw", control.error_kw);
    cJSON_AddNumberToObject(root, "alarm_flags", safety_manager_get_alarm_flags());

    cJSON *meters = cJSON_AddArrayToObject(root, "meters");
    uint8_t fresh_meters = 0;
    uint8_t stale_meters = 0;
    for (uint8_t index = 0; index < config->meter_count; ++index) {
        const meter_config_t *meter_config = &config->meters[index];
        meter_data_t data = {0};
        bool runtime_available = meter_config->enabled && meter_manager_get_data(index, &data);
        bool has_data = runtime_available && data.last_update_ms != 0;
        uint32_t age_ms = has_data ? now_ms - data.last_update_ms : 0;
        uint32_t stale_after_ms = config->control.meter_stale_timeout_ms;
        if (stale_after_ms < meter_config->poll_interval_ms * 2U) {
            stale_after_ms = meter_config->poll_interval_ms * 2U;
        }
        bool stale = !has_data || !data.online || age_ms > stale_after_ms;
        const char *quality = meter_quality(meter_config->enabled, has_data, data.online, stale);
        if (strcmp(quality, "fresh") == 0) fresh_meters++;
        else if (strcmp(quality, "stale") == 0) stale_meters++;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", meter_config->name);
        cJSON_AddBoolToObject(item, "enabled", meter_config->enabled);
        cJSON_AddBoolToObject(item, "online", runtime_available && data.online);
        cJSON_AddBoolToObject(item, "has_data", has_data);
        cJSON_AddBoolToObject(item, "stale", stale);
        cJSON_AddStringToObject(item, "quality", quality);
        cJSON_AddNumberToObject(item, "age_ms", has_data ? (double)age_ms : -1);
        cJSON_AddNumberToObject(item, "active_power_kw", has_data ? data.active_power_kw : 0.0);
        cJSON_AddNumberToObject(item, "response_errors", runtime_available ? data.response_errors : 0);
        cJSON_AddStringToObject(item, "host", meter_config->endpoint.host);
        cJSON_AddNumberToObject(item, "port", meter_config->endpoint.port);
        cJSON_AddNumberToObject(item, "unit_id", meter_config->endpoint.unit_id);
        cJSON_AddNumberToObject(item, "pdu_address", meter_config->active_power_address);
        cJSON_AddNumberToObject(item, "poll_ms", meter_config->poll_interval_ms);
        cJSON_AddItemToArray(meters, item);
    }

    cJSON *inverters = cJSON_AddArrayToObject(root, "inverters");
    uint8_t enabled_inverters = 0;
    float total_rated_kw = 0.0f;
    float total_commanded_kw = 0.0f;
    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        const inverter_config_t *inverter_config = &config->inverters[index];
        inverter_data_t data = {0};
        bool runtime_available = inverter_manager_get_data(index, &data);
        if (inverter_config->enabled) {
            enabled_inverters++;
            total_rated_kw += inverter_config->rated_power_kw;
        }
        if (runtime_available) total_commanded_kw += data.commanded_power_kw;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", inverter_config->name);
        cJSON_AddBoolToObject(item, "enabled", inverter_config->enabled);
        cJSON_AddBoolToObject(item, "command_online", runtime_available && data.online);
        cJSON_AddNumberToObject(item, "rated_kw", inverter_config->rated_power_kw);
        cJSON_AddNumberToObject(item, "commanded_percent", runtime_available ? data.commanded_percent : 0.0);
        cJSON_AddNumberToObject(item, "commanded_power_kw", runtime_available ? data.commanded_power_kw : 0.0);
        cJSON_AddNumberToObject(item, "write_errors", runtime_available ? data.write_errors : 0);
        cJSON_AddStringToObject(item, "host", inverter_config->endpoint.host);
        cJSON_AddNumberToObject(item, "port", inverter_config->endpoint.port);
        cJSON_AddNumberToObject(item, "unit_id", inverter_config->endpoint.unit_id);
        cJSON_AddNumberToObject(item, "limit_pdu_address", inverter_config->power_limit_address);
        cJSON_AddBoolToObject(item, "measured_power_available", false);
        cJSON_AddItemToArray(inverters, item);
    }

    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "meter_count", config->meter_count);
    cJSON_AddNumberToObject(summary, "fresh_meter_count", fresh_meters);
    cJSON_AddNumberToObject(summary, "stale_meter_count", stale_meters);
    cJSON_AddNumberToObject(summary, "inverter_count", config->inverter_count);
    cJSON_AddNumberToObject(summary, "enabled_inverter_count", enabled_inverters);
    cJSON_AddNumberToObject(summary, "total_rated_kw", total_rated_kw);
    cJSON_AddNumberToObject(summary, "total_commanded_kw", total_commanded_kw);
    cJSON_AddBoolToObject(summary, "measured_pv_available", false);
    cJSON_AddBoolToObject(summary, "generator_telemetry_available", false);
    cJSON_AddBoolToObject(summary, "facility_load_available", false);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(config);
    if (!json) return httpd_resp_send_500(request);

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

esp_err_t operational_api_register(httpd_handle_t server)
{
    const httpd_uri_t telemetry = {
        .uri = "/api/telemetry",
        .method = HTTP_GET,
        .handler = telemetry_get
    };
    return httpd_register_uri_handler(server, &telemetry);
}
