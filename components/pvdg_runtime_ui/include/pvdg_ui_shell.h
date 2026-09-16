#pragma once

#include "lvgl.h"
#include "pvdg_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PVDG_UI_ROUTE_OVERVIEW = 0,
    PVDG_UI_ROUTE_GRID,
    PVDG_UI_ROUTE_SOLAR,
    PVDG_UI_ROUTE_GENERATOR,
    PVDG_UI_ROUTE_LOAD,
    PVDG_UI_ROUTE_REPORTS,
    PVDG_UI_ROUTE_WIFI,
    PVDG_UI_ROUTE_ENGINEERING
} pvdg_ui_route_t;

typedef void (*pvdg_ui_route_cb_t)(pvdg_ui_route_t route, void *user);
typedef struct { pvdg_ui_route_cb_t on_route; void *user; } pvdg_ui_shell_callbacks_t;
typedef struct { lv_obj_t *root; lv_obj_t *content; } pvdg_ui_shell_t;

pvdg_ui_shell_t pvdg_ui_shell_create(lv_obj_t *parent,const pvdg_ui_shell_callbacks_t *callbacks);
void pvdg_ui_shell_apply_model(const pvdg_ui_model_t *model);
void pvdg_ui_shell_set_clock(const char *time_text,const char *date_text);
void pvdg_ui_shell_set_active_route(pvdg_ui_route_t route);

#ifdef __cplusplus
}
#endif
