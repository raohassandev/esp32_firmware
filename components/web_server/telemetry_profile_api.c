#include "telemetry_profile_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "config_manager.h"
#include "esp_check.h"
#include "profile_manager.h"

#define TELEMETRY_PROFILE_MAX_BODY 8192

_Static_assert(APP_MAX_INVERTERS == PROFILE_MAX_INVERTERS,
               "configuration and telemetry profile capacities must match");

static esp_err_t send_json_text(httpd_req_t *request,
                                const char *status,
                                const char *json)
{
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t send_json_error(httpd_req_t *request,
                                 const char *status,
                                 const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddBoolToObject(root, "saved", false);
    cJSON_AddStringToObject(root, "error", message ? message : "Unknown error");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    esp_err_t err = send_json_text(request, status, json);
    free(json);
    return err;
}

static esp_err_t read_body(httpd_req_t *request, char **out_body)
{
    if (!out_body || request->content_len <= 0 ||
        request->content_len > TELEMETRY_PROFILE_MAX_BODY) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *body = malloc(request->content_len + 1);
    if (!body) return ESP_ERR_NO_MEM;
    size_t offset = 0;
    while (offset < request->content_len) {
        int received = httpd_req_recv(request, body + offset,
                                      request->content_len - offset);
        if (received <= 0) {
            free(body);
            return ESP_FAIL;
        }
        offset += (size_t)received;
    }
    body[offset] = '\0';
    *out_body = body;
    return ESP_OK;
}

static void add_profile_json(cJSON *profiles,
                             uint8_t index,
                             const inverter_config_t *inverter,
                             const inverter_telemetry_profile_t *profile)
{
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "index", index);
    cJSON_AddStringToObject(item, "name", inverter->name);
    cJSON_AddBoolToObject(item, "device_enabled", inverter->enabled);
    cJSON_AddNumberToObject(item, "rated_kw", inverter->rated_power_kw);
    cJSON_AddBoolToObject(item, "enabled", profile->enabled);

    cJSON *active_power = cJSON_AddObjectToObject(item, "active_power");
    cJSON_AddNumberToObject(active_power, "function",
                            profile->active_power.function_code);
    cJSON_AddNumberToObject(active_power, "pdu_address",
                            profile->active_power.address);
    cJSON_AddNumberToObject(active_power, "data_type",
                            profile->active_power.data_type);
    cJSON_AddNumberToObject(active_power, "word_order",
                            profile->active_power.word_order);
    cJSON_AddNumberToObject(active_power, "scale",
                            profile->active_power.scale);
    cJSON_AddNumberToObject(active_power, "offset",
                            profile->active_power.offset);
    cJSON_AddNumberToObject(active_power, "poll_ms",
                            profile->active_power.poll_interval_ms);
    cJSON_AddItemToArray(profiles, item);
}

static esp_err_t profiles_get(httpd_req_t *request)
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
        return send_json_error(request, "500 Internal Server Error",
                               "Inverter telemetry profiles are unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        free(profile_set);
        return httpd_resp_send_500(request);
    }
    cJSON_AddNumberToObject(root, "schema", profile_set->version);
    cJSON_AddBoolToObject(root, "control_enabled", config->control.enabled);
    cJSON_AddNumberToObject(root, "configured_count", config->inverter_count);
    cJSON_AddBoolToObject(root, "restart_required_after_save", true);
    cJSON *profiles = cJSON_AddArrayToObject(root, "profiles");
    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        add_profile_json(profiles, index, &config->inverters[index],
                         &profile_set->inverters[index]);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(config);
    free(profile_set);
    if (!json) return httpd_resp_send_500(request);
    err = send_json_text(request, NULL, json);
    free(json);
    return err;
}

static bool required_bool(cJSON *object, const char *key, bool *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(item)) return false;
    *value = cJSON_IsTrue(item);
    return true;
}

static bool required_integer(cJSON *object, const char *key,
                             int minimum, int maximum, int *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum || item->valuedouble > maximum) {
        return false;
    }
    *value = item->valueint;
    return true;
}

static bool required_float(cJSON *object, const char *key,
                           bool nonzero, float *value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        (nonzero && item->valuedouble == 0.0)) {
        return false;
    }
    *value = (float)item->valuedouble;
    return isfinite(*value) && (!nonzero || *value != 0.0f);
}

