#include "em500_settings_plan_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "meter_manager.h"

#define PLAN_MAX_BODY 4096

typedef struct {
    uint8_t meter_index;
    uint8_t function_code;
    uint8_t address_base;
    char menu[8];
} plan_request_t;

typedef struct {
    const char *key;
    uint16_t table_address;
    uint8_t words;
    uint32_t minimum;
    uint32_t maximum;
} integer_setting_t;

static const integer_setting_t s_m01_integer_settings[] = {
    {"ct_primary_a", 0x5000, 1, 1, 10000},
    {"rated_voltage_v", 0x5004, 2, 49, 500000},
    {"vt_primary_v", 0x5008, 2, 50, 500000},
    {"vt_secondary_v", 0x500A, 1, 50, 500},
    {"wiring", 0x500C, 1, 0, 5},
};

static uint16_t pdu_address(uint16_t table_address, uint8_t address_base)
{
    return table_address >= address_base
               ? (uint16_t)(table_address - address_base)
               : table_address;
}

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
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

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "error", message ? message : "Request failed");
    if (status) httpd_resp_set_status(request, status);
    return send_json(request, root);
}

static esp_err_t read_body(httpd_req_t *request, char **out_body)
{
    if (!request || !out_body || request->content_len <= 0 ||
        request->content_len > PLAN_MAX_BODY) return ESP_ERR_INVALID_SIZE;

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

static bool json_integer(cJSON *object, const char *key, uint32_t minimum,
                         uint32_t maximum, uint32_t *out, bool *present,
                         char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) {
        if (present) *present = false;
        return true;
    }
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < minimum || item->valuedouble > maximum ||
        floor(item->valuedouble) != item->valuedouble) {
        snprintf(error, error_size, "%s must be an integer from %u to %u",
                 key, (unsigned)minimum, (unsigned)maximum);
        return false;
    }
    if (present) *present = true;
    if (out) *out = (uint32_t)item->valuedouble;
    return true;
}

static bool json_bool(cJSON *object, const char *key, bool *out,
                      bool *present, char *error, size_t error_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) {
        if (present) *present = false;
        return true;
    }
    if (!cJSON_IsBool(item)) {
        snprintf(error, error_size, "%s must be boolean", key);
        return false;
    }
    if (present) *present = true;
    if (out) *out = cJSON_IsTrue(item);
    return true;
}

static bool known_m01_key(const char *key)
{
    if (!key) return false;
    if (strcmp(key, "ct_secondary_a") == 0 || strcmp(key, "use_vt") == 0) {
        return true;
    }
    for (size_t index = 0;
         index < sizeof(s_m01_integer_settings) / sizeof(s_m01_integer_settings[0]);
         ++index) {
        if (strcmp(s_m01_integer_settings[index].key, key) == 0) return true;
    }
    return false;
}

static bool only_known_keys(cJSON *changes, bool tariff,
                            char *error, size_t error_size)
{
    for (cJSON *item = changes ? changes->child : NULL; item; item = item->next) {
        bool known = tariff ? strcmp(item->string, "active_tariff") == 0
                            : known_m01_key(item->string);
        if (!known) {
            snprintf(error, error_size, "Unknown setting '%s'", item->string);
            return false;
        }
    }
    return true;
}

static void split_u32(uint32_t value, uint16_t words[2])
{
    words[0] = (uint16_t)(value >> 16);
    words[1] = (uint16_t)value;
}

static void add_words(cJSON *object, const char *key,
                      const uint16_t *words, uint8_t count)
{
    cJSON *array = cJSON_AddArrayToObject(object, key);
    for (uint8_t index = 0; index < count; ++index) {
        cJSON_AddItemToArray(array, cJSON_CreateNumber(words[index]));
    }
}

static void add_change(cJSON *changes, const char *key,
                       uint16_t table_address, uint8_t address_base,
                       const uint16_t *current_words,
                       const uint16_t *requested_words, uint8_t word_count,
                       const char *display_current,
                       const char *display_requested)
{
    if (memcmp(current_words, requested_words,
               (size_t)word_count * sizeof(current_words[0])) == 0) return;

    cJSON *change = cJSON_CreateObject();
    cJSON_AddStringToObject(change, "key", key);
    cJSON_AddNumberToObject(change, "table_address", table_address);
    cJSON_AddNumberToObject(change, "pdu_address",
                            pdu_address(table_address, address_base));
    cJSON_AddNumberToObject(change, "words", word_count);
    add_words(change, "current_raw_words", current_words, word_count);
    add_words(change, "requested_raw_words", requested_words, word_count);
    if (display_current) cJSON_AddStringToObject(change, "current", display_current);
    if (display_requested) cJSON_AddStringToObject(change, "requested", display_requested);
    cJSON_AddItemToArray(changes, change);
}

