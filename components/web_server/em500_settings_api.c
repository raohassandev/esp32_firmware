#include "em500_settings_api.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "config_manager.h"
#include "meter_manager.h"

#define SETTINGS_QUERY_MAX 192
#define SETTINGS_MAX_WORDS 80

typedef enum {
    SETTING_U16 = 0,
    SETTING_S16,
    SETTING_U32,
    SETTING_ASCII,
    SETTING_IPV4,
    SETTING_SENSITIVE_U16,
} setting_value_type_t;

typedef struct {
    const char *code;
    const char *key;
    uint16_t offset;
    uint8_t words;
    setting_value_type_t type;
    const char *minimum;
    const char *maximum;
    const char *default_value;
    const char *notes;
} setting_descriptor_t;

typedef struct {
    const char *menu;
    const char *name;
    uint16_t base;
    uint16_t channel_step;
    uint8_t word_count;
    uint8_t maximum_channel;
    const setting_descriptor_t *parameters;
    size_t parameter_count;
} menu_descriptor_t;

typedef struct {
    uint8_t meter_index;
    uint8_t function_code;
    uint8_t address_base;
    uint8_t channel;
    char menu[4];
} settings_request_t;

#define PARAM(code_, key_, offset_, words_, type_, min_, max_, def_, notes_) \
    {code_, key_, offset_, words_, type_, min_, max_, def_, notes_}

static const setting_descriptor_t s_m01[] = {
    PARAM("P01.01", "ct_primary_a", 0x00, 1, SETTING_U16, "1", "10000", "5", "CT primary current in amperes"),
    PARAM("P01.02", "ct_secondary", 0x02, 1, SETTING_U16, "0", "1", "1", "Documented enum: 0=1A, 1=5A"),
    PARAM("P01.03", "rated_voltage_v", 0x04, 2, SETTING_U32, "49", "500000", "49", "Rated system voltage"),
    PARAM("P01.04", "use_vt", 0x06, 1, SETTING_U16, "0", "1", "0", "Voltage transformer enable"),
    PARAM("P01.05", "vt_primary_v", 0x08, 2, SETTING_U32, "50", "500000", "100", "VT/PT primary voltage"),
    PARAM("P01.06", "vt_secondary_v", 0x0A, 1, SETTING_U16, "50", "500", "100", "VT/PT secondary voltage"),
    PARAM("P01.07", "wiring", 0x0C, 1, SETTING_U16, "0", "5", "0", "0=3P+N, 1=3P, 2=3P+N balanced, 3=3P balanced, 4=2P+N, 5=1P+N"),
};

static const setting_descriptor_t s_m02[] = {
    PARAM("P02.01", "language", 0x00, 1, SETTING_U16, "0", "4", "0", "Language enum"),
    PARAM("P02.02", "display_contrast", 0x02, 1, SETTING_U16, "0", "50", "100", "Clone may use a different effective range"),
    PARAM("P02.03", "backlight_high", 0x04, 1, SETTING_U16, "10", "100", "100", "Percent"),
    PARAM("P02.04", "backlight_low", 0x06, 1, SETTING_U16, "10", "100", "30", "Percent"),
    PARAM("P02.05", "backlight_low_delay_s", 0x08, 1, SETTING_U16, "5", "600", "30", "Seconds"),
    PARAM("P02.06", "default_page_return_s", 0x0A, 1, SETTING_U16, "9", "600", "60", "Seconds"),
    PARAM("P02.07", "default_page", 0x0C, 1, SETTING_U16, "1", "32", "1", "Page number"),
    PARAM("P02.08", "default_subpage", 0x0E, 1, SETTING_U16, "0", "13", "0", "Sub-page number"),
    PARAM("P02.09", "display_update_time", 0x10, 1, SETTING_U16, "1", "50", "5", "Display refresh parameter"),
};

