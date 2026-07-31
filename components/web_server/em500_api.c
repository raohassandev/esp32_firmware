#include "em500_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "em500_cache.h"
#include "meter_manager.h"
#include "modbus_decoder.h"
#include "modbus_types.h"

#define EM500_QUERY_MAX 160
#define EM500_MAX_READ_REGISTERS 80

typedef struct {
    uint8_t meter_index;
    uint8_t function_code;
    uint8_t address_base;
    char scope[16];
} em500_request_t;

typedef struct {
    const char *key;
    uint16_t table_address;
    modbus_data_type_t type;
    double scale;
    const char *unit;
} scaled_register_t;

typedef struct {
    const char *key;
    uint16_t table_address;
    const char *unit;
} energy_register_t;

static const scaled_register_t s_instantaneous[] = {
    {"voltage_l1_n", 0x0002, MODBUS_DATA_UINT32, 0.01, "V"},
    {"voltage_l2_n", 0x0004, MODBUS_DATA_UINT32, 0.01, "V"},
    {"voltage_l3_n", 0x0006, MODBUS_DATA_UINT32, 0.01, "V"},
    {"current_l1", 0x0008, MODBUS_DATA_UINT32, 0.0001, "A"},
    {"current_l2", 0x000A, MODBUS_DATA_UINT32, 0.0001, "A"},
    {"current_l3", 0x000C, MODBUS_DATA_UINT32, 0.0001, "A"},
    {"voltage_l1_l2", 0x000E, MODBUS_DATA_UINT32, 0.01, "V"},
    {"voltage_l2_l3", 0x0010, MODBUS_DATA_UINT32, 0.01, "V"},
    {"voltage_l3_l1", 0x0012, MODBUS_DATA_UINT32, 0.01, "V"},
    {"active_power_l1", 0x0014, MODBUS_DATA_INT32, 0.00001, "kW"},
    {"active_power_l2", 0x0016, MODBUS_DATA_INT32, 0.00001, "kW"},
    {"active_power_l3", 0x0018, MODBUS_DATA_INT32, 0.00001, "kW"},
    {"reactive_power_l1", 0x001A, MODBUS_DATA_INT32, 0.00001, "kvar"},
    {"reactive_power_l2", 0x001C, MODBUS_DATA_INT32, 0.00001, "kvar"},
    {"reactive_power_l3", 0x001E, MODBUS_DATA_INT32, 0.00001, "kvar"},
    {"apparent_power_l1", 0x0020, MODBUS_DATA_UINT32, 0.00001, "kVA"},
    {"apparent_power_l2", 0x0022, MODBUS_DATA_UINT32, 0.00001, "kVA"},
    {"apparent_power_l3", 0x0024, MODBUS_DATA_UINT32, 0.00001, "kVA"},
    {"power_factor_l1", 0x0026, MODBUS_DATA_INT32, 0.0001, "ratio"},
    {"power_factor_l2", 0x0028, MODBUS_DATA_INT32, 0.0001, "ratio"},
    {"power_factor_l3", 0x002A, MODBUS_DATA_INT32, 0.0001, "ratio"},
    {"frequency", 0x0032, MODBUS_DATA_UINT32, 0.001, "Hz"},
    {"voltage_phase_equivalent", 0x0034, MODBUS_DATA_UINT32, 0.01, "V"},
    {"voltage_line_equivalent", 0x0036, MODBUS_DATA_UINT32, 0.01, "V"},
    {"current_equivalent", 0x0038, MODBUS_DATA_UINT32, 0.0001, "A"},
    {"active_power_total", 0x003A, MODBUS_DATA_INT32, 0.00001, "kW"},
    {"reactive_power_total", 0x003C, MODBUS_DATA_INT32, 0.00001, "kvar"},
    {"apparent_power_total", 0x003E, MODBUS_DATA_UINT32, 0.00001, "kVA"},
    {"power_factor_total", 0x0040, MODBUS_DATA_INT32, 0.0001, "ratio"},
    {"voltage_line_asymmetry", 0x0042, MODBUS_DATA_UINT32, 0.01, "%"},
    {"voltage_phase_asymmetry", 0x0044, MODBUS_DATA_UINT32, 0.01, "%"},
    {"current_asymmetry", 0x0046, MODBUS_DATA_UINT32, 0.01, "%"},
    {"current_neutral", 0x0048, MODBUS_DATA_UINT32, 0.0001, "A"},
    {"voltage_thd_l1", 0x0054, MODBUS_DATA_UINT32, 0.01, "%"},
    {"voltage_thd_l2", 0x0056, MODBUS_DATA_UINT32, 0.01, "%"},
    {"voltage_thd_l3", 0x0058, MODBUS_DATA_UINT32, 0.01, "%"},
    {"current_thd_l1", 0x005A, MODBUS_DATA_UINT32, 0.01, "%"},
    {"current_thd_l2", 0x005C, MODBUS_DATA_UINT32, 0.01, "%"},
    {"current_thd_l3", 0x005E, MODBUS_DATA_UINT32, 0.01, "%"},
    {"voltage_thd_l1_l2", 0x0060, MODBUS_DATA_UINT32, 0.01, "%"},
    {"voltage_thd_l2_l3", 0x0062, MODBUS_DATA_UINT32, 0.01, "%"},
    {"voltage_thd_l3_l1", 0x0064, MODBUS_DATA_UINT32, 0.01, "%"},
};

