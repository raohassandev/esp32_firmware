#pragma once

#include "lvgl.h"
#include "pvdg_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Operator Overview content page. Parent is the shell's content area. */
lv_obj_t *pvdg_ui_overview_create(lv_obj_t *parent);
void pvdg_ui_overview_apply_model(const pvdg_ui_model_t *model);
void pvdg_ui_overview_show_unavailable(void);

#ifdef __cplusplus
}
#endif