static bool parse_common(cJSON *root, plan_request_t *options,
                         cJSON **changes, char *error, size_t error_size)
{
    *options = (plan_request_t){
        .meter_index = 0,
        .function_code = 4,
        .address_base = 0,
    };
    strlcpy(options->menu, "M01", sizeof(options->menu));

    uint32_t value = 0;
    bool present = false;
    if (!json_integer(root, "index", 0, APP_MAX_METERS - 1,
                      &value, &present, error, error_size)) return false;
    if (present) options->meter_index = (uint8_t)value;

    if (!json_integer(root, "function", 3, 4,
                      &value, &present, error, error_size)) return false;
    if (present) options->function_code = (uint8_t)value;

    if (!json_integer(root, "address_base", 0, 1,
                      &value, &present, error, error_size)) return false;
    if (present) options->address_base = (uint8_t)value;

    cJSON *menu = cJSON_GetObjectItemCaseSensitive(root, "menu");
    if (menu) {
        if (!cJSON_IsString(menu) || !menu->valuestring ||
            (strcmp(menu->valuestring, "M01") != 0 &&
             strcmp(menu->valuestring, "TARIFF") != 0)) {
            strlcpy(error, "menu must be M01 or TARIFF", error_size);
            return false;
        }
        strlcpy(options->menu, menu->valuestring, sizeof(options->menu));
    }

    *changes = cJSON_GetObjectItemCaseSensitive(root, "changes");
    if (!cJSON_IsObject(*changes) || !(*changes)->child) {
        strlcpy(error, "A non-empty changes object is required", error_size);
        return false;
    }
    return only_known_keys(*changes, strcmp(options->menu, "TARIFF") == 0,
                           error, error_size);
}

static esp_err_t plan_m01(httpd_req_t *request, const plan_request_t *options,
                          cJSON *requested)
{
    uint16_t current[14] = {0};
    esp_err_t read_error = meter_manager_read_registers(
        options->meter_index, options->function_code,
        pdu_address(0x5000, options->address_base), 14, current);
    if (read_error != ESP_OK) {
        return send_error(request, "409 Conflict",
                          "Current M01 settings could not be read; no plan was created");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "menu", "M01");
    cJSON_AddNumberToObject(root, "meter_index", options->meter_index);
    cJSON_AddNumberToObject(root, "function", options->function_code);
    cJSON_AddNumberToObject(root, "address_base", options->address_base);
    cJSON_AddBoolToObject(root, "valid", true);
    cJSON_AddBoolToObject(root, "writes_performed", false);
    cJSON_AddBoolToObject(root, "apply_available", false);
    cJSON *planned = cJSON_AddArrayToObject(root, "changes");

    char validation_error[128] = {0};
    uint32_t value = 0;
    bool present = false;
    for (size_t index = 0;
         index < sizeof(s_m01_integer_settings) / sizeof(s_m01_integer_settings[0]);
         ++index) {
        const integer_setting_t *setting = &s_m01_integer_settings[index];
        if (!json_integer(requested, setting->key, setting->minimum,
                          setting->maximum, &value, &present,
                          validation_error, sizeof(validation_error))) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request", validation_error);
        }
        if (!present) continue;

        uint16_t next[2] = {0};
        if (setting->words == 1) next[0] = (uint16_t)value;
        else split_u32(value, next);
        uint16_t offset = (uint16_t)(setting->table_address - 0x5000);
        char before[24];
        char after[24];
        uint32_t current_value = setting->words == 1
                                     ? current[offset]
                                     : ((uint32_t)current[offset] << 16) |
                                       current[offset + 1U];
        snprintf(before, sizeof(before), "%u", (unsigned)current_value);
        snprintf(after, sizeof(after), "%u", (unsigned)value);
        add_change(planned, setting->key, setting->table_address,
                   options->address_base, &current[offset], next,
                   setting->words, before, after);
    }

    uint32_t secondary = 0;
    if (!json_integer(requested, "ct_secondary_a", 1, 5,
                      &secondary, &present, validation_error,
                      sizeof(validation_error))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", validation_error);
    }
    if (present && secondary != 1 && secondary != 5) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request",
                          "ct_secondary_a must be 1 or 5");
    }
    if (present) {
        uint16_t next = secondary == 1 ? 0 : 1;
        const char *before = current[2] == 0 ? "1" :
                             current[2] == 1 ? "5" : "unknown";
        const char *after = secondary == 1 ? "1" : "5";
        add_change(planned, "ct_secondary_a", 0x5002,
                   options->address_base, &current[2], &next, 1,
                   before, after);
    }

    bool use_vt = false;
    if (!json_bool(requested, "use_vt", &use_vt, &present,
                   validation_error, sizeof(validation_error))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", validation_error);
    }
    if (present) {
        uint16_t next = use_vt ? 1 : 0;
        add_change(planned, "use_vt", 0x5006,
                   options->address_base, &current[6], &next, 1,
                   current[6] ? "true" : "false",
                   use_vt ? "true" : "false");
    }

    int change_count = cJSON_GetArraySize(planned);
    cJSON_AddNumberToObject(root, "change_count", change_count);
    cJSON_AddBoolToObject(root, "no_change", change_count == 0);
    cJSON *gates = cJSON_AddObjectToObject(root, "required_gates");
    cJSON_AddBoolToObject(gates, "control_disabled", true);
    cJSON_AddBoolToObject(gates, "inverter_writes_inhibited", true);
    cJSON_AddBoolToObject(gates, "service_authorization", true);
    cJSON_AddBoolToObject(gates, "model_fingerprint", true);
    cJSON_AddBoolToObject(gates, "prewrite_snapshot", true);
    cJSON_AddBoolToObject(gates, "readback", true);
    cJSON_AddBoolToObject(gates, "rollback", true);
    cJSON_AddBoolToObject(gates, "physical_qualification", true);
    return send_json(request, root);
}