static const setting_descriptor_t s_m03[] = {
    PARAM("P03.01", "passwords_enabled", 0x00, 1, SETTING_U16, "0", "1", "0", "Password enable only"),
    PARAM("P03.02", "user_password", 0x02, 1, SETTING_SENSITIVE_U16, "0", "9999", "masked", "Never returned by the API"),
    PARAM("P03.03", "advanced_password", 0x04, 1, SETTING_SENSITIVE_U16, "0", "9999", "masked", "Never returned by the API"),
};

static const setting_descriptor_t s_m04[] = {
    PARAM("P04.01", "integration_mode", 0x00, 1, SETTING_U16, "0", "3", "1", "Integration mode enum"),
    PARAM("P04.02", "power_integration_time_min", 0x02, 1, SETTING_U16, "1", "60", "15", "Minutes"),
    PARAM("P04.03", "current_integration_time_min", 0x04, 1, SETTING_U16, "1", "60", "15", "Minutes"),
    PARAM("P04.04", "voltage_integration_time_min", 0x06, 1, SETTING_U16, "1", "60", "1", "Minutes"),
    PARAM("P04.05", "frequency_integration_time_min", 0x08, 1, SETTING_U16, "1", "60", "1", "Minutes"),
};

static const setting_descriptor_t s_m05[] = {
    PARAM("P05.01", "hour_counters_enabled", 0x00, 1, SETTING_U16, "0", "1", "1", "General hour-counter enable"),
    PARAM("P05.02", "partial_hour_counter_mode", 0x02, 1, SETTING_U16, "0", "4", "1", "Partial counter source type"),
    PARAM("P05.03", "partial_hour_counter_channel", 0x04, 1, SETTING_U16, "1", "8", "1", "Source channel"),
};

static const setting_descriptor_t s_m06[] = {
    PARAM("P06.01", "trend_measure", 0x00, 1, SETTING_U16, "0", "3", "1", "Trend-page measurement"),
    PARAM("P06.02", "trend_autorange", 0x02, 1, SETTING_U16, "0", "1", "1", "Automatic scale"),
    PARAM("P06.03", "trend_full_scale", 0x04, 1, SETTING_U16, "0", "1000", "1000", "Full-scale raw value"),
    PARAM("P06.04", "trend_full_scale_multiplier", 0x06, 1, SETTING_U16, "0", "2", "0", "Multiplier enum"),
};

static const setting_descriptor_t s_m07[] = {
    PARAM("P07.n.01", "serial_node_address", 0x00, 1, SETTING_U16, "1", "255", "1", "Communication node address"),
    PARAM("P07.n.02", "serial_speed", 0x02, 1, SETTING_U16, "0", "5", "3", "Documented speed enum"),
    PARAM("P07.n.03", "data_format", 0x04, 1, SETTING_U16, "0", "4", "0", "Parity/data-bit enum"),
    PARAM("P07.n.04", "stop_bits", 0x06, 1, SETTING_U16, "0", "1", "0", "Documented enum; display as 1 or 2 stop bits after model verification"),
    PARAM("P07.n.05", "protocol", 0x08, 1, SETTING_U16, "0", "1", "0", "RTU/ASCII on DMG610 built-in RS485"),
    PARAM("P07.n.06", "ip_address", 0x0A, 2, SETTING_IPV4, "0.0.0.0", "255.255.255.255", "0.0.0.0", "Expansion Ethernet only"),
    PARAM("P07.n.07", "subnet_mask", 0x0C, 2, SETTING_IPV4, "0.0.0.0", "255.255.255.255", "0.0.0.0", "Expansion Ethernet only"),
    PARAM("P07.n.08", "ip_port", 0x0E, 1, SETTING_U16, "0", "9999", "1001", "Expansion Ethernet only"),
};

