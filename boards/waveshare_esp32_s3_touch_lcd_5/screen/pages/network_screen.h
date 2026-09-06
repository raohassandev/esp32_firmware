#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_SCREEN_MAX_RESULTS 16U
#define NETWORK_SCREEN_MESSAGE_MAX 160U
#define NETWORK_SCREEN_SSID_MAX 33U
#define NETWORK_SCREEN_IP_MAX 16U

typedef enum {
    NETWORK_SCREEN_AUTH_OK = 0,
    NETWORK_SCREEN_AUTH_DENIED,
    NETWORK_SCREEN_AUTH_LOCKED,
    NETWORK_SCREEN_AUTH_ERROR,
} network_screen_auth_result_t;

typedef struct {
    char ssid[NETWORK_SCREEN_SSID_MAX];
    int8_t rssi;
    bool secured;
    bool configured;
    bool connected;
} network_screen_ap_t;

typedef struct {
    bool scanning;
    uint16_t count;
    network_screen_ap_t items[NETWORK_SCREEN_MAX_RESULTS];
} network_screen_scan_t;

/* Mirrors network_status_t (components/network_manager) without exposing
 * that header to the screen layer -- the same decoupling already used for
 * commissioning and source-commissioning backends in this codebase. */
typedef struct {
    bool valid;
    bool network_online;
    bool fallback_ap_active;
    char ssid[NETWORK_SCREEN_SSID_MAX];
    char ip[NETWORK_SCREEN_IP_MAX];
    int8_t rssi;
} network_screen_status_t;

typedef struct {
    bool ok;
    bool restart_required;
    char message[NETWORK_SCREEN_MESSAGE_MAX];
} network_screen_action_result_t;

typedef struct {
    void *context;
    network_screen_auth_result_t (*unlock)(void *context,
                                           const char *credential,
                                           uint32_t *retry_after_ms,
                                           bool *setup_required);
    void (*lock)(void *context);
    bool (*read_status)(void *context, network_screen_status_t *out);
    bool (*request_scan)(void *context);
    bool (*read_scan)(void *context, network_screen_scan_t *out);
    bool (*connect)(void *context, const char *ssid, const char *password,
                    network_screen_action_result_t *result);
    bool (*restart_controller)(void *context, network_screen_action_result_t *result);
} network_screen_backend_t;

lv_obj_t *network_screen_create(lv_obj_t *parent);
void network_screen_set_backend(const network_screen_backend_t *backend);
void network_screen_apply_status(const network_screen_status_t *status);
void network_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
