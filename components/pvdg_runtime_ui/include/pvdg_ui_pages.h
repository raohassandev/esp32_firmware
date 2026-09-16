#pragma once

#include "lvgl.h"
#include "pvdg_ui_em500_history.h"
#include "pvdg_ui_model.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *pvdg_ui_generator_create(lv_obj_t *parent);
void pvdg_ui_generator_apply_model(const pvdg_ui_model_t *model);
void pvdg_ui_generator_show_unavailable(void);

lv_obj_t *pvdg_ui_load_create(lv_obj_t *parent);
void pvdg_ui_load_apply_model(const pvdg_ui_model_t *model);
void pvdg_ui_load_show_unavailable(void);

lv_obj_t *pvdg_ui_reports_create(lv_obj_t *parent);
void pvdg_ui_reports_apply_model(const pvdg_ui_model_t *model);
void pvdg_ui_reports_apply_em500_statistics(const pvdg_ui_em500_statistics_t *statistics);
void pvdg_ui_reports_show_unavailable(void);

#ifdef __cplusplus
}
#endif