static const setting_descriptor_t s_m08[] = {
    PARAM("P08.n.01", "reference_measure", 0x00, 1, SETTING_U16, "0", "41", "0", "Measurement enum"),
    PARAM("P08.n.02", "function", 0x02, 1, SETTING_U16, "0", "2", "0", "Threshold function enum"),
    PARAM("P08.n.03", "upper_threshold", 0x04, 1, SETTING_S16, "-9999", "9999", "0", "Signed raw threshold"),
    PARAM("P08.n.04", "upper_multiplier", 0x06, 1, SETTING_U16, "0", "6", "2", "Decimal multiplier enum"),
    PARAM("P08.n.05", "upper_delay", 0x08, 1, SETTING_U16, "0", "6000", "0", "Delay"),
    PARAM("P08.n.06", "lower_threshold", 0x0A, 1, SETTING_S16, "-9999", "9999", "0", "Signed raw threshold"),
    PARAM("P08.n.07", "lower_multiplier", 0x0C, 1, SETTING_U16, "0", "6", "2", "Decimal multiplier enum"),
    PARAM("P08.n.08", "lower_delay", 0x0E, 1, SETTING_U16, "0", "6000", "0", "Delay"),
    PARAM("P08.n.09", "normal_status", 0x10, 1, SETTING_U16, "0", "1", "0", "Normal/inverted state"),
    PARAM("P08.n.10", "latch", 0x12, 1, SETTING_U16, "0", "1", "0", "Latched threshold"),
};

static const setting_descriptor_t s_m09[] = {
    PARAM("P09.n.01", "alarm_source", 0x00, 1, SETTING_U16, "0", "3", "0", "Alarm-source enum"),
    PARAM("P09.n.02", "channel", 0x02, 1, SETTING_U16, "1", "8", "1", "Source channel"),
    PARAM("P09.n.03", "latch", 0x04, 1, SETTING_U16, "0", "1", "0", "Latched alarm"),
    PARAM("P09.n.04", "priority", 0x06, 1, SETTING_U16, "0", "1", "0", "Alarm priority"),
    PARAM("P09.n.05", "text", 0x08, 8, SETTING_ASCII, "", "8 words", "ALAn", "Alarm text"),
};

static const setting_descriptor_t s_m10[] = {
    PARAM("P10.n.01", "counter_source", 0x00, 1, SETTING_U16, "0", "4", "0", "Counter-source enum"),
    PARAM("P10.n.02", "channel", 0x02, 1, SETTING_U16, "1", "8", "1", "Source channel"),
    PARAM("P10.n.03", "multiplier", 0x04, 1, SETTING_U16, "1", "1000", "1", "Counter multiplier"),
    PARAM("P10.n.04", "divider", 0x06, 1, SETTING_U16, "1", "1000", "1", "Counter divider"),
    PARAM("P10.n.05", "description", 0x08, 8, SETTING_ASCII, "", "8 words", "CNTn", "Counter description"),
    PARAM("P10.n.06", "unit", 0x10, 3, SETTING_ASCII, "", "3 words", "UMn", "Unit text"),
};

static const setting_descriptor_t s_m11[] = {
    PARAM("P11.n.01", "source_measurement", 0x00, 1, SETTING_U16, "0", "5", "0", "Energy source enum"),
    PARAM("P11.n.02", "count_unit", 0x02, 1, SETTING_U16, "0", "3", "1", "Pulse count unit"),
    PARAM("P11.n.03", "pulse_duration_ms", 0x04, 1, SETTING_U16, "10", "1000", "100", "Milliseconds"),
};

