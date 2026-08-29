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

/* Live pages should not invalidate an LVGL label when its visible text did not
 * change.  The helpers below keep that policy in one place so steady refreshes
 * do not create avoidable layout/draw work on the RGB panel. */
bool screen_ui_set_text_if_changed(lv_obj_t *label, const char *text);
bool screen_ui_set_text_fmt_if_changed(lv_obj_t *label, const char *format, ...);

void screen_ui_set_kw(lv_obj_t *label, bool available, double value);
void screen_ui_set_state_text(lv_obj_t *label, const char *text, bool healthy);
const char *screen_ui_safe_text(const char *text, const char *fallback);

#ifdef __cplusplus
}
#endif
