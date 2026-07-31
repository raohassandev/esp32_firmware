#include "meter_config_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "http_json.h"
#include "modbus_types.h"

#define METER_CONFIG_MAX_BODY 8192U
#define METER_CONFIG_BODY_DEADLINE_MS 5000U
#define METER_CONFIG_JSON_MAX_DEPTH 8U
#define METER_TIMEOUT_MIN_MS 100U
#define METER_TIMEOUT_MAX_MS 60000U
/* Zero means "issue the next read as soon as the previous transaction completes".
 * The engineer keeps the choice: any value above zero is honoured as a delay
 * inserted after each completed transaction. */
#define METER_POLL_MIN_MS 0U
#define METER_POLL_MAX_MS 60000U

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

static bool set_error(char *error, size_t error_size, const char *format,
                      const char *field, unsigned index)
{
    if (error && error_size) snprintf(error, error_size, format, field, index + 1U);
    return false;
}

static bool read_optional_bool(cJSON *object, const char *key, bool *out,
                               unsigned index, char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsBool(item)) {
        return set_error(error, error_size,
                         "Meter %2$u field '%1$s' must be boolean", key, index);
    }
    *out = cJSON_IsTrue(item);
    return true;
}

static bool read_optional_string(cJSON *object, const char *key, char *out,
                                 size_t out_size, unsigned index,
                                 char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsString(item) || !item->valuestring ||
        strlen(item->valuestring) >= out_size) {
        return set_error(error, error_size,
                         "Meter %2$u field '%1$s' is invalid or too long", key, index);
    }
    strlcpy(out, item->valuestring, out_size);
    return true;
}

static bool read_optional_u32(cJSON *object, const char *key, uint32_t minimum,
                              uint32_t maximum, uint32_t *out, unsigned index,
                              char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < (double)minimum ||
        item->valuedouble > (double)maximum ||
        floor(item->valuedouble) != item->valuedouble) {
        return set_error(error, error_size,
                         "Meter %2$u field '%1$s' is outside its valid integer range",
                         key, index);
    }
    *out = (uint32_t)item->valuedouble;
    return true;
}

static bool read_optional_float(cJSON *object, const char *key, float *out,
                                unsigned index, char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble == 0.0 || fabs(item->valuedouble) > 1000000.0) {
        return set_error(error, error_size,
                         "Meter %2$u field '%1$s' must be a finite non-zero scale",
                         key, index);
    }
    *out = (float)item->valuedouble;
    return true;
}

static void default_meter(meter_config_t *meter, unsigned index)
{
    memset(meter, 0, sizeof(*meter));
    snprintf(meter->name, sizeof(meter->name), "Meter %u", index + 1U);
    meter->endpoint.port = 502;
    meter->endpoint.unit_id = (uint8_t)(index + 1U);
    meter->endpoint.timeout_ms = 500;
    meter->function_code = 3;
    meter->active_power_type = MODBUS_DATA_INT32;
    meter->active_power_order = MODBUS_ORDER_ABCD;
    meter->active_power_scale = 1.0f;
    meter->poll_interval_ms = 1000;
}

