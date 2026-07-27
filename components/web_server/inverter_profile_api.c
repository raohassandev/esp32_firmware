#include "inverter_profile_api.h"

#include <stdlib.h>
#include "cJSON.h"
#include "esp_check.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"

#define MAX_ASSIGNMENT_BODY 256

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

static esp_err_t profiles_get(httpd_req_t *request)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);

    cJSON_AddNumberToObject(root, "count", inverter_profiles_count());
    cJSON_AddBoolToObject(root, "writes_require_production_approval", true);
    cJSON *profiles = cJSON_AddArrayToObject(root, "profiles");

    for (size_t index = 0; index < inverter_profiles_count(); ++index) {
        const inverter_profile_t *profile = inverter_profiles_get(index);
        if (!profile) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", profile->id);
        cJSON_AddStringToObject(item, "manufacturer", profile->manufacturer);
        cJSON_AddStringToObject(item, "model_family", profile->model_family);
        cJSON_AddStringToObject(item, "protocol", profile->protocol);
        cJSON_AddStringToObject(item, "connection", inverter_profile_connection_label(profile->connection));
        cJSON_AddStringToObject(item, "qualification", inverter_profile_qualification_label(profile->qualification));
        cJSON_AddStringToObject(item, "manual_reference", profile->manual_reference ? profile->manual_reference : "");
        cJSON_AddBoolToObject(item, "read_allowed", inverter_profile_allows_read(profile));
        cJSON_AddBoolToObject(item, "write_allowed", inverter_profile_allows_write(profile));
        cJSON_AddBoolToObject(item, "identity_probe_supported", profile->has_identity_probe);
        cJSON_AddBoolToObject(item, "active_power_supported", profile->has_active_power);
        cJSON_AddBoolToObject(item, "power_limit_supported", profile->has_power_limit);
        cJSON_AddBoolToObject(item, "power_limit_readback_supported", profile->has_power_limit_readback);

        cJSON *limits = cJSON_AddObjectToObject(item, "limits");
        cJSON_AddNumberToObject(limits, "minimum_percent", profile->minimum_percent);
        cJSON_AddNumberToObject(limits, "maximum_percent", profile->maximum_percent);

        cJSON_AddItemToArray(profiles, item);
    }

    return send_json(request, root);
}

static esp_err_t receive_body(httpd_req_t *request, char *buffer, size_t size)
{
    if (request->content_len <= 0 || request->content_len >= size) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int result = httpd_req_recv(request, buffer + received,
                                    request->content_len - received);
        if (result <= 0) return ESP_FAIL;
        received += (size_t)result;
    }
    buffer[received] = '\0';
    return ESP_OK;
}

static esp_err_t profile_assignment_post(httpd_req_t *request)
{
    char body[MAX_ASSIGNMENT_BODY];
    if (receive_body(request, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid profile assignment body");
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Profile assignment must be valid JSON");
    }

    cJSON *index_item = cJSON_GetObjectItemCaseSensitive(json, "inverter_index");
    cJSON *profile_item = cJSON_GetObjectItemCaseSensitive(json, "profile_id");
    if (!cJSON_IsNumber(index_item) || !cJSON_IsString(profile_item) ||
        index_item->valueint < 0 || index_item->valueint >= APP_MAX_INVERTERS) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "inverter_index and profile_id are required");
    }

    const uint8_t inverter_index = (uint8_t)index_item->valueint;
    const inverter_profile_t *profile = inverter_profiles_find(profile_item->valuestring);
    if (!profile) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Unknown inverter profile");
    }

    esp_err_t err = inverter_profile_store_set(inverter_index, profile->id);
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to save inverter profile assignment");
    }

    cJSON *response = cJSON_CreateObject();
    if (!response) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(response, "saved", true);
    cJSON_AddNumberToObject(response, "inverter_index", inverter_index);
    cJSON_AddStringToObject(response, "profile_id", profile->id);
    cJSON_AddBoolToObject(response, "automatic_control_disabled", true);
    cJSON_AddBoolToObject(response, "restart_required", true);
    cJSON_AddBoolToObject(response, "write_allowed_after_restart",
                          inverter_profile_allows_write(profile));
    return send_json(request, response);
}

esp_err_t inverter_profile_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {
            .uri = "/api/inverter-profiles",
            .method = HTTP_GET,
            .handler = profiles_get
        },
        {
            .uri = "/api/inverter-profile-assignment",
            .method = HTTP_POST,
            .handler = profile_assignment_post
        }
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[i]),
                            "inverter_profile_api", "handler registration failed");
    }
    return ESP_OK;
}
