#include "web_api.h"
#include "esp_check.h"
#include "esp_system.h"
#include <stdlib.h>
#include "cJSON.h"
#include "config_manager.h"
#include "control_engine.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_manager.h"
#include "network_manager.h"
#include "safety_manager.h"

static esp_err_t status_get(httpd_req_t *request)
{
    meter_data_t meter = {0};
    control_status_t control = {0};
    network_status_t network = {0};
    meter_manager_get_data(0, &meter);
    control_engine_get_status(&control);
    network_manager_get_status(&network);

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(root, "network_online", network.network_ready);
    cJSON_AddNumberToObject(root, "wifi_state", network.state);
    cJSON_AddStringToObject(root, "ssid", network.ssid);
    cJSON_AddStringToObject(root, "ip", network.ip);
    cJSON_AddStringToObject(root, "gateway", network.gateway);
    cJSON_AddStringToObject(root, "netmask", network.netmask);
    cJSON_AddNumberToObject(root, "rssi", network.rssi);
    cJSON_AddBoolToObject(root, "using_fallback_sta", network.using_fallback_sta);
    cJSON_AddBoolToObject(root, "fallback_ap_active", network.fallback_ap_active);
    cJSON_AddNumberToObject(root, "disconnect_count", network.disconnect_count);
    cJSON_AddNumberToObject(root, "reconnect_count", network.reconnect_count);

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
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static esp_err_t config_get(httpd_req_t *request)
{
    char *json = NULL;
    ESP_RETURN_ON_ERROR(config_manager_export_json(&json), "web_api", "config export failed");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
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
        offset += (size_t)received;
    }
    body[offset] = '\0';

    esp_err_t err = config_manager_import_json(body);
    free(body);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Configuration validation or persistence failed");
    }

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"saved\":true,\"persisted\":true,\"restart_required\":true}");
}

static esp_err_t wifi_rescan_post(httpd_req_t *request)
{
    esp_err_t err = network_manager_rescan_and_connect();
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi rescan failed");
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"accepted\":true}");
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
}

static esp_err_t restart_post(httpd_req_t *request)
{
    if (xTaskCreate(restart_task, "api_restart", 2048, NULL, 5, NULL) != pdPASS) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Restart scheduling failed");
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"restarting\":true}");
}

esp_err_t web_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/config", .method = HTTP_GET, .handler = config_get},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_post},
        {.uri = "/api/wifi/rescan", .method = HTTP_POST, .handler = wifi_rescan_post},
        {.uri = "/api/system/restart", .method = HTTP_POST, .handler = restart_post}
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]), "web_api", "handler registration failed");
    }
    return ESP_OK;
}
