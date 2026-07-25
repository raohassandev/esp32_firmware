#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "modbus_types.h"

#define APP_CONFIG_MAGIC 0x50564447u
#define APP_CONFIG_VERSION 2u
#define APP_MAX_METERS 4
#define APP_MAX_INVERTERS 12

typedef enum {
    APP_WIFI_IP_DHCP = 0,
    APP_WIFI_IP_STATIC = 1
} app_wifi_ip_mode_t;

typedef struct {
    bool enabled;
    char ssid[33];
    char password[65];
    app_wifi_ip_mode_t ip_mode;
    char static_ip[16];
    char gateway[16];
    char netmask[16];
    char dns1[16];
    char dns2[16];
} app_wifi_sta_profile_t;

typedef struct {
    app_wifi_sta_profile_t primary;
    app_wifi_sta_profile_t fallback;
    bool scan_before_connect;
    bool fallback_ap_enabled;
    char fallback_ap_ssid[33];
    char fallback_ap_password[65];
    uint8_t max_retries_per_profile;
    uint32_t reconnect_backoff_ms;
} app_wifi_config_t;

typedef struct {
    bool enabled;
    char name[24];
    modbus_endpoint_t endpoint;
    uint8_t function_code;
    uint16_t active_power_address;
    modbus_data_type_t active_power_type;
    modbus_word_order_t active_power_order;
    float active_power_scale;
    uint32_t poll_interval_ms;
} meter_config_t;

typedef struct {
    bool enabled;
    char name[24];
    modbus_endpoint_t endpoint;
    float rated_power_kw;
    uint16_t power_limit_address;
    uint8_t power_limit_function;
    float raw_units_per_percent;
    float minimum_percent;
    float maximum_percent;
} inverter_config_t;

typedef struct {
    bool enabled;
    float grid_import_target_kw;
    float deadband_kw;
    float kp;
    float ki;
    float ramp_up_percent_per_second;
    float ramp_down_percent_per_second;
    uint32_t interval_ms;
    uint32_t meter_stale_timeout_ms;
} control_config_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    char device_name[32];
    app_wifi_config_t wifi;
    uint8_t meter_count;
    meter_config_t meters[APP_MAX_METERS];
    uint8_t inverter_count;
    inverter_config_t inverters[APP_MAX_INVERTERS];
    control_config_t control;
} app_config_t;
