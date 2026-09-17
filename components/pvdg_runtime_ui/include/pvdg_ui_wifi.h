#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "pvdg_ui_model.h"
#include "pvdg_ui_wifi_config.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PVDG_UI_WIFI_MAX_NETWORKS 8U
typedef struct { char ssid[PVDG_UI_TEXT_MEDIUM]; int rssi; uint8_t channel; uint8_t auth_mode; char security[PVDG_UI_TEXT_SMALL]; bool connected; bool configured_primary; bool configured_fallback; bool secure; bool supported; } pvdg_ui_wifi_network_t;
typedef struct { bool scan_running; uint8_t count; pvdg_ui_wifi_network_t networks[PVDG_UI_WIFI_MAX_NETWORKS]; } pvdg_ui_wifi_scan_t;
typedef struct { void (*request_scan)(void *user); void (*request_reconnect)(void *user); void (*submit_config)(const pvdg_ui_wifi_config_t *config,bool primary_changed,void *user); void *user; } pvdg_ui_wifi_callbacks_t;
lv_obj_t *pvdg_ui_wifi_create(lv_obj_t *parent,const pvdg_ui_wifi_callbacks_t *callbacks);
void pvdg_ui_wifi_apply(const pvdg_ui_model_t *model,const pvdg_ui_wifi_scan_t *scan);
void pvdg_ui_wifi_set_config_snapshot(const pvdg_ui_wifi_config_t *config,bool available);
void pvdg_ui_wifi_set_action_state(bool busy,const char *message);
void pvdg_ui_wifi_set_write_authorized(bool authorized);
void pvdg_ui_wifi_clear_password_input(void);
#ifdef __cplusplus
}
#endif
