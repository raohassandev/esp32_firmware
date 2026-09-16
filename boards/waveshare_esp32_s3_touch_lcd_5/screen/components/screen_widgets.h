#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_ui_panel(lv_obj_t *parent);
lv_obj_t *screen_ui_title(lv_obj_t *parent, const char *text);
lv_obj_t *screen_ui_muted_label(lv_obj_t *parent, const char *text);
lv_obj_t *screen_ui_value_label(lv_obj_t *parent, const char *text);
lv_obj_t *screen_ui_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out);
void screen_ui_set_kw(lv_obj_t *label, bool available, double value);
void screen_ui_set_state_text(lv_obj_t *label, const char *text, bool healthy);
const char *screen_ui_safe_text(const char *text, const char *fallback);

#ifdef __cplusplus
}
#endif
