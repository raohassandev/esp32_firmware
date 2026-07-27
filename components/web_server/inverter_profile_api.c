#include "inverter_profile_api.h"

#include <stdlib.h>
#include "cJSON.h"
#include "esp_check.h"
#include "inverter_profiles.h"

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

esp_err_t inverter_profile_api_register(httpd_handle_t server)
{
    const httpd_uri_t handler = {
        .uri = "/api/inverter-profiles",
        .method = HTTP_GET,
        .handler = profiles_get
    };
    return httpd_register_uri_handler(server, &handler);
}