static const setting_descriptor_t s_m12[] = {
    PARAM("P12.n.01", "operand_1", 0x00, 1, SETTING_U16, "0", "5", "0", "Boolean operand"),
    PARAM("P12.n.02", "channel_1", 0x02, 1, SETTING_U16, "1", "8", "1", "Operand channel"),
    PARAM("P12.n.03", "operator_1", 0x04, 1, SETTING_U16, "0", "6", "0", "Logic operator"),
    PARAM("P12.n.04", "operand_2", 0x06, 1, SETTING_U16, "0", "5", "0", "Boolean operand"),
    PARAM("P12.n.05", "channel_2", 0x08, 1, SETTING_U16, "1", "8", "1", "Operand channel"),
    PARAM("P12.n.06", "operator_2", 0x0A, 1, SETTING_U16, "0", "6", "0", "Logic operator"),
    PARAM("P12.n.07", "operand_3", 0x0C, 1, SETTING_U16, "0", "5", "0", "Boolean operand"),
    PARAM("P12.n.08", "channel_3", 0x0E, 1, SETTING_U16, "1", "8", "1", "Operand channel"),
    PARAM("P12.n.09", "operator_3", 0x10, 1, SETTING_U16, "0", "6", "0", "Logic operator"),
    PARAM("P12.n.10", "operand_4", 0x12, 1, SETTING_U16, "0", "5", "0", "Boolean operand"),
    PARAM("P12.n.11", "channel_4", 0x14, 1, SETTING_U16, "1", "8", "1", "Operand channel"),
};

static const setting_descriptor_t s_m13[] = {
    PARAM("P13.n.01", "input_function", 0x00, 1, SETTING_U16, "0", "5", "0", "Input function enum"),
    PARAM("P13.n.02", "normal_status", 0x02, 1, SETTING_U16, "0", "1", "0", "Normal input state"),
    PARAM("P13.n.03", "on_delay", 0x04, 2, SETTING_U32, "0", "60000", "5", "Delay"),
    PARAM("P13.n.04", "off_delay", 0x06, 2, SETTING_U32, "0", "60000", "5", "Delay"),
};

static const setting_descriptor_t s_m14[] = {
    PARAM("P14.n.01", "output_function", 0x00, 1, SETTING_U16, "0", "7", "0", "Output function enum"),
    PARAM("P14.n.02", "channel", 0x02, 1, SETTING_U16, "1", "8", "1", "Function channel"),
    PARAM("P14.n.03", "idle_status", 0x04, 1, SETTING_U16, "0", "1", "0", "Idle output state"),
};

static const setting_descriptor_t s_m15[] = {
    PARAM("P15.n.01", "enabled", 0x00, 1, SETTING_U16, "0", "1", "0", "User-page enable"),
    PARAM("P15.n.02", "title", 0x02, 8, SETTING_ASCII, "", "8 words", "PAGn", "Page title"),
    PARAM("P15.n.03", "measurement_1", 0x0A, 1, SETTING_U16, "0", "47", "0", "Measurement enum"),
    PARAM("P15.n.04", "measurement_2", 0x0C, 1, SETTING_U16, "0", "47", "0", "Measurement enum"),
    PARAM("P15.n.05", "measurement_3", 0x0E, 1, SETTING_U16, "0", "47", "0", "Measurement enum"),
    PARAM("P15.n.06", "measurement_4", 0x10, 1, SETTING_U16, "0", "47", "0", "Measurement enum"),
};

static const setting_descriptor_t s_m16[] = {
    PARAM("P16.n.01", "input_type", 0x00, 1, SETTING_U16, "0", "5", "0", "Analog-input type"),
    PARAM("P16.n.02", "scale_start", 0x02, 1, SETTING_S16, "-9999", "9999", "0", "Signed raw value"),
    PARAM("P16.n.03", "scale_start_multiplier", 0x04, 1, SETTING_U16, "0", "6", "2", "Decimal multiplier enum"),
    PARAM("P16.n.04", "scale_end", 0x06, 1, SETTING_S16, "-9999", "9999", "0", "Signed raw value"),
    PARAM("P16.n.05", "scale_end_multiplier", 0x08, 1, SETTING_U16, "0", "6", "2", "Decimal multiplier enum"),
    PARAM("P16.n.06", "description", 0x0A, 8, SETTING_ASCII, "", "8 words", "AINn", "Analog-input description"),
    PARAM("P16.n.07", "unit", 0x12, 3, SETTING_ASCII, "", "3 words", "UMn", "Unit text"),
};

