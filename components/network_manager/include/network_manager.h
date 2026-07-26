#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NETWORK_SCAN_MAX_RESULTS 24

typedef enum {
    NETWORK_WIFI_IDLE = 0,
    NETWORK_WIFI_SCANNING,
    NETWORK_WIFI_CONNECTING_PRIMARY,
    NETWORK_WIFI_CONNECTING_FALLBACK,
    NETWORK_WIFI_CONNECTED,
    NETWORK_WIFI_AP_FALLBACK,
    NETWORK_WIFI_DISCONNECTED
} network_wifi_state_t;

typedef enum {
    NETWORK_SCAN_IDLE = 0,
    NETWORK_SCAN_RUNNING,
    NETWORK_SCAN_COMPLETE,
    NETWORK_SCAN_FAILED
} network_scan_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth_mode;
    bool configured_primary;
    bool configured_fallback;
    bool connected;
} network_scan_ap_t;

typedef struct {
    network_scan_state_t state;
    esp_err_t last_error;
    uint32_t generation;
    uint32_t started_ms;
    uint32_t completed_ms;
    uint16_t count;
    network_scan_ap_t results[NETWORK_SCAN_MAX_RESULTS];
} network_scan_snapshot_t;

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

/* Two-phase operator reconnect API. Every successful begin call admits one HTTP
 * response handler and must be paired with exactly one complete call. */
esp_err_t network_manager_operator_reconnect_response_begin(bool *accepted);
void network_manager_operator_reconnect_response_complete(bool accepted, esp_err_t send_result);

/* Compatibility wrapper for non-HTTP callers. New HTTP code must use the
 * response-aware begin/complete pair above. */
esp_err_t network_manager_rescan_and_connect(void);

esp_err_t network_manager_request_scan(void);
void network_manager_get_scan_snapshot(network_scan_snapshot_t *out_snapshot);
