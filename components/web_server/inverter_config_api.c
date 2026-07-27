#include "inverter_config_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"

#define INVERTER_CONFIG_MAX_BODY 12288
#define INVERTER_TIMEOUT_MIN_MS 100U
#define INVERTER_TIMEOUT_MAX_MS 60000U
#define INVERTER_RATED_MAX_KW 100000.0

static esp_err_t send_json_text(httpd_req_t *request, const char *status,
                                const char *text)
{
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(request, text);
}

static esp_err_t send_json_error(httpd_req_t *request, const char *status,
                                 const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", message ? message : "Request failed");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(request);
    esp_err_t err = send_json_text(request, status, json);
    free(json);
    return err;
}

static esp_err_t read_request_body(httpd_req_t *request, char **out_body)
{
    if (!request || !out_body || request->content_len <= 0 ||
        request->content_len > INVERTER_CONFIG_MAX_BODY) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *body = malloc((size_t)request->content_len + 1U);
    if (!body) return ESP_ERR_NO_MEM;

    size_t offset = 0;
    while (offset < (size_t)request->content_len) {
        int received = httpd_req_recv(request, body + offset,
                                      (size_t)request->content_len - offset);
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

static bool read_bool(cJSON *object, const char *key, bool *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsBool(item)) return false;
    *out = cJSON_IsTrue(item);
    return true;
}

static bool read_string(cJSON *object, const char *key, char *out, size_t out_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || !item->valuestring ||
        strlen(item->valuestring) >= out_size) return false;
    strlcpy(out, item->valuestring, out_size);
    return true;
}

static bool read_integer(cJSON *object, const char *key, uint32_t minimum,
                         uint32_t maximum, uint32_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < (double)minimum || item->valuedouble > (double)maximum) {
        return false;
    }
    *out = (uint32_t)item->valuedouble;
    return true;
}

static bool read_rated_kw(cJSON *object, float *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "rated_kw");
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < 0.0 || item->valuedouble > INVERTER_RATED_MAX_KW) {
        return false;
    }
    *out = (float)item->valuedouble;
    return true;
}

static void default_inverter(inverter_config_t *inverter, unsigned index)
{
    memset(inverter, 0, sizeof(*inverter));
    snprintf(inverter->name, sizeof(inverter->name), "Inverter %u", index + 1U);
    inverter->endpoint.port = 502;
    inverter->endpoint.unit_id = 1;
    inverter->endpoint.timeout_ms = 1000;
    inverter->rated_power_kw = 0.0f;
    inverter->minimum_percent = 0.0f;
    inverter->maximum_percent = 100.0f;
}

static bool parse_inverter(cJSON *object, inverter_config_t *next,
                           const inverter_config_t *current, unsigned index,
                           char *error, size_t error_size)
{
    if (!cJSON_IsObject(object)) {
        snprintf(error, error_size, "Inverter %u must be a JSON object", index + 1U);
        return false;
    }

    if (current) *next = *current;
    else default_inverter(next, index);

    uint32_t value = 0;
    if (!read_bool(object, "enabled", &next->enabled)) {
        snprintf(error, error_size, "Inverter %u field 'enabled' must be boolean", index + 1U);
        return false;
    }
    if (!read_string(object, "name", next->name, sizeof(next->name)) || !next->name[0]) {
        snprintf(error, error_size, "Inverter %u requires a valid name", index + 1U);
        return false;
    }
    if (!read_string(object, "host", next->endpoint.host, sizeof(next->endpoint.host))) {
        snprintf(error, error_size, "Inverter %u has an invalid host", index + 1U);
        return false;
    }
    if (!read_integer(object, "port", 1, 65535, &value)) {
        snprintf(error, error_size, "Inverter %u has an invalid port", index + 1U);
        return false;
    }
    next->endpoint.port = (uint16_t)value;
    if (!read_integer(object, "unit_id", 1, 247, &value)) {
        snprintf(error, error_size, "Inverter %u has an invalid unit ID", index + 1U);
        return false;
    }
    next->endpoint.unit_id = (uint8_t)value;
    if (!read_integer(object, "timeout_ms", INVERTER_TIMEOUT_MIN_MS,
                      INVERTER_TIMEOUT_MAX_MS, &value)) {
        snprintf(error, error_size, "Inverter %u has an invalid timeout", index + 1U);
        return false;
    }
    next->endpoint.timeout_ms = value;
    if (!read_rated_kw(object, &next->rated_power_kw)) {
        snprintf(error, error_size, "Inverter %u has an invalid rated power", index + 1U);
        return false;
    }
    if (next->enabled && (!next->endpoint.host[0] || next->rated_power_kw <= 0.0f)) {
        snprintf(error, error_size,
                 "Enabled inverter %u requires a host and rated power greater than zero",
                 index + 1U);
        return false;
    }
    return true;
}