static const setting_descriptor_t s_m17[] = {
    PARAM("P17.n.01", "output_type", 0x00, 1, SETTING_U16, "0", "4", "0", "Analog-output type"),
    PARAM("P17.n.02", "reference_measure", 0x02, 1, SETTING_U16, "0", "47", "0", "Measurement enum"),
    PARAM("P17.n.03", "scale_start", 0x04, 1, SETTING_S16, "-9999", "9999", "0", "Signed raw value"),
    PARAM("P17.n.04", "scale_start_multiplier", 0x06, 1, SETTING_U16, "0", "6", "2", "Decimal multiplier enum"),
    PARAM("P17.n.05", "scale_end", 0x08, 1, SETTING_S16, "-9999", "9999", "0", "Signed raw value"),
    PARAM("P17.n.06", "scale_end_multiplier", 0x0A, 1, SETTING_U16, "0", "6", "2", "Decimal multiplier enum"),
};

static const setting_descriptor_t s_m18[] = {
    PARAM("P18.01", "enabled", 0x00, 1, SETTING_U16, "0", "1", "0", "Energy-quality enable"),
    PARAM("P18.02", "avg_voltage_nlo", 0x02, 1, SETTING_U16, "49", "100", "85.0", "Documented percent threshold; clone scaling must be verified"),
    PARAM("P18.03", "avg_voltage_lo", 0x04, 1, SETTING_U16, "49", "100", "90.0", "Documented percent threshold"),
    PARAM("P18.04", "avg_voltage_hi", 0x06, 1, SETTING_U16, "99", "150", "110.0", "Documented percent threshold"),
    PARAM("P18.05", "avg_voltage_nhi", 0x08, 1, SETTING_U16, "99", "150", "115.0", "Documented percent threshold"),
    PARAM("P18.06", "harmonic_control_mode", 0x0A, 1, SETTING_U16, "0", "2", "HARM", "Harmonic-control mode"),
    PARAM("P18.07", "thd_threshold", 0x0C, 1, SETTING_U16, "1", "50", "8", "Percent"),
    PARAM("P18.08", "asymmetry_threshold", 0x0E, 1, SETTING_U16, "0", "50", "2.0", "Percent; clone scaling must be verified"),
    PARAM("P18.09", "avg_frequency_nlo", 0x10, 1, SETTING_U16, "79", "100", "94.0", "Percent of nominal frequency"),
    PARAM("P18.10", "avg_frequency_lo", 0x12, 1, SETTING_U16, "79", "100", "99.0", "Percent of nominal frequency"),
    PARAM("P18.11", "avg_frequency_hi", 0x14, 1, SETTING_U16, "99", "120", "101.0", "Percent of nominal frequency"),
    PARAM("P18.12", "avg_frequency_nhi", 0x16, 1, SETTING_U16, "99", "120", "104.0", "Percent of nominal frequency"),
    PARAM("P18.13", "dip_threshold", 0x18, 1, SETTING_U16, "4", "100", "90.0", "Percent; clone scaling must be verified"),
    PARAM("P18.14", "swell_threshold", 0x1A, 1, SETTING_U16, "99", "150", "110.0", "Percent; clone scaling must be verified"),
    PARAM("P18.15", "dip_swell_hysteresis", 0x1C, 1, SETTING_U16, "0", "10", "2.0", "Percent; clone scaling must be verified"),
    PARAM("P18.16", "capture_dip_swell", 0x1E, 1, SETTING_U16, "0", "1", "0", "Waveform-capture enable"),
    PARAM("P18.17", "interruption_threshold", 0x20, 1, SETTING_U16, "0", "10", "5.0", "Percent; clone scaling must be verified"),
    PARAM("P18.18", "interruption_hysteresis", 0x22, 1, SETTING_U16, "0", "10", "1.0", "Percent; clone scaling must be verified"),
    PARAM("P18.19", "capture_interruption", 0x24, 1, SETTING_U16, "0", "1", "0", "Waveform-capture enable"),
};

