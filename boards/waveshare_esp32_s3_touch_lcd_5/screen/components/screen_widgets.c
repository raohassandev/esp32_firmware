#include "screen_widgets.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

lv_obj_t *screen_ui_panel(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(SCREEN_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, SCREEN_RADIUS_MD, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, SCREEN_SPACE_MD, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(SCREEN_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, SCREEN_FONT_BODY, LV_PART_MAIN);
    return obj;
}

lv_obj_t *screen_ui_card(lv_obj_t *parent)
{
    lv_obj_t *obj = screen_ui_panel(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(SCREEN_COLOR_SURFACE_RAISED), LV_PART_MAIN);
    /* A 2px accent-tinted top edge reads as "this card is lit/raised"
     * without needing a real drop shadow, which is expensive to redraw
     * on every refresh of an RGB panel. */
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(SCREEN_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_50, LV_PART_MAIN);
    return obj;
}

lv_obj_t *screen_ui_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(SCREEN_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, SCREEN_FONT_TITLE, LV_PART_MAIN);
    return label;
}

lv_obj_t *screen_ui_muted_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(SCREEN_COLOR_TEXT_SECONDARY), LV_PART_MAIN);
    return label;
}

lv_obj_t *screen_ui_value_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "--");
    lv_obj_set_style_text_color(label, lv_color_hex(SCREEN_COLOR_TEXT_PRIMARY), LV_PART_MAIN);
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
    lv_obj_set_style_pad_top(row, SCREEN_SPACE_XS, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, SCREEN_SPACE_XS, LV_PART_MAIN);

    screen_ui_muted_label(row, name);
    lv_obj_t *value = screen_ui_value_label(row, "--");
    if (value_out) *value_out = value;
    return row;
}

lv_obj_t *screen_ui_badge(lv_obj_t *parent, const char *text, bool healthy)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(badge, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_height(badge, LV_SIZE_CONTENT);
    lv_obj_set_width(badge, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(badge, SCREEN_SPACE_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(badge, SCREEN_SPACE_XS, LV_PART_MAIN);
    lv_obj_set_style_radius(badge, SCREEN_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(badge);
    lv_obj_set_style_text_font(label, SCREEN_FONT_BODY, LV_PART_MAIN);
    lv_obj_center(label);

    screen_ui_set_badge(badge, text, healthy);
    return badge;
}

void screen_ui_set_badge(lv_obj_t *badge, const char *text, bool healthy)
{
    if (!badge) return;
    const uint32_t fill = healthy ? SCREEN_COLOR_SUCCESS : SCREEN_COLOR_WARNING;
    lv_obj_set_style_bg_color(badge, lv_color_hex(fill), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, LV_PART_MAIN);

    lv_obj_t *label = lv_obj_get_child(badge, 0);
    if (!label) return;
    (void)screen_ui_set_text_if_changed(label, text && text[0] ? text : "Unknown");
    lv_obj_set_style_text_color(label, lv_color_hex(fill), LV_PART_MAIN);
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
    lv_obj_set_style_text_color(label, lv_color_hex(healthy ? SCREEN_COLOR_SUCCESS : SCREEN_COLOR_WARNING),
                                LV_PART_MAIN);
}

const char *screen_ui_safe_text(const char *text, const char *fallback)
{
    return text && text[0] ? text : fallback;
}

/* Thresholds match the common phone/laptop Wi-Fi bar convention (roughly
 * -55/-67/-75/-85 dBm break points for a 2.4 GHz link). Real RSSI on a
 * 2.4 GHz industrial panel rarely exceeds -30 dBm even next to the AP, so
 * the top bracket intentionally starts at -55, not 0.
 *
 * The icon itself never changes shape -- LVGL ships exactly one Wi-Fi
 * glyph (LV_SYMBOL_WIFI), not a set of bar-count icons -- so strength is
 * carried by color (green/amber/red) and by the dBm number next to it,
 * not by swapping glyphs. Offline uses the muted/disabled color and the
 * icon is still shown so the indicator's position never jumps around as
 * connectivity changes. */
void screen_ui_apply_wifi_indicator(lv_obj_t *label, bool online, int rssi)
{
    if (!label) return;
    char text[24];
    uint32_t color;
    if (!online) {
        snprintf(text, sizeof(text), LV_SYMBOL_WIFI " --");
        color = SCREEN_COLOR_TEXT_SECONDARY;
    } else {
        snprintf(text, sizeof(text), LV_SYMBOL_WIFI " %d", rssi);
        if (rssi >= -67) color = SCREEN_COLOR_SUCCESS;
        else if (rssi >= -80) color = SCREEN_COLOR_WARNING;
        else color = SCREEN_COLOR_DANGER;
    }
    (void)screen_ui_set_text_if_changed(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
}
