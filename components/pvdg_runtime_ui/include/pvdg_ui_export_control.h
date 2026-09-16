#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "pvdg_ui_source_config.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { bool config_available; bool write_allowed; bool busy; bool restart_required; const char *message; } pvdg_ui_export_control_state_t;
typedef struct { void (*request_config)(void *user); void (*submit_config)(const pvdg_ui_source_config_t *config, void *user); void *user; } pvdg_ui_export_control_callbacks_t;
lv_obj_t *pvdg_ui_export_control_create(lv_obj_t *parent,const pvdg_ui_export_control_callbacks_t *callbacks); void pvdg_ui_export_control_set_config(const pvdg_ui_source_config_t *config); void pvdg_ui_export_control_apply_state(const pvdg_ui_export_control_state_t *state);
#ifdef __cplusplus
}
#endif
