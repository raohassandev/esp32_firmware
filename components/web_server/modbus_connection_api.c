#include "modbus_connection_api.h"

#include <stdlib.h>

#include "cJSON.h"
#include "config_manager.h"
#include "modbus_tcp.h"

static void add_endpoint_mode(cJSON *item, const modbus_endpoint_t *endpoint)
{
    cJSON_AddStringToObject(item, "connection_mode",
                            modbus_tcp_connection_mode_name(endpoint->connection_mode));
    cJSON_AddNumberToObject(item, "connection_mode_code", endpoint->connection_mode);
    cJSON_AddBoolToObject(item, "keeps_healthy_socket",
                          endpoint->connection_mode != MODBUS_CONNECTION_PER_TRANSACTION);
    cJSON_AddBoolToObject(item, "closes_on_any_error",
                          endpoint->connection_mode == MODBUS_CONNECTION_RECONNECT_ON_ERROR);
    cJSON_AddBoolToObject(item, "same_call_retry", false);
}

static esp_err_t connections_get(httpd_req_t *request)
{
    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    esp_err_t error = config_manager_get_snapshot(config);
    if (error != ESP_OK) {
        free(config);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Modbus configuration unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }
    cJSON_AddNumberToObject(root, "schema", config->version);
    cJSON_AddStringToObject(root, "retry_policy",
                            "never replay a transaction in the same call");

    cJSON *meters = cJSON_AddArrayToObject(root, "meters");
    for (uint8_t index = 0; index < config->meter_count; ++index) {
        const meter_config_t *meter = &config->meters[index];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", meter->name);
        cJSON_AddBoolToObject(item, "enabled", meter->enabled);
        add_endpoint_mode(item, &meter->endpoint);
        cJSON_AddItemToArray(meters, item);
    }

    cJSON *inverters = cJSON_AddArrayToObject(root, "inverters");
    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        const inverter_config_t *inverter = &config->inverters[index];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", index);
        cJSON_AddStringToObject(item, "name", inverter->name);
        cJSON_AddBoolToObject(item, "enabled", inverter->enabled);
        add_endpoint_mode(item, &inverter->endpoint);
        cJSON_AddItemToArray(inverters, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(config);
    if (!json) return httpd_resp_send_500(request);

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    error = httpd_resp_sendstr(request, json);
    free(json);
    return error;
}

esp_err_t modbus_connection_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/modbus/connections",
        .method = HTTP_GET,
        .handler = connections_get,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
