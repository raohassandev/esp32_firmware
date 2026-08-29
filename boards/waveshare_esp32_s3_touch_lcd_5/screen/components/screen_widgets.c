#include "screen_widgets.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint32_t PANEL = 0x151B24;
static const uint32_t BORDER = 0x2E3948;
static const uint32_t TEXT = 0xF2F6FA;
static const uint32_t MUTED = 0x9EADBF;
static const uint32_t GOOD = 0x62D28F;
static const uint32_t WARN = 0xF2B84B;

lv_obj_t *screen_ui_panel(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(PANEL), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(TEXT), LV_PART_MAIN);
    return obj;
}

lv_obj_t *screen_ui_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(TEXT), LV_PART_MAIN);
    return label;
}

lv_obj_t *screen_ui_muted_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(MUTED), LV_PART_MAIN);
    return label;
}

lv_obj_t *screen_ui_value_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, lv_color_hex(TEXT), LV_PART_MAIN);
    return label;
}

lv_obj_t *screen_ui_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    screen_ui_muted_label(row, name);
    lv_obj_t *value = screen_ui_value_label(row, "--");
    if (value_out) *value_out = value;
    return row;
}

bool screen_ui_set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label) return false;
    if (!text) text = "";
    const char *current = lv_label_get_text(label);
    if (current && strcmp(current, text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

bool screen_ui_set_text_fmt_if_changed(lv_obj_t *label, const char *format, ...)
{
    if (!label || !format) return false;
    char text[192];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written < 0) return false;
    text[sizeof(text) - 1U] = '\0';
    return screen_ui_set_text_if_changed(label, text);
}

void screen_ui_set_kw(lv_obj_t *label, bool available, double value)
{
    if (!label) return;
    if (!available) {
        (void)screen_ui_set_text_if_changed(label, "-- kW");
        return;
    }
    (void)screen_ui_set_text_fmt_if_changed(label, "%.1f kW", value);
}

void screen_ui_set_state_text(lv_obj_t *label, const char *text, bool healthy)
{
    if (!label) return;
    (void)screen_ui_set_text_if_changed(label, text && text[0] ? text : "Unknown");
    /* The colour change is intentionally kept independent from text comparison:
     * a backend may keep the same state label while its health classification
     * changes, and correctness wins over avoiding this small-label invalidation. */
    lv_obj_set_style_text_color(label, lv_color_hex(healthy ? GOOD : WARN), LV_PART_MAIN);
}

const char *screen_ui_safe_text(const char *text, const char *fallback)
{
    return text && text[0] ? text : fallback;
}
