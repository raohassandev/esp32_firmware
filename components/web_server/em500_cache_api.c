#include "em500_cache_api.h"

#include <stdlib.h>

#include "cJSON.h"
#include "em500_cache.h"
#include "esp_check.h"
#include "meter_manager.h"

static void add_group(cJSON *parent, const char *name,
                      const em500_cache_group_status_t *status)
{
    cJSON *group = cJSON_AddObjectToObject(parent, name);
    cJSON_AddBoolToObject(group, "has_data", status->has_data);
    cJSON_AddBoolToObject(group, "stale", status->stale);
    if (status->has_data) {
        cJSON_AddNumberToObject(group, "updated_ms", status->updated_ms);
        cJSON_AddNumberToObject(group, "age_ms", status->age_ms);
    } else {
        cJSON_AddNullToObject(group, "updated_ms");
        cJSON_AddNullToObject(group, "age_ms");
    }
    cJSON_AddNumberToObject(group, "response_ms", status->response_ms);
    cJSON_AddNumberToObject(group, "last_error", status->last_error);
    cJSON_AddStringToObject(group, "last_error_name",
                            esp_err_to_name(status->last_error));
    cJSON_AddNumberToObject(group, "success_count", status->success_count);
    cJSON_AddNumberToObject(group, "error_count", status->error_count);
    uint32_t attempts = status->success_count + status->error_count;
    if (attempts > 0U) {
        cJSON_AddNumberToObject(group, "success_percent",
                                (status->success_count * 100U) / attempts);
    } else {
        cJSON_AddNullToObject(group, "success_percent");
    }
}

static esp_err_t cache_status_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "modbus_io_in_http_handler", false);
    cJSON_AddStringToObject(root, "acquisition_owner", "em500_cache_task");
    cJSON_AddNumberToObject(root, "meter_count", meter_manager_get_count());
    cJSON *meters = cJSON_AddArrayToObject(root, "meters");

    for (uint8_t index = 0; index < meter_manager_get_count(); ++index) {
        em500_cache_status_t status = {0};
        if (!em500_cache_get_status(index, &status)) continue;
        cJSON *meter = cJSON_CreateObject();
        cJSON_AddNumberToObject(meter, "index", index);
        cJSON_AddBoolToObject(meter, "configured", status.configured);
        cJSON_AddBoolToObject(meter, "scan_in_progress", status.scan_in_progress);
        cJSON_AddNumberToObject(meter, "function", status.function_code);
        cJSON_AddNumberToObject(meter, "address_base", status.address_base);
        cJSON_AddNumberToObject(meter, "requested_scopes", status.requested_scopes);
        cJSON_AddNumberToObject(meter, "generation", status.generation);
        cJSON_AddNumberToObject(meter, "requested_ms", status.requested_ms);
        cJSON *groups = cJSON_AddObjectToObject(meter, "groups");
        add_group(groups, "instantaneous", &status.instantaneous);
        add_group(groups, "source_input", &status.source_input);
        add_group(groups, "energy", &status.energy);
        add_group(groups, "setup", &status.setup);
        cJSON_AddItemToArray(meters, meter);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    esp_err_t error = httpd_resp_sendstr(request, json);
    free(json);
    return error;
}

esp_err_t em500_cache_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/meters/em500/cache",
        .method = HTTP_GET,
        .handler = cache_status_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &endpoint),
                        "em500_cache_api", "handler registration failed");
    return ESP_OK;
}
