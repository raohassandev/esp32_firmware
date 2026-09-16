#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Approved dark industrial palette for the 800x480 native HMI. */
#define PVDG_UI_COLOR_BG             0x061421u
#define PVDG_UI_COLOR_BG_DEEP        0x04101Au
#define PVDG_UI_COLOR_TOPBAR         0x0B2235u
#define PVDG_UI_COLOR_SIDEBAR        0x091D2Du
#define PVDG_UI_COLOR_SURFACE        0x0B2031u
#define PVDG_UI_COLOR_SURFACE_RAISED 0x0D2639u
#define PVDG_UI_COLOR_BORDER         0x183A52u
#define PVDG_UI_COLOR_TEXT           0xEEF6FFu
#define PVDG_UI_COLOR_MUTED          0x8FB1CCu
#define PVDG_UI_COLOR_GRID           0x20A8FFu
#define PVDG_UI_COLOR_SOLAR          0xFFC928u
#define PVDG_UI_COLOR_GENERATOR      0xFF9938u
#define PVDG_UI_COLOR_LOAD           0x36E27Au
#define PVDG_UI_COLOR_COMM           0x26D8F2u
#define PVDG_UI_COLOR_SUCCESS        0x36E27Au
#define PVDG_UI_COLOR_WARNING        0xFFC857u
#define PVDG_UI_COLOR_DANGER         0xFF646Cu
#define PVDG_UI_COLOR_INACTIVE       0x5D7890u

#define PVDG_UI_TOPBAR_H   44
#define PVDG_UI_SIDEBAR_W  82
#define PVDG_UI_GAP_XS      4
#define PVDG_UI_GAP_SM      6
#define PVDG_UI_GAP_MD      8
#define PVDG_UI_GAP_LG     12
#define PVDG_UI_RADIUS      8
#define PVDG_UI_TOUCH_MIN  44

/* Existing Waveshare firmware enables Montserrat 14/20/24. Keep the native
 * component inside that proven font footprint until hardware memory evidence
 * supports adding more font sizes. */
#define PVDG_UI_FONT_BODY  (&lv_font_montserrat_14)
#define PVDG_UI_FONT_TITLE (&lv_font_montserrat_20)
#define PVDG_UI_FONT_HERO  (&lv_font_montserrat_24)

void pvdg_ui_theme_init(void);
void pvdg_ui_style_root(lv_obj_t *obj);
void pvdg_ui_style_surface(lv_obj_t *obj, bool raised);
void pvdg_ui_style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font);
lv_obj_t *pvdg_ui_make_card(lv_obj_t *parent);
lv_obj_t *pvdg_ui_make_title(lv_obj_t *parent, const char *text);
lv_obj_t *pvdg_ui_make_muted(lv_obj_t *parent, const char *text);
lv_obj_t *pvdg_ui_make_badge(lv_obj_t *parent, const char *text, lv_color_t color);
lv_obj_t *pvdg_ui_make_metric_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out);
void pvdg_ui_badge_set(lv_obj_t *badge, const char *text, lv_color_t color);
void pvdg_ui_label_set_if_changed(lv_obj_t *label, const char *text);

#ifdef __cplusplus
}
#endif