#define MENU(menu_, name_, base_, step_, words_, max_channel_, params_) \
    {menu_, name_, base_, step_, words_, max_channel_, params_, sizeof(params_) / sizeof((params_)[0])}

static const menu_descriptor_t s_menus[] = {
    MENU("M01", "General", 0x5000, 0, 14, 1, s_m01),
    MENU("M02", "Utility", 0x5080, 0, 17, 1, s_m02),
    MENU("M03", "Password", 0x5100, 0, 5, 1, s_m03),
    MENU("M04", "Integration", 0x5180, 0, 9, 1, s_m04),
    MENU("M05", "Hour counters", 0x5200, 0, 5, 1, s_m05),
    MENU("M06", "Trend graph", 0x5280, 0, 7, 1, s_m06),
    MENU("M07", "Communication", 0x5300, 0x80, 16, 2, s_m07),
    MENU("M08", "Limit thresholds", 0x5400, 0x80, 20, 8, s_m08),
    MENU("M09", "Alarms", 0x5800, 0x80, 16, 8, s_m09),
    MENU("M10", "Counters", 0x5C00, 0x80, 19, 8, s_m10),
    MENU("M11", "Energy pulse", 0x5E00, 0x80, 5, 8, s_m11),
    MENU("M12", "Boolean logic", 0x6080, 0x80, 21, 8, s_m12),
    MENU("M13", "Inputs", 0x6480, 0x80, 8, 8, s_m13),
    MENU("M14", "Outputs", 0x6880, 0x80, 5, 8, s_m14),
    MENU("M15", "User pages", 0x6C80, 0x80, 17, 8, s_m15),
    MENU("M16", "Analog inputs", 0x6E80, 0x40, 21, 8, s_m16),
    MENU("M17", "Analog outputs", 0x7080, 0x40, 11, 8, s_m17),
    MENU("M18", "Energy quality", 0x6B40, 0, 37, 1, s_m18),
};

static uint16_t pdu_address(uint16_t table_address, uint8_t address_base)
{
    return table_address >= address_base
               ? (uint16_t)(table_address - address_base)
               : table_address;
}

static const char *value_type_name(setting_value_type_t type)
{
    switch (type) {
    case SETTING_U16: return "u16";
    case SETTING_S16: return "s16";
    case SETTING_U32: return "u32";
    case SETTING_ASCII: return "ascii";
    case SETTING_IPV4: return "ipv4";
    case SETTING_SENSITIVE_U16: return "sensitive_u16";
    default: return "unknown";
    }
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

static bool parse_request(httpd_req_t *request, settings_request_t *options)
{
    *options = (settings_request_t){
        .meter_index = 0,
        .function_code = 4,
        .address_base = 0,
        .channel = 1,
    };
    strlcpy(options->menu, "M01", sizeof(options->menu));

    size_t length = httpd_req_get_url_query_len(request);
    if (!length) return true;
    if (length >= SETTINGS_QUERY_MAX) return false;

    char query[SETTINGS_QUERY_MAX];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) return false;

    char value[24];
    if (httpd_query_key_value(query, "index", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 0, APP_MAX_METERS - 1, &options->meter_index)) return false;
    if (httpd_query_key_value(query, "function", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 3, 4, &options->function_code)) return false;
    if (httpd_query_key_value(query, "address_base", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 0, 1, &options->address_base)) return false;
    if (httpd_query_key_value(query, "channel", value, sizeof(value)) == ESP_OK &&
        !parse_u8(value, 1, 8, &options->channel)) return false;
    if (httpd_query_key_value(query, "menu", value, sizeof(value)) == ESP_OK) {
        if (strlen(value) != 3 || value[0] != 'M' || !isdigit((unsigned char)value[1]) ||
            !isdigit((unsigned char)value[2])) return false;
        strlcpy(options->menu, value, sizeof(options->menu));
    }
    return true;
}