static const energy_register_t s_total_energy[] = {
    {"total_imported_active", 0x1B20, "kWh"},
    {"total_exported_active", 0x1B24, "kWh"},
    {"total_imported_reactive", 0x1B28, "kvarh"},
    {"total_exported_reactive", 0x1B2C, "kvarh"},
    {"total_apparent", 0x1B30, "kVAh"},
    {"partial_imported_active", 0x1B34, "kWh"},
    {"partial_exported_active", 0x1B38, "kWh"},
    {"partial_imported_reactive", 0x1B3C, "kvarh"},
    {"partial_exported_reactive", 0x1B40, "kvarh"},
    {"partial_apparent", 0x1B44, "kVAh"},
    {"tariff_1_imported_active", 0x1B48, "kWh"},
    {"tariff_1_exported_active", 0x1B4C, "kWh"},
    {"tariff_1_imported_reactive", 0x1B50, "kvarh"},
    {"tariff_1_exported_reactive", 0x1B54, "kvarh"},
    {"tariff_1_apparent", 0x1B58, "kVAh"},
    {"tariff_2_imported_active", 0x1B5C, "kWh"},
    {"tariff_2_exported_active", 0x1B60, "kWh"},
    {"tariff_2_imported_reactive", 0x1B64, "kvarh"},
    {"tariff_2_exported_reactive", 0x1B68, "kvarh"},
    {"tariff_2_apparent", 0x1B6C, "kVAh"},
};

static const energy_register_t s_phase_energy[] = {
    {"imported_active", 0x00, "kWh"},
    {"exported_active", 0x04, "kWh"},
    {"imported_reactive", 0x08, "kvarh"},
    {"exported_reactive", 0x0C, "kvarh"},
    {"apparent", 0x10, "kVAh"},
    {"partial_imported_active", 0x14, "kWh"},
    {"partial_exported_active", 0x18, "kWh"},
    {"partial_imported_reactive", 0x1C, "kvarh"},
    {"partial_exported_reactive", 0x20, "kvarh"},
    {"partial_apparent", 0x24, "kVAh"},
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
    esp_err_t err = httpd_resp_sendstr(request, json);
    free(json);
    return err;
}