static bool parse_meter(cJSON *object, const meter_config_t *current,
                        meter_config_t *next, unsigned index,
                        char *error, size_t error_size)
{
    if (!cJSON_IsObject(object) || !next) {
        if (error && error_size) snprintf(error, error_size,
                                         "Meter %u must be a JSON object", index + 1U);
        return false;
    }

    if (current) *next = *current;
    else default_meter(next, index);

    uint32_t value = 0;
    if (!read_optional_bool(object, "enabled", &next->enabled, index,
                            error, error_size) ||
        !read_optional_string(object, "name", next->name, sizeof(next->name),
                              index, error, error_size) ||
        !read_optional_string(object, "host", next->endpoint.host,
                              sizeof(next->endpoint.host), index, error, error_size)) {
        return false;
    }

    value = next->endpoint.port;
    if (!read_optional_u32(object, "port", 1, 65535, &value, index,
                           error, error_size)) return false;
    next->endpoint.port = (uint16_t)value;

    value = next->endpoint.unit_id;
    if (!read_optional_u32(object, "unit_id", 1, 247, &value, index,
                           error, error_size)) return false;
    next->endpoint.unit_id = (uint8_t)value;

    /* WHICH INSTRUMENT IS ON THE OTHER END. Commissioning data, not a label:
     * it decides how register 0x2100 is READ, and it decides whether the meter
     * is in scope for this release phase at all. Omitting the key preserves
     * whatever was already commissioned; a meter that has never had one stays
     * UNDECLARED and the commissioning gate stays closed. Nothing here infers a
     * model from the register mapping supplied alongside it. */
    value = next->model;
    if (!read_optional_u32(object, "model", METER_MODEL_UNDECLARED,
                           (uint32_t)METER_MODEL_COUNT - 1U, &value,
                           index, error, error_size)) return false;
    next->model = value;

    /* What the meter measures. The control engine selects by role, so this is
     * commissioning data, not a label. */
    value = next->role;
    if (!read_optional_u32(object, "role", METER_ROLE_UNASSIGNED, METER_ROLE_PV, &value,
                           index, error, error_size)) return false;
    next->role = (uint8_t)value;

    if (next->role == METER_ROLE_GENERATOR) {
        value = next->generator_index < APP_MAX_GENERATORS ? next->generator_index : 0U;
        if (!read_optional_u32(object, "generator_index", 0, APP_MAX_GENERATORS - 1U, &value,
                               index, error, error_size)) return false;
        next->generator_index = (uint8_t)value;
    } else {
        next->generator_index = METER_GENERATOR_INDEX_NONE;
    }

    value = next->endpoint.timeout_ms;
    if (!read_optional_u32(object, "timeout_ms", METER_TIMEOUT_MIN_MS,
                           METER_TIMEOUT_MAX_MS, &value, index,
                           error, error_size)) return false;
    next->endpoint.timeout_ms = value;

    value = next->function_code;
    if (!read_optional_u32(object, "function", 3, 4, &value, index,
                           error, error_size)) return false;
    next->function_code = (uint8_t)value;

    value = next->active_power_address;
    if (!read_optional_u32(object, "active_power_address", 0, 65535, &value,
                           index, error, error_size)) return false;
    next->active_power_address = (uint16_t)value;

    value = (uint32_t)next->active_power_type;
    if (!read_optional_u32(object, "data_type", MODBUS_DATA_UINT16,
                           MODBUS_DATA_FLOAT32, &value, index,
                           error, error_size)) return false;
    next->active_power_type = (modbus_data_type_t)value;

    value = (uint32_t)next->active_power_order;
    if (!read_optional_u32(object, "word_order", MODBUS_ORDER_ABCD,
                           MODBUS_ORDER_DCBA, &value, index,
                           error, error_size)) return false;
    next->active_power_order = (modbus_word_order_t)value;

    if (!read_optional_float(object, "scale", &next->active_power_scale,
                             index, error, error_size)) return false;

    value = next->poll_interval_ms;
    if (!read_optional_u32(object, "poll_ms", METER_POLL_MIN_MS,
                           METER_POLL_MAX_MS, &value, index,
                           error, error_size)) return false;
    next->poll_interval_ms = value;

    if (!next->name[0]) {
        if (error && error_size) snprintf(error, error_size,
                                         "Meter %u requires a name", index + 1U);
        return false;
    }
    if (!isfinite(next->active_power_scale) || next->active_power_scale == 0.0f) {
        if (error && error_size) snprintf(error, error_size,
                                         "Meter %u has an invalid scale", index + 1U);
        return false;
    }
    if (next->enabled && !next->endpoint.host[0]) {
        if (error && error_size) snprintf(error, error_size,
                                         "Enabled meter %u requires a host", index + 1U);
        return false;
    }
    return true;
}

