#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PVDG_UI_WIFI_SSID_BYTES 33U
#define PVDG_UI_WIFI_PASSWORD_BYTES 65U
#define PVDG_UI_WIFI_IPV4_BYTES 16U

typedef enum {
    PVDG_UI_WIFI_IP_DHCP = 0,
    PVDG_UI_WIFI_IP_STATIC = 1
} pvdg_ui_wifi_ip_mode_t;

typedef struct {
    bool enabled;
    char ssid[PVDG_UI_WIFI_SSID_BYTES];
    char password[PVDG_UI_WIFI_PASSWORD_BYTES];
    bool clear_password;
    pvdg_ui_wifi_ip_mode_t ip_mode;
    char static_ip[PVDG_UI_WIFI_IPV4_BYTES];
    char gateway[PVDG_UI_WIFI_IPV4_BYTES];
    char netmask[PVDG_UI_WIFI_IPV4_BYTES];
    char dns1[PVDG_UI_WIFI_IPV4_BYTES];
    char dns2[PVDG_UI_WIFI_IPV4_BYTES];
} pvdg_ui_wifi_profile_config_t;

typedef struct {
    pvdg_ui_wifi_profile_config_t primary;
    pvdg_ui_wifi_profile_config_t fallback;
    bool scan_before_connect;
    bool fallback_ap_enabled;
    char fallback_ap_ssid[PVDG_UI_WIFI_SSID_BYTES];
    char fallback_ap_password[PVDG_UI_WIFI_PASSWORD_BYTES];
    uint8_t max_retries_per_profile;
    uint32_t reconnect_backoff_ms;
} pvdg_ui_wifi_config_t;

typedef enum {
    PVDG_UI_WIFI_CONFIG_OK = 0,
    PVDG_UI_WIFI_CONFIG_INVALID_ARGUMENT,
    PVDG_UI_WIFI_CONFIG_INVALID_BASELINE,
    PVDG_UI_WIFI_CONFIG_INVALID_SSID,
    PVDG_UI_WIFI_CONFIG_PASSWORD_REQUIRED,
    PVDG_UI_WIFI_CONFIG_INVALID_PASSWORD
} pvdg_ui_wifi_config_result_t;

pvdg_ui_wifi_config_result_t pvdg_ui_wifi_prepare_primary(
    const pvdg_ui_wifi_config_t *current,
    const char *ssid,
    const char *password,
    bool secure_network,
    pvdg_ui_wifi_config_t *out,
    bool *primary_changed,
    char *error,
    size_t error_size);

bool pvdg_ui_wifi_config_valid(const pvdg_ui_wifi_config_t *config,
                               char *error,
                               size_t error_size);

#ifdef __cplusplus
}
#endif