static bool duplicate_enabled_endpoint(const inverter_config_t *inverters,
                                       uint8_t count, unsigned *first,
                                       unsigned *second)
{
    for (uint8_t left = 0; left < count; ++left) {
        if (!inverters[left].enabled) continue;
        for (uint8_t right = (uint8_t)(left + 1U); right < count; ++right) {
            if (!inverters[right].enabled) continue;
            if (inverters[left].endpoint.port == inverters[right].endpoint.port &&
                inverters[left].endpoint.unit_id == inverters[right].endpoint.unit_id &&
                strcmp(inverters[left].endpoint.host, inverters[right].endpoint.host) == 0) {
                if (first) *first = left;
                if (second) *second = right;
                return true;
            }
        }
    }
    return false;
}

static esp_err_t inverters_config_post(httpd_req_t *request)
{
    char *body = NULL;
    esp_err_t read_err = read_request_body(request, &body);
    if (read_err == ESP_ERR_INVALID_SIZE) {
        return send_json_error(request, "413 Payload Too Large",
                               "Inverter configuration body is missing or too large");
    }
    if (read_err != ESP_OK) {
        return send_json_error(request, "400 Bad Request",
                               "Unable to read inverter configuration body");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return send_json_error(request, "400 Bad Request", "Invalid JSON body");

    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "inverters");
    if (!cJSON_IsArray(array)) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request", "An inverters array is required");
    }

    int requested_count = cJSON_GetArraySize(array);
    if (requested_count < 0 || requested_count > APP_MAX_INVERTERS) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request",
                               "Inverter count exceeds the controller limit");
    }

    app_config_t *config = malloc(sizeof(*config));
    if (!config) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    esp_err_t err = config_manager_get_snapshot(config);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        free(config);
        return send_json_error(request, "500 Internal Server Error",
                               "Current configuration is unavailable");
    }

    inverter_config_t parsed[APP_MAX_INVERTERS] = {0};
    char validation_error[192] = {0};
    for (int index = 0; index < requested_count; ++index) {
        const inverter_config_t *current = index < config->inverter_count
                                               ? &config->inverters[index]
                                               : NULL;
        if (!parse_inverter(cJSON_GetArrayItem(array, index), &parsed[index], current,
                            (unsigned)index, validation_error,
                            sizeof(validation_error))) {
            cJSON_Delete(root);
            free(config);
            return send_json_error(request, "400 Bad Request", validation_error);
        }
    }
    cJSON_Delete(root);

    unsigned first = 0;
    unsigned second = 0;
    if (duplicate_enabled_endpoint(parsed, (uint8_t)requested_count, &first, &second)) {
        snprintf(validation_error, sizeof(validation_error),
                 "Inverters %u and %u use the same enabled host, port and unit ID",
                 first + 1U, second + 1U);
        free(config);
        return send_json_error(request, "409 Conflict", validation_error);
    }

    memset(config->inverters, 0, sizeof(config->inverters));
    memcpy(config->inverters, parsed, sizeof(parsed));
    config->inverter_count = (uint8_t)requested_count;
    config->control.enabled = false;

    err = config_manager_save(config);
    free(config);
    if (err != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error",
                               "Failed to persist inverter configuration");
    }

    char response[192];
    snprintf(response, sizeof(response),
             "{\"saved\":true,\"inverter_count\":%d,\"automatic_control_disabled\":true,\"restart_required\":true,\"command_registers_changed\":false}",
             requested_count);
    return send_json_text(request, NULL, response);
}

esp_err_t inverter_config_api_register(httpd_handle_t server)
{
    const httpd_uri_t handler = {
        .uri = "/api/inverters/config",
        .method = HTTP_POST,
        .handler = inverters_config_post
    };
    return httpd_register_uri_handler(server, &handler);
}