static esp_err_t send_error(httpd_req_t *request, httpd_err_code_t status,
                            const char *message)
{
    return httpd_resp_send_err(request, status, message);
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

static bool parse_request(httpd_req_t *request, em500_request_t *options)
{
    *options = (em500_request_t){
        .meter_index = 0,
        .function_code = 3,
        .address_base = 0,
    };
    strlcpy(options->scope, "instantaneous", sizeof(options->scope));

    size_t query_length = httpd_req_get_url_query_len(request);
    if (!query_length) return true;
    if (query_length >= EM500_QUERY_MAX) return false;

    char query[EM500_QUERY_MAX];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    char value[24];
    if (httpd_query_key_value(query, "index", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 0, APP_MAX_METERS - 1, &options->meter_index)) {
        return false;
    }
    if (httpd_query_key_value(query, "function", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 3, 4, &options->function_code)) {
        return false;
    }
    if (httpd_query_key_value(query, "address_base", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 0, 1, &options->address_base)) {
        return false;
    }
    if (httpd_query_key_value(query, "scope", value, sizeof(value)) == ESP_OK) {
        if (strlen(value) >= sizeof(options->scope)) return false;
        strlcpy(options->scope, value, sizeof(options->scope));
    }

    return strcmp(options->scope, "instantaneous") == 0 ||
           strcmp(options->scope, "energy") == 0 ||
           strcmp(options->scope, "setup") == 0 ||
           strcmp(options->scope, "all") == 0;
}

static esp_err_t read_block(const em500_request_t *options,
                            uint16_t table_address, uint16_t count,
                            uint16_t *registers)
{
    if (count > EM500_MAX_READ_REGISTERS) return ESP_ERR_INVALID_SIZE;
    return meter_manager_read_registers(options->meter_index,
                                        options->function_code,
                                        pdu_address(table_address, options->address_base),
                                        count, registers);
}

static cJSON *add_register_object(cJSON *parent, const char *key,
                                  uint16_t table_address, uint8_t address_base,
                                  uint8_t words, const char *unit)
{
    cJSON *object = cJSON_AddObjectToObject(parent, key);
    cJSON_AddNumberToObject(object, "table_address", table_address);
    cJSON_AddNumberToObject(object, "pdu_address",
                            pdu_address(table_address, address_base));
    cJSON_AddNumberToObject(object, "words", words);
    if (unit) cJSON_AddStringToObject(object, "unit", unit);
    return object;
}

static void add_scaled_value(cJSON *parent, const scaled_register_t *descriptor,
                             const uint16_t *registers, uint16_t block_start,
                             uint16_t block_count, uint8_t address_base)
{
    cJSON *object = add_register_object(parent, descriptor->key,
                                        descriptor->table_address,
                                        address_base, 2, descriptor->unit);
    uint16_t offset = (uint16_t)(descriptor->table_address - block_start);
    float value = 0.0f;
    if ((uint32_t)offset + 2U <= block_count &&
        modbus_decode_scaled(&registers[offset], 2, descriptor->type,
                             MODBUS_ORDER_ABCD, (float)descriptor->scale,
                             0.0f, &value) == ESP_OK) {
        cJSON_AddNumberToObject(object, "value", value);
    } else {
        cJSON_AddNullToObject(object, "value");
    }
}

static uint64_t raw_u64(const uint16_t *registers)
{
    return ((uint64_t)registers[0] << 48) |
           ((uint64_t)registers[1] << 32) |
           ((uint64_t)registers[2] << 16) |
           (uint64_t)registers[3];
}

static void add_energy_value(cJSON *parent, const char *key,
                             uint16_t table_address, const char *unit,
                             const uint16_t *registers, uint16_t block_start,
                             uint16_t block_count, uint8_t address_base)
{
    cJSON *object = add_register_object(parent, key, table_address,
                                        address_base, 4, unit);
    uint16_t offset = (uint16_t)(table_address - block_start);
    if ((uint32_t)offset + 4U > block_count) {
        cJSON_AddNullToObject(object, "value");
        return;
    }

    double value = 0.0;
    if (modbus_decode_u64_be_scaled(&registers[offset], 4, 0.01, 0.0,
                                    &value) != ESP_OK) {
        cJSON_AddNullToObject(object, "value");
        return;
    }

    char raw_decimal[32];
    char raw_hex[24];
    uint64_t raw = raw_u64(&registers[offset]);
    snprintf(raw_decimal, sizeof(raw_decimal), "%" PRIu64, raw);
    snprintf(raw_hex, sizeof(raw_hex), "0x%016" PRIX64, raw);
    cJSON_AddNumberToObject(object, "value", value);
    cJSON_AddStringToObject(object, "raw_u64", raw_decimal);
    cJSON_AddStringToObject(object, "raw_hex", raw_hex);
}

static void add_group_error(cJSON *group, esp_err_t error)
{
    cJSON_AddBoolToObject(group, "available", false);
    cJSON_AddStringToObject(group, "error", esp_err_to_name(error));
}

static void add_instantaneous(cJSON *root, const em500_request_t *options)
{
    cJSON *group = cJSON_AddObjectToObject(root, "instantaneous");
    uint16_t registers[100] = {0};
    esp_err_t first = read_block(options, 0x0002, 78, registers);
    esp_err_t second = first == ESP_OK
                           ? read_block(options, 0x0050, 22, &registers[78])
                           : first;
    if (first != ESP_OK || second != ESP_OK) {
        add_group_error(group, first != ESP_OK ? first : second);
        return;
    }

    cJSON_AddBoolToObject(group, "available", true);
    cJSON_AddNumberToObject(group, "block_1_table_start", 0x0002);
    cJSON_AddNumberToObject(group, "block_1_pdu_start",
                            pdu_address(0x0002, options->address_base));
    cJSON_AddNumberToObject(group, "block_1_count", 78);
    cJSON_AddNumberToObject(group, "block_2_table_start", 0x0050);
    cJSON_AddNumberToObject(group, "block_2_pdu_start",
                            pdu_address(0x0050, options->address_base));
    cJSON_AddNumberToObject(group, "block_2_count", 22);

    cJSON *values = cJSON_AddObjectToObject(group, "values");
    for (size_t index = 0; index < sizeof(s_instantaneous) / sizeof(s_instantaneous[0]); ++index) {
        add_scaled_value(values, &s_instantaneous[index], registers,
                         0x0002, 100, options->address_base);
    }

    cJSON *source = cJSON_AddObjectToObject(group, "source_input");
    cJSON_AddNumberToObject(source, "table_address", EM500_SOURCE_INPUT_TABLE_ADDRESS);
    cJSON_AddNumberToObject(source, "pdu_address",
                            pdu_address(EM500_SOURCE_INPUT_TABLE_ADDRESS, options->address_base));
    cJSON_AddStringToObject(source, "classification", "clone_specific");
    cJSON_AddStringToObject(source, "site_mapping", "0=grid,1=generator");
    uint16_t source_register = 0;
    esp_err_t source_error =
        read_block(options, EM500_SOURCE_INPUT_TABLE_ADDRESS, 1, &source_register);
    if (source_error == ESP_OK) {
        cJSON_AddBoolToObject(source, "available", true);
        cJSON_AddNumberToObject(source, "raw", source_register);
        if (source_register == 0) cJSON_AddStringToObject(source, "requested_source", "grid");
        else if (source_register == 1) cJSON_AddStringToObject(source, "requested_source", "generator");
        else cJSON_AddStringToObject(source, "requested_source", "invalid");
    } else {
        cJSON_AddBoolToObject(source, "available", false);
        cJSON_AddNullToObject(source, "raw");
        cJSON_AddStringToObject(source, "error", esp_err_to_name(source_error));
    }
}

static void add_hours(cJSON *energy, const em500_request_t *options)
{
    cJSON *hours = cJSON_AddObjectToObject(energy, "hour_counters");
    uint16_t registers[10] = {0};
    esp_err_t err = read_block(options, 0x1E00, 10, registers);
    if (err != ESP_OK) {
        add_group_error(hours, err);
        return;
    }

    cJSON_AddBoolToObject(hours, "available", true);
    static const char *keys[] = {
        "total", "partial_1", "partial_2", "partial_3", "partial_4"
    };
    cJSON *values = cJSON_AddObjectToObject(hours, "values");
    for (uint16_t index = 0; index < 5; ++index) {
        cJSON *object = add_register_object(values, keys[index],
                                            (uint16_t)(0x1E00 + index * 2U),
                                            options->address_base, 2, "hours");
        float decoded = 0.0f;
        if (modbus_decode_scaled(&registers[index * 2U], 2,
                                 MODBUS_DATA_UINT32, MODBUS_ORDER_ABCD,
                                 1.0f, 0.0f, &decoded) == ESP_OK) {
            cJSON_AddNumberToObject(object, "value", decoded);
        } else {
            cJSON_AddNullToObject(object, "value");
        }
    }
}

static void add_phase_energy(cJSON *energy, const em500_request_t *options,
                             const char *phase_name, uint16_t phase_base)
{
    cJSON *phase = cJSON_AddObjectToObject(energy, phase_name);
    uint16_t registers[40] = {0};
    esp_err_t err = read_block(options, phase_base, 40, registers);
    if (err != ESP_OK) {
        add_group_error(phase, err);
        return;
    }

    cJSON_AddBoolToObject(phase, "available", true);
    cJSON *values = cJSON_AddObjectToObject(phase, "values");
    for (size_t index = 0; index < sizeof(s_phase_energy) / sizeof(s_phase_energy[0]); ++index) {
        add_energy_value(values, s_phase_energy[index].key,
                         (uint16_t)(phase_base + s_phase_energy[index].table_address),
                         s_phase_energy[index].unit, registers, phase_base, 40,
                         options->address_base);
    }
}

static void add_energy(cJSON *root, const em500_request_t *options)
{
    cJSON *group = cJSON_AddObjectToObject(root, "energy");
    uint16_t registers[80] = {0};
    esp_err_t err = read_block(options, 0x1B20, 80, registers);
    if (err != ESP_OK) {
        add_group_error(group, err);
    } else {
        cJSON_AddBoolToObject(group, "available", true);
        cJSON *values = cJSON_AddObjectToObject(group, "totals_and_tariffs");
        for (size_t index = 0; index < sizeof(s_total_energy) / sizeof(s_total_energy[0]); ++index) {
            add_energy_value(values, s_total_energy[index].key,
                             s_total_energy[index].table_address,
                             s_total_energy[index].unit,
                             registers, 0x1B20, 80, options->address_base);
        }
    }

    add_hours(group, options);
    add_phase_energy(group, options, "phase_l1", 0x1E20);
    add_phase_energy(group, options, "phase_l2", 0x1E48);
    add_phase_energy(group, options, "phase_l3", 0x1E70);
}

static void add_setup_u16(cJSON *values, const char *key,
                          uint16_t table_address, uint16_t raw,
                          uint8_t address_base)
{
    cJSON *object = add_register_object(values, key, table_address,
                                        address_base, 1, NULL);
    cJSON_AddNumberToObject(object, "raw", raw);
}

static uint32_t combine_u32(const uint16_t *registers)
{
    return ((uint32_t)registers[0] << 16) | registers[1];
}

static void add_setup(cJSON *root, const em500_request_t *options)
{
    cJSON *group = cJSON_AddObjectToObject(root, "setup");
    cJSON_AddBoolToObject(group, "writes_enabled", false);
    cJSON_AddStringToObject(group, "write_policy",
                            "read_only_until_model_fingerprint_and_rollback_qualification");

    uint16_t registers[14] = {0};
    esp_err_t err = read_block(options, 0x5000, 14, registers);
    if (err != ESP_OK) {
        add_group_error(group, err);
        return;
    }

    cJSON_AddBoolToObject(group, "available", true);
    cJSON *values = cJSON_AddObjectToObject(group, "general");

    add_setup_u16(values, "ct_primary_a", 0x5000, registers[0],
                  options->address_base);

    cJSON *ct_secondary = add_register_object(values, "ct_secondary_a",
                                               0x5002, options->address_base,
                                               1, "A");
    cJSON_AddNumberToObject(ct_secondary, "raw", registers[2]);
    if (registers[2] == 0) cJSON_AddNumberToObject(ct_secondary, "value", 1);
    else if (registers[2] == 1) cJSON_AddNumberToObject(ct_secondary, "value", 5);
    else cJSON_AddNullToObject(ct_secondary, "value");

    cJSON *rated = add_register_object(values, "rated_voltage_v", 0x5004,
                                       options->address_base, 2, "V");
    cJSON_AddNumberToObject(rated, "value", combine_u32(&registers[4]));

    cJSON *use_vt = add_register_object(values, "use_vt", 0x5006,
                                        options->address_base, 1, NULL);
    cJSON_AddNumberToObject(use_vt, "raw", registers[6]);
    cJSON_AddBoolToObject(use_vt, "value", registers[6] != 0);

    cJSON *vt_primary = add_register_object(values, "vt_primary_v", 0x5008,
                                             options->address_base, 2, "V");
    cJSON_AddNumberToObject(vt_primary, "value", combine_u32(&registers[8]));

    cJSON *vt_secondary = add_register_object(values, "vt_secondary_v", 0x500A,
                                               options->address_base, 1, "V");
    cJSON_AddNumberToObject(vt_secondary, "value", registers[10]);

    static const char *wiring_names[] = {
        "L1-L2-L3-N", "L1-L2-L3", "L1-L2-L3-N BIL",
        "L1-L2-L3 BIL", "L1-N-L2", "L1-N"
    };
    cJSON *wiring = add_register_object(values, "wiring", 0x500C,
                                        options->address_base, 1, NULL);
    cJSON_AddNumberToObject(wiring, "raw", registers[12]);
    if (registers[12] < sizeof(wiring_names) / sizeof(wiring_names[0])) {
        cJSON_AddStringToObject(wiring, "value", wiring_names[registers[12]]);
    } else {
        cJSON_AddStringToObject(wiring, "value", "unknown");
    }

    cJSON *tariff = cJSON_AddObjectToObject(group, "tariff_control");
    cJSON_AddNumberToObject(tariff, "command_table_address", 0x4200);
    cJSON_AddNumberToObject(tariff, "command_pdu_address",
                            pdu_address(0x4200, options->address_base));
    cJSON_AddStringToObject(tariff, "allowed_values_documented", "1 or 2");
    cJSON_AddBoolToObject(tariff, "write_enabled", false);
    cJSON_AddBoolToObject(tariff, "physical_readback_qualified", false);
}

static esp_err_t snapshot_get(httpd_req_t *request)
{
    em500_request_t options;
    if (!parse_request(request, &options)) {
        return send_error(request, HTTPD_400_BAD_REQUEST,
                          "Use index=0..3, function=3|4, address_base=0|1 and scope=instantaneous|energy|setup|all");
    }
    if (options.meter_index >= meter_manager_get_count()) {
        return send_error(request, HTTPD_404_NOT_FOUND,
                          "Requested meter index is not configured");
    }

    app_config_t *config = malloc(sizeof(*config));
    if (!config) return httpd_resp_send_500(request);
    esp_err_t config_error = config_manager_get_snapshot(config);
    if (config_error != ESP_OK) {
        free(config);
        return send_error(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Meter configuration is unavailable");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(config);
        return httpd_resp_send_500(request);
    }

    const meter_config_t *meter = &config->meters[options.meter_index];
    cJSON_AddStringToObject(root, "profile", "EM500_DMG610_COMPATIBLE");
    cJSON_AddStringToObject(root, "compatibility", "clone_requires_physical_write_qualification");
    cJSON_AddNumberToObject(root, "meter_index", options.meter_index);
    cJSON_AddStringToObject(root, "meter_name", meter->name);
    cJSON_AddStringToObject(root, "host", meter->endpoint.host);
    cJSON_AddNumberToObject(root, "port", meter->endpoint.port);
    cJSON_AddNumberToObject(root, "unit_id", meter->endpoint.unit_id);
    cJSON_AddNumberToObject(root, "function", options.function_code);
    cJSON_AddNumberToObject(root, "address_base", options.address_base);
    cJSON_AddStringToObject(root, "scope", options.scope);
    cJSON_AddBoolToObject(root, "read_only", true);
    free(config);

    if (strcmp(options.scope, "instantaneous") == 0 ||
        strcmp(options.scope, "all") == 0) {
        add_instantaneous(root, &options);
    }
    if (strcmp(options.scope, "energy") == 0 ||
        strcmp(options.scope, "all") == 0) {
        add_energy(root, &options);
    }
    if (strcmp(options.scope, "setup") == 0 ||
        strcmp(options.scope, "all") == 0) {
        add_setup(root, &options);
    }

    return send_json(request, root);
}

esp_err_t em500_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/meters/em500/snapshot",
        .method = HTTP_GET,
        .handler = snapshot_get,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
