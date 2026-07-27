#include "em500_history_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "meter_manager.h"
#include "modbus_decoder.h"
#include "modbus_types.h"

#define HISTORY_QUERY_MAX 160
#define HISTORY_WORDS 72

typedef struct {
    const char *key;
    uint16_t offset;
    modbus_data_type_t type;
    float scale;
    const char *unit;
} history_register_t;

typedef struct {
    uint8_t meter_index;
    uint8_t function_code;
    uint8_t address_base;
    char block[16];
} history_request_t;

static const history_register_t s_history[] = {
    {"voltage_l1_n", 0x00, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"voltage_l2_n", 0x02, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"voltage_l3_n", 0x04, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"current_l1", 0x06, MODBUS_DATA_UINT32, 0.0001f, "A"},
    {"current_l2", 0x08, MODBUS_DATA_UINT32, 0.0001f, "A"},
    {"current_l3", 0x0A, MODBUS_DATA_UINT32, 0.0001f, "A"},
    {"voltage_l1_l2", 0x0C, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"voltage_l2_l3", 0x0E, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"voltage_l3_l1", 0x10, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"active_power_l1", 0x12, MODBUS_DATA_INT32, 0.00001f, "kW"},
    {"active_power_l2", 0x14, MODBUS_DATA_INT32, 0.00001f, "kW"},
    {"active_power_l3", 0x16, MODBUS_DATA_INT32, 0.00001f, "kW"},
    {"reactive_power_l1", 0x18, MODBUS_DATA_INT32, 0.00001f, "kvar"},
    {"reactive_power_l2", 0x1A, MODBUS_DATA_INT32, 0.00001f, "kvar"},
    {"reactive_power_l3", 0x1C, MODBUS_DATA_INT32, 0.00001f, "kvar"},
    {"apparent_power_l1", 0x1E, MODBUS_DATA_UINT32, 0.00001f, "kVA"},
    {"apparent_power_l2", 0x20, MODBUS_DATA_UINT32, 0.00001f, "kVA"},
    {"apparent_power_l3", 0x22, MODBUS_DATA_UINT32, 0.00001f, "kVA"},
    {"power_factor_l1", 0x24, MODBUS_DATA_INT32, 0.0001f, "ratio"},
    {"power_factor_l2", 0x26, MODBUS_DATA_INT32, 0.0001f, "ratio"},
    {"power_factor_l3", 0x28, MODBUS_DATA_INT32, 0.0001f, "ratio"},
    {"frequency", 0x30, MODBUS_DATA_UINT32, 0.001f, "Hz"},
    {"voltage_phase_equivalent", 0x32, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"voltage_line_equivalent", 0x34, MODBUS_DATA_UINT32, 0.01f, "V"},
    {"current_equivalent", 0x36, MODBUS_DATA_UINT32, 0.0001f, "A"},
    {"active_power_total", 0x38, MODBUS_DATA_INT32, 0.00001f, "kW"},
    {"reactive_power_total", 0x3A, MODBUS_DATA_INT32, 0.00001f, "kvar"},
    {"apparent_power_total", 0x3C, MODBUS_DATA_UINT32, 0.00001f, "kVA"},
    {"power_factor_total", 0x3E, MODBUS_DATA_INT32, 0.0001f, "ratio"},
    {"voltage_line_asymmetry", 0x40, MODBUS_DATA_UINT32, 0.01f, "%"},
    {"voltage_phase_asymmetry", 0x42, MODBUS_DATA_UINT32, 0.01f, "%"},
    {"current_asymmetry", 0x44, MODBUS_DATA_UINT32, 0.01f, "%"},
    {"current_neutral", 0x46, MODBUS_DATA_UINT32, 0.0001f, "A"},
};

static uint16_t pdu_address(uint16_t table_address, uint8_t address_base)
{
    return table_address >= address_base
               ? (uint16_t)(table_address - address_base)
               : table_address;
}

static bool parse_u8(const char *text, uint8_t minimum, uint8_t maximum,
                     uint8_t *out)
{
    if (!text || !text[0] || !out) return false;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!end || *end || value < minimum || value > maximum) return false;
    *out = (uint8_t)value;
    return true;
}