static bool parse_profile_item(cJSON *item,
                               const app_config_t *config,
                               inverter_telemetry_profile_set_t *profile_set,
                               bool *seen,
                               char *error,
                               size_t error_size)
{
    if (!cJSON_IsObject(item)) {
        strlcpy(error, "Every telemetry profile must be an object", error_size);
        return false;
    }

    int index = 0;
    if (!required_integer(item, "index", 0, PROFILE_MAX_INVERTERS - 1,
                          &index)) {
        strlcpy(error, "Telemetry profile index is invalid", error_size);
        return false;
    }
    if (seen[index]) {
        strlcpy(error, "Telemetry profile indexes must be unique", error_size);
        return false;
    }
    seen[index] = true;

    bool enabled = false;
    if (!required_bool(item, "enabled", &enabled)) {
        strlcpy(error, "Telemetry profile enabled state is required", error_size);
        return false;
    }
    if (enabled &&
        (index >= config->inverter_count || !config->inverters[index].enabled)) {
        strlcpy(error, "Telemetry can only be enabled for an enabled configured inverter",
                error_size);
        return false;
    }

    cJSON *active_power = cJSON_GetObjectItemCaseSensitive(item, "active_power");
    if (!cJSON_IsObject(active_power)) {
        strlcpy(error, "Active-power telemetry mapping is required", error_size);
        return false;
    }

    int function_code = 0;
    int address = 0;
    int data_type = 0;
    int word_order = 0;
    int poll_ms = 0;
    float scale = 0.0f;
    float offset = 0.0f;
    if (!required_integer(active_power, "function", 3, 4, &function_code) ||
        (function_code != 3 && function_code != 4) ||
        !required_integer(active_power, "pdu_address", 0, 65535, &address) ||
        !required_integer(active_power, "data_type", MODBUS_DATA_UINT16,
                          MODBUS_DATA_FLOAT32, &data_type) ||
        !required_integer(active_power, "word_order", MODBUS_ORDER_ABCD,
                          MODBUS_ORDER_DCBA, &word_order) ||
        !required_float(active_power, "scale", true, &scale) ||
        !required_float(active_power, "offset", false, &offset) ||
        !required_integer(active_power, "poll_ms", 100, 60000, &poll_ms)) {
        strlcpy(error, "Active-power telemetry mapping is invalid", error_size);
        return false;
    }

    inverter_telemetry_profile_t next = {0};
    next.enabled = enabled;
    strlcpy(next.active_power.key, "active_power",
            sizeof(next.active_power.key));
    next.active_power.function_code = (uint8_t)function_code;
    next.active_power.address = (uint16_t)address;
    next.active_power.data_type = (modbus_data_type_t)data_type;
    next.active_power.word_order = (modbus_word_order_t)word_order;
    next.active_power.scale = scale;
    next.active_power.offset = offset;
    next.active_power.poll_interval_ms = (uint32_t)poll_ms;
    next.active_power.writable = false;

    if (profile_manager_validate_inverter_telemetry_profile(&next) != ESP_OK) {
        strlcpy(error, "Active-power telemetry mapping failed validation",
                error_size);
        return false;
    }
    profile_set->inverters[index] = next;
    return true;
}

static esp_err_t profiles_post(httpd_req_t *request)
{
    char *body = NULL;
    esp_err_t err = read_body(request, &body);
    if (err != ESP_OK) {
        return send_json_error(request, "400 Bad Request",
                               "Invalid telemetry profile body");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return send_json_error(request, "400 Bad Request",
                               "Invalid telemetry profile JSON");
    }

    app_config_t *config = malloc(sizeof(*config));
    inverter_telemetry_profile_set_t *profile_set = malloc(sizeof(*profile_set));
    if (!config || !profile_set) {
        free(config);
        free(profile_set);
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }

    err = config_manager_get_snapshot(config);
    if (err == ESP_OK) {
        err = profile_manager_get_inverter_telemetry_set(profile_set);
    }
    if (err != ESP_OK) {
        free(config);
        free(profile_set);
        cJSON_Delete(root);
        return send_json_error(request, "500 Internal Server Error",
                               "Current telemetry profiles are unavailable");
    }
    if (config->control.enabled) {
        free(config);
        free(profile_set);
        cJSON_Delete(root);
        return send_json_error(request, "409 Conflict",
                               "Disable automatic control before changing telemetry profiles");
    }

    cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    if (!cJSON_IsArray(profiles) || cJSON_GetArraySize(profiles) < 1 ||
        cJSON_GetArraySize(profiles) > PROFILE_MAX_INVERTERS) {
        free(config);
        free(profile_set);
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request",
                               "A non-empty telemetry profile array is required");
    }

    bool seen[PROFILE_MAX_INVERTERS] = {0};
    char validation_error[160] = {0};
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, profiles) {
        if (!parse_profile_item(item, config, profile_set, seen,
                                validation_error,
                                sizeof(validation_error))) {
            free(config);
            free(profile_set);
            cJSON_Delete(root);
            return send_json_error(request, "400 Bad Request",
                                   validation_error);
        }
    }
    cJSON_Delete(root);
    free(config);

    err = profile_manager_save_inverter_telemetry_set(profile_set);
    free(profile_set);
    if (err != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error",
                               "Telemetry profiles could not be persisted");
    }
    return send_json_text(
        request, NULL,
        "{\"saved\":true,\"persisted\":true,\"restart_required\":true}");
}

esp_err_t telemetry_profile_api_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/inverter-telemetry-profiles", .method = HTTP_GET,
         .handler = profiles_get},
        {.uri = "/api/inverter-telemetry-profiles", .method = HTTP_POST,
         .handler = profiles_post}
    };

    for (size_t index = 0; index < sizeof(handlers) / sizeof(handlers[0]); ++index) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &handlers[index]),
                            "telemetry_api", "handler registration failed");
    }
    return ESP_OK;
}