static const menu_descriptor_t *find_menu(const char *menu)
{
    for (size_t index = 0; index < sizeof(s_menus) / sizeof(s_menus[0]); ++index) {
        if (strcmp(s_menus[index].menu, menu) == 0) return &s_menus[index];
    }
    return NULL;
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

static uint32_t combine_u32(const uint16_t *registers)
{
    return ((uint32_t)registers[0] << 16) | registers[1];
}

static void add_raw_words(cJSON *object, const uint16_t *registers, uint8_t words)
{
    cJSON *array = cJSON_AddArrayToObject(object, "raw_words");
    for (uint8_t index = 0; index < words; ++index) {
        cJSON_AddItemToArray(array, cJSON_CreateNumber(registers[index]));
    }
}

static void decode_ascii(const uint16_t *registers, uint8_t words,
                         char *output, size_t output_size)
{
    size_t offset = 0;
    for (uint8_t index = 0; index < words && offset + 1 < output_size; ++index) {
        uint8_t high = (uint8_t)(registers[index] >> 8);
        uint8_t low = (uint8_t)registers[index];
        if (high && isprint(high) && offset + 1 < output_size) output[offset++] = (char)high;
        if (low && isprint(low) && offset + 1 < output_size) output[offset++] = (char)low;
    }
    while (offset && output[offset - 1] == ' ') offset--;
    output[offset] = '\0';
}

static void add_value(cJSON *object, const setting_descriptor_t *descriptor,
                      const uint16_t *registers)
{
    if (descriptor->type == SETTING_SENSITIVE_U16) {
        cJSON_AddBoolToObject(object, "sensitive", true);
        cJSON_AddBoolToObject(object, "masked", true);
        cJSON_AddNullToObject(object, "value");
        return;
    }

    add_raw_words(object, registers, descriptor->words);
    switch (descriptor->type) {
    case SETTING_U16:
        cJSON_AddNumberToObject(object, "value", registers[0]);
        break;
    case SETTING_S16:
        cJSON_AddNumberToObject(object, "value", (int16_t)registers[0]);
        break;
    case SETTING_U32:
        cJSON_AddNumberToObject(object, "value", combine_u32(registers));
        break;
    case SETTING_ASCII: {
        char text[40];
        decode_ascii(registers, descriptor->words, text, sizeof(text));
        cJSON_AddStringToObject(object, "value", text);
        break;
    }
    case SETTING_IPV4: {
        uint8_t bytes[4] = {
            (uint8_t)(registers[0] >> 8), (uint8_t)registers[0],
            (uint8_t)(registers[1] >> 8), (uint8_t)registers[1]
        };
        char address[16];
        snprintf(address, sizeof(address), "%u.%u.%u.%u",
                 bytes[0], bytes[1], bytes[2], bytes[3]);
        cJSON_AddStringToObject(object, "value", address);
        break;
    }
    default:
        cJSON_AddNullToObject(object, "value");
        break;
    }
}

static esp_err_t settings_get(httpd_req_t *request)
{
    settings_request_t options;
    if (!parse_request(request, &options)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Use index=0..3, function=3|4, address_base=0|1, menu=M01..M18 and channel=1..8");
    }
    if (options.meter_index >= meter_manager_get_count()) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                                   "Requested meter index is not configured");
    }

    const menu_descriptor_t *menu = find_menu(options.menu);
    if (!menu) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND,
                                   "Unknown setup menu");
    }
    if (options.channel > menu->maximum_channel) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Channel is outside the documented menu range");
    }
    if (menu->word_count > SETTINGS_MAX_WORDS) return httpd_resp_send_500(request);

    uint16_t table_base = (uint16_t)(menu->base +
                           (uint16_t)(options.channel - 1U) * menu->channel_step);
    uint16_t registers[SETTINGS_MAX_WORDS] = {0};
    esp_err_t read_error = meter_manager_read_registers(
        options.meter_index, options.function_code,
        pdu_address(table_base, options.address_base),
        menu->word_count, registers);

    cJSON *root = cJSON_CreateObject();
    if (!root) return httpd_resp_send_500(request);
    cJSON_AddStringToObject(root, "profile", "EM500_DMG610_COMPATIBLE");
    cJSON_AddBoolToObject(root, "read_only", true);
    cJSON_AddBoolToObject(root, "writes_enabled", false);
    cJSON_AddStringToObject(root, "write_gate",
                            "model_fingerprint_readback_rollback_and_service_authorization_required");
    cJSON_AddNumberToObject(root, "meter_index", options.meter_index);
    cJSON_AddNumberToObject(root, "function", options.function_code);
    cJSON_AddNumberToObject(root, "address_base", options.address_base);
    cJSON_AddStringToObject(root, "menu", menu->menu);
    cJSON_AddStringToObject(root, "menu_name", menu->name);
    cJSON_AddNumberToObject(root, "channel", options.channel);
    cJSON_AddNumberToObject(root, "table_base", table_base);
    cJSON_AddNumberToObject(root, "pdu_base",
                            pdu_address(table_base, options.address_base));
    cJSON_AddNumberToObject(root, "read_words", menu->word_count);

    if (read_error != ESP_OK) {
        cJSON_AddBoolToObject(root, "available", false);
        cJSON_AddStringToObject(root, "error", esp_err_to_name(read_error));
        return send_json(request, root);
    }

    cJSON_AddBoolToObject(root, "available", true);
    cJSON *parameters = cJSON_AddArrayToObject(root, "parameters");
    for (size_t index = 0; index < menu->parameter_count; ++index) {
        const setting_descriptor_t *descriptor = &menu->parameters[index];
        cJSON *object = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "code", descriptor->code);
        cJSON_AddStringToObject(object, "key", descriptor->key);
        cJSON_AddStringToObject(object, "type", value_type_name(descriptor->type));
        cJSON_AddNumberToObject(object, "table_address",
                                (uint16_t)(table_base + descriptor->offset));
        cJSON_AddNumberToObject(object, "pdu_address",
                                pdu_address((uint16_t)(table_base + descriptor->offset),
                                            options.address_base));
        cJSON_AddNumberToObject(object, "words", descriptor->words);
        cJSON_AddStringToObject(object, "documented_min", descriptor->minimum);
        cJSON_AddStringToObject(object, "documented_max", descriptor->maximum);
        cJSON_AddStringToObject(object, "documented_default", descriptor->default_value);
        if (descriptor->notes && descriptor->notes[0]) {
            cJSON_AddStringToObject(object, "notes", descriptor->notes);
        }

        if ((uint32_t)descriptor->offset + descriptor->words <= menu->word_count) {
            add_value(object, descriptor, &registers[descriptor->offset]);
        } else {
            cJSON_AddBoolToObject(object, "available", false);
            cJSON_AddNullToObject(object, "value");
        }
        cJSON_AddItemToArray(parameters, object);
    }

    cJSON *maintenance = cJSON_AddObjectToObject(root, "maintenance_policy");
    cJSON_AddBoolToObject(maintenance, "reset_energy_exposed", false);
    cJSON_AddBoolToObject(maintenance, "restore_defaults_exposed", false);
    cJSON_AddBoolToObject(maintenance, "reboot_exposed", false);
    cJSON_AddBoolToObject(maintenance, "wiring_test_exposed", false);
    cJSON_AddBoolToObject(maintenance, "tariff_write_exposed", false);

    return send_json(request, root);
}

esp_err_t em500_settings_api_register(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t endpoint = {
        .uri = "/api/meters/em500/settings",
        .method = HTTP_GET,
        .handler = settings_get,
    };
    return httpd_register_uri_handler(server, &endpoint);
}
