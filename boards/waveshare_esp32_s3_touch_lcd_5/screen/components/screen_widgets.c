#include "screen_widgets.h"

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

void screen_ui_set_kw(lv_obj_t *label, bool available, double value)
{
    if (!label) return;
    if (!available) {
        lv_label_set_text(label, "-- kW");
        return;
    }
    lv_label_set_text_fmt(label, "%.1f kW", value);
}

void screen_ui_set_state_text(lv_obj_t *label, const char *text, bool healthy)
{
    if (!label) return;
    lv_label_set_text(label, text && text[0] ? text : "Unknown");
    lv_obj_set_style_text_color(label, lv_color_hex(healthy ? GOOD : WARN), LV_PART_MAIN);
}

const char *screen_ui_safe_text(const char *text, const char *fallback)
{
    return text && text[0] ? text : fallback;
}