static bool parse_request(httpd_req_t *request, history_request_t *options)
{
    *options = (history_request_t){
        .meter_index = 0,
        .function_code = 3,
        .address_base = 0,
    };
    strlcpy(options->block, "maximum", sizeof(options->block));

    size_t length = httpd_req_get_url_query_len(request);
    if (!length) return true;
    if (length >= HISTORY_QUERY_MAX) return false;

    char query[HISTORY_QUERY_MAX];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    char value[24];
    if (httpd_query_key_value(query, "index", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 0, APP_MAX_METERS - 1, &options->meter_index)) return false;
    if (httpd_query_key_value(query, "function", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 3, 4, &options->function_code)) return false;
    if (httpd_query_key_value(query, "address_base", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 0, 1, &options->address_base)) return false;
    if (httpd_query_key_value(query, "block", value, sizeof(value)) == ESP_OK) {
        if (strlen(value) >= sizeof(options->block)) return false;
        strlcpy(options->block, value, sizeof(options->block));
    }

    return strcmp(options->block, "maximum") == 0 ||
           strcmp(options->block, "minimum") == 0 ||
           strcmp(options->block, "average") == 0 ||
           strcmp(options->block, "demand") == 0;
}

static uint16_t block_base(const char *block)
{
    if (strcmp(block, "maximum") == 0) return 0x0400;
    if (strcmp(block, "minimum") == 0) return 0x0600;
    if (strcmp(block, "average") == 0) return 0x0800;
    return 0x0A00;
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

static esp_err_t history_get(httpd_req_t *request)
{
    history_request_t options;
    if (!parse_request(request, &options)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Use index=0..3, function=3|4, address_base=0|1 and block=maximum|minimum|average|demand");
    }
    if (options.meter_index >= meter_manager_get_count()) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                                   "Requested meter index is not configured");
    }

    uint16_t base = block_base(options.block);
    uint16_t registers[HISTORY_WORDS] = {0};
    esp_err_t read_error = meter_manager_read_registers(
        options.meter_index, options.function_code,
        pdu_address(base, options.address_base),
        HISTORY_WORDS, registers);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "profile", "EM500_DMG610_COMPATIBLE");
    cJSON_AddBoolToObject(root, "read_only", true);
    cJSON_AddNumberToObject(root, "meter_index", options.meter_index);
    cJSON_AddNumberToObject(root, "function", options.function_code);
    cJSON_AddNumberToObject(root, "address_base", options.address_base);
    cJSON_AddStringToObject(root, "block", options.block);
    cJSON_AddNumberToObject(root, "table_base", base);
    cJSON_AddNumberToObject(root, "pdu_base",
                            pdu_address(base, options.address_base));
    cJSON_AddNumberToObject(root, "read_words", HISTORY_WORDS);

    if (read_error != ESP_OK) {
        cJSON_AddBoolToObject(root, "available", false);
        cJSON_AddStringToObject(root, "error", esp_err_to_name(read_error));
        return send_json(request, root);
    }

    cJSON_AddBoolToObject(root, "available", true);
    cJSON *values = cJSON_AddObjectToObject(root, "values");
    for (size_t index = 0; index < sizeof(s_history) / sizeof(s_history[0]); ++index) {
        const history_register_t *descriptor = &s_history[index];
        cJSON *object = cJSON_AddObjectToObject(values, descriptor->key);
        uint16_t table_address = (uint16_t)(base + descriptor->offset);
        cJSON_AddNumberToObject(object, "table_address", table_address);
        cJSON_AddNumberToObject(object, "pdu_address",
                                pdu_address(table_address, options.address_base));
        cJSON_AddNumberToObject(object, "words", 2);
        cJSON_AddStringToObject(object, "unit", descriptor->unit);
        float value = 0.0f;
        if ((uint32_t)descriptor->offset + 2U <= HISTORY_WORDS &&
            modbus_decode_scaled(&registers[descriptor->offset], 2,
                                 descriptor->type, MODBUS_ORDER_ABCD,
                                 descriptor->scale, 0.0f, &value) == ESP_OK) {
            cJSON_AddNumberToObject(object, "value", value);
        } else {
            cJSON_AddNullToObject(object, "value");
        }
    }
    return send_json(request, root);
}

esp_err_t em500_history_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/meters/em500/history",
        .method = HTTP_GET,
        .handler = history_get,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