static esp_err_t plan_tariff(httpd_req_t *request,
                             const plan_request_t *options,
                             cJSON *requested)
{
    char validation_error[128] = {0};
    uint32_t tariff = 0;
    bool present = false;
    if (!json_integer(requested, "active_tariff", 1, 2,
                      &tariff, &present, validation_error,
                      sizeof(validation_error)) || !present) {
        return send_error(request, "400 Bad Request",
                          validation_error[0] ? validation_error :
                          "active_tariff is required");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "menu", "TARIFF");
    cJSON_AddNumberToObject(root, "meter_index", options->meter_index);
    cJSON_AddBoolToObject(root, "valid", true);
    cJSON_AddBoolToObject(root, "writes_performed", false);
    cJSON_AddBoolToObject(root, "apply_available", false);
    cJSON_AddStringToObject(root, "classification", "EM500_UNVERIFIED_COMMAND");
    cJSON_AddNumberToObject(root, "table_address", 0x4200);
    cJSON_AddNumberToObject(root, "pdu_address",
                            pdu_address(0x4200, options->address_base));
    cJSON_AddNumberToObject(root, "requested_tariff", tariff);

    uint16_t current = 0;
    esp_err_t read_error = meter_manager_read_registers(
        options->meter_index, options->function_code,
        pdu_address(0x4200, options->address_base), 1, &current);
    cJSON_AddBoolToObject(root, "current_readback_available", read_error == ESP_OK);
    if (read_error == ESP_OK) cJSON_AddNumberToObject(root, "current_raw", current);
    else cJSON_AddNullToObject(root, "current_raw");

    cJSON *gates = cJSON_AddObjectToObject(root, "required_gates");
    cJSON_AddBoolToObject(gates, "service_authorization", true);
    cJSON_AddBoolToObject(gates, "command_readback_qualification", true);
    cJSON_AddBoolToObject(gates, "energy_input_function_check", true);
    cJSON_AddBoolToObject(gates, "physical_qualification", true);
    return send_json(request, root);
}

static esp_err_t settings_plan_post(httpd_req_t *request)
{
    char *body = NULL;
    esp_err_t body_error = read_body(request, &body);
    if (body_error == ESP_ERR_INVALID_SIZE) {
        return send_error(request, "413 Payload Too Large",
                          "Plan body is missing or too large");
    }
    if (body_error != ESP_OK) {
        return send_error(request, "400 Bad Request", "Unable to read plan body");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return send_error(request, "400 Bad Request", "Invalid JSON body");

    plan_request_t options;
    cJSON *changes = NULL;
    char validation_error[160] = {0};
    bool valid = parse_common(root, &options, &changes,
                              validation_error, sizeof(validation_error));
    if (!valid) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", validation_error);
    }
    if (options.meter_index >= meter_manager_get_count()) {
        cJSON_Delete(root);
        return send_error(request, "404 Not Found",
                          "Requested meter index is not configured");
    }

    esp_err_t result = strcmp(options.menu, "TARIFF") == 0
                           ? plan_tariff(request, &options, changes)
                           : plan_m01(request, &options, changes);
    cJSON_Delete(root);
    return result;
}

esp_err_t em500_settings_plan_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/meters/em500/settings/plan",
        .method = HTTP_POST,
        .handler = settings_plan_post,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
