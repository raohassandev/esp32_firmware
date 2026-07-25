#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    NETWORK_WIFI_IDLE = 0,
    NETWORK_WIFI_SCANNING,
    NETWORK_WIFI_CONNECTING_PRIMARY,
    NETWORK_WIFI_CONNECTING_FALLBACK,
    NETWORK_WIFI_CONNECTED,
    NETWORK_WIFI_AP_FALLBACK,
    NETWORK_WIFI_DISCONNECTED
} network_wifi_state_t;

typedef struct {
    network_wifi_state_t state;
    bool network_ready;
    bool using_fallback_sta;
    bool fallback_ap_active;
    char ssid[33];
    char ip[16];
    char gateway[16];
    char netmask[16];
    int8_t rssi;
    uint32_t reconnect_count;
    uint32_t disconnect_count;
} network_status_t;

esp_err_t network_manager_init(void);
bool network_manager_is_connected(void);
bool network_manager_wait_ready(uint32_t timeout_ms);
const char *network_manager_get_ip(void);
void network_manager_get_status(network_status_t *out_status);
esp_err_t network_manager_rescan_and_connect(void);
