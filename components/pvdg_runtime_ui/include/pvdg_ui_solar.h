#pragma once

#include "lvgl.h"
#include "pvdg_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *pvdg_ui_solar_create(lv_obj_t *parent);
void pvdg_ui_solar_apply_model(const pvdg_ui_model_t *model);
void pvdg_ui_solar_show_unavailable(void);

#ifdef __cplusplus
}
#endif
