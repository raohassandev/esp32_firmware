#include "web_api.h"
#include "esp_check.h"
#include <stdlib.h>
#include "cJSON.h"
#include "config_manager.h"
#include "control_engine.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"

static esp_err_t status_get(httpd_req_t *request)
{
    meter_data_t meter = {0};
    control_status_t control = {0};
    meter_manager_get_data(0, &meter);
    control_engine_get_status(&control);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "network_online", network_manager_is_connected());
    cJSON_AddStringToObject(root, "ip", network_manager_get_ip());
    cJSON_AddBoolToObject(root, "meter_online", meter.online);
    cJSON_AddNumberToObject(root, "grid_power_kw", meter.active_power_kw);
    cJSON_AddNumberToObject(root, "mode", control.mode);
    cJSON_AddNumberToObject(root, "requested_pv_kw", control.requested_pv_kw);
    cJSON_AddNumberToObject(root, "applied_pv_kw", control.applied_pv_kw);
    cJSON_AddNumberToObject(root, "alarms", safety_manager_get_alarm_flags());
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static esp_err_t config_get(httpd_req_t *request)
{
    char *json = NULL;
    ESP_RETURN_ON_ERROR(config_manager_export_json(&json), "web_api", "config export failed");
    httpd_resp_set_type(request, "application/json");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static esp_err_t config_post(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 16384) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid configuration size");
    }
    char *body = malloc(request->content_len + 1);
    if (!body) return httpd_resp_send_500(request);
    size_t offset = 0;
    while (offset < request->content_len) {
        int received = httpd_req_recv(request, body + offset, request->content_len - offset);
        if (received <= 0) {
            free(body);
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
        }
        offset += received;
    }
    body[offset] = '\0';
    esp_err_t err = config_manager_import_json(body);
    free(body);
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Configuration validation failed");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"saved\":true,\"restart_required\":true}");
}

esp_err_t web_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_post}
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), "web_api", "handler registration failed");
    }
    return ESP_OK;
}