static bool duplicate_enabled_endpoint(const meter_config_t *meters,
                                       uint8_t count, unsigned *first,
                                       unsigned *second)
{
    for (uint8_t left = 0; left < count; ++left) {
        if (!meters[left].enabled) continue;
        for (uint8_t right = (uint8_t)(left + 1U); right < count; ++right) {
            if (!meters[right].enabled) continue;
            if (meters[left].endpoint.port == meters[right].endpoint.port &&
                meters[left].endpoint.unit_id == meters[right].endpoint.unit_id &&
                strcmp(meters[left].endpoint.host, meters[right].endpoint.host) == 0) {
                if (first) *first = left;
                if (second) *second = right;
                return true;
            }
        }
    }
    return false;
}

static esp_err_t meters_config_post(httpd_req_t *request)
{
    cJSON *root = NULL;
    esp_err_t read_err = http_json_parse_bounded(request,
                                                  METER_CONFIG_MAX_BODY,
                                                  METER_CONFIG_BODY_DEADLINE_MS,
                                                  METER_CONFIG_JSON_MAX_DEPTH,
                                                  &root);
    if (read_err == ESP_ERR_INVALID_SIZE) {
        return send_json_error(request, "413 Payload Too Large",
                               "Meter configuration body is missing or too large");
    }
    if (read_err == ESP_ERR_TIMEOUT) {
        return send_json_error(request, "408 Request Timeout",
                               "Meter configuration body timed out");
    }
    if (read_err != ESP_OK) {
        return send_json_error(request, "400 Bad Request",
                               "Meter configuration must be valid bounded JSON");
    }

    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "meters");
    if (!cJSON_IsArray(array)) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request", "A meters array is required");
    }

    int requested_count = cJSON_GetArraySize(array);
    if (requested_count < 0 || requested_count > APP_MAX_METERS) {
        cJSON_Delete(root);
        return send_json_error(request, "400 Bad Request",
                               "Meter count exceeds the controller limit");
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

    meter_config_t parsed[APP_MAX_METERS] = {0};
    char validation_error[160] = {0};
    for (int index = 0; index < requested_count; ++index) {
        const meter_config_t *current = index < config->meter_count
                                            ? &config->meters[index]
                                            : NULL;
        if (!parse_meter(cJSON_GetArrayItem(array, index), current,
                         &parsed[index], (unsigned)index,
                         validation_error, sizeof(validation_error))) {
            cJSON_Delete(root);
            free(config);
            return send_json_error(request, "400 Bad Request", validation_error);
        }
    }
    cJSON_Delete(root);

    unsigned first = 0;
    unsigned second = 0;
    if (duplicate_enabled_endpoint(parsed, (uint8_t)requested_count,
                                   &first, &second)) {
        snprintf(validation_error, sizeof(validation_error),
                 "Meters %u and %u use the same enabled host, port and unit ID",
                 first + 1U, second + 1U);
        free(config);
        return send_json_error(request, "409 Conflict", validation_error);
    }

    memset(config->meters, 0, sizeof(config->meters));
    if (requested_count > 0) {
        memcpy(config->meters, parsed,
               (size_t)requested_count * sizeof(parsed[0]));
    }
    config->meter_count = (uint8_t)requested_count;

    /* A meter mapping change invalidates every live-control assumption. */
    config->control.enabled = false;

    err = config_manager_save(config);
    free(config);
    if (err != ESP_OK) {
        return send_json_error(request, "500 Internal Server Error",
                               "Meter configuration could not be persisted and verified");
    }

    char response[160];
    snprintf(response, sizeof(response),
             "{\"saved\":true,\"persisted\":true,\"meter_count\":%d,"
             "\"control_enabled\":false,\"restart_required\":true}",
             requested_count);
    return send_json_text(request, "202 Accepted", response);
}

esp_err_t meter_config_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/meters/config",
        .method = HTTP_POST,
        .handler = meters_config_post,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
