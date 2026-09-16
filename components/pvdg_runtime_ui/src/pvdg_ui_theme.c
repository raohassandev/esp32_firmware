#include "pvdg_ui_theme.h"

#include <string.h>

static lv_style_t s_root_style;
static lv_style_t s_surface_style;
static lv_style_t s_raised_style;
static bool s_ready;

void pvdg_ui_theme_init(void)
{
    if (s_ready) return;

    lv_style_init(&s_root_style);
    lv_style_set_bg_color(&s_root_style, lv_color_hex(PVDG_UI_COLOR_BG));
    lv_style_set_bg_opa(&s_root_style, LV_OPA_COVER);
    lv_style_set_border_width(&s_root_style, 0);
    lv_style_set_radius(&s_root_style, 0);
    lv_style_set_pad_all(&s_root_style, 0);
    lv_style_set_text_color(&s_root_style, lv_color_hex(PVDG_UI_COLOR_TEXT));
    lv_style_set_text_font(&s_root_style, PVDG_UI_FONT_BODY);

    lv_style_init(&s_surface_style);
    lv_style_set_bg_color(&s_surface_style, lv_color_hex(PVDG_UI_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_surface_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_surface_style, lv_color_hex(PVDG_UI_COLOR_BORDER));
    lv_style_set_border_width(&s_surface_style, 1);
    lv_style_set_radius(&s_surface_style, PVDG_UI_RADIUS);
    lv_style_set_pad_all(&s_surface_style, PVDG_UI_GAP_MD);
    lv_style_set_text_color(&s_surface_style, lv_color_hex(PVDG_UI_COLOR_TEXT));
    lv_style_set_text_font(&s_surface_style, PVDG_UI_FONT_BODY);

    lv_style_init(&s_raised_style);
    lv_style_set_bg_color(&s_raised_style, lv_color_hex(PVDG_UI_COLOR_SURFACE_RAISED));
    lv_style_set_bg_opa(&s_raised_style, LV_OPA_COVER);
    lv_style_set_border_color(&s_raised_style, lv_color_hex(PVDG_UI_COLOR_BORDER));
    lv_style_set_border_width(&s_raised_style, 1);
    lv_style_set_radius(&s_raised_style, PVDG_UI_RADIUS);
    lv_style_set_pad_all(&s_raised_style, PVDG_UI_GAP_MD);
    lv_style_set_text_color(&s_raised_style, lv_color_hex(PVDG_UI_COLOR_TEXT));
    lv_style_set_text_font(&s_raised_style, PVDG_UI_FONT_BODY);

    s_ready = true;
}

void pvdg_ui_style_root(lv_obj_t *obj)
{
    if (!obj) return;
    pvdg_ui_theme_init();
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &s_root_style, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void pvdg_ui_style_surface(lv_obj_t *obj, bool raised)
{
    if (!obj) return;
    pvdg_ui_theme_init();
    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, raised ? &s_raised_style : &s_surface_style, LV_PART_MAIN);
}

void pvdg_ui_style_label(lv_obj_t *label, lv_color_t color, const lv_font_t *font)
{
    if (!label) return;
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font ? font : PVDG_UI_FONT_BODY, LV_PART_MAIN);
}

lv_obj_t *pvdg_ui_make_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    if (!card) return NULL;
    pvdg_ui_style_surface(card, true);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    return card;
}

lv_obj_t *pvdg_ui_make_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return NULL;
    lv_label_set_text(label, text ? text : "");
    pvdg_ui_style_label(label, lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    return label;
}

lv_obj_t *pvdg_ui_make_muted(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return NULL;
    lv_label_set_text(label, text ? text : "");
    pvdg_ui_style_label(label, lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    return label;
}

lv_obj_t *pvdg_ui_make_badge(lv_obj_t *parent, const char *text, lv_color_t color)
{
    lv_obj_t *badge = lv_label_create(parent);
    if (!badge) return NULL;
    lv_label_set_text(badge, text ? text : "");
    lv_obj_set_style_text_color(badge, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_mix(color, lv_color_hex(PVDG_UI_COLOR_SURFACE), LV_OPA_30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, color, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(badge, 999, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(badge, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(badge, 3, LV_PART_MAIN);
    lv_obj_set_style_text_font(badge, PVDG_UI_FONT_BODY, LV_PART_MAIN);
    return badge;
}

lv_obj_t *pvdg_ui_make_metric_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    if (!row) return NULL;
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 27);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *key = lv_label_create(row);
    lv_label_set_text(key, name ? name : "");
    pvdg_ui_style_label(key, lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);

    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "--");
    pvdg_ui_style_label(value, lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_BODY);
    if (value_out) *value_out = value;
    return row;
}

void pvdg_ui_badge_set(lv_obj_t *badge, const char *text, lv_color_t color)
{
    if (!badge) return;
    pvdg_ui_label_set_if_changed(badge, text ? text : "");
    lv_obj_set_style_text_color(badge, color, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_mix(color, lv_color_hex(PVDG_UI_COLOR_SURFACE), LV_OPA_30), LV_PART_MAIN);
}

void pvdg_ui_label_set_if_changed(lv_obj_t *label, const char *text)
{
    if (!label) return;
    const char *next = text ? text : "";
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, next) != 0) lv_label_set_text(label, next);
}
