#include "overview_screen.h"

#include <stdio.h>
#include <string.h>

/*
 * Presentation-only screen. No backend writes, no control decisions, no safety
 * policy. Labels come from the existing backend snapshot where available.
 */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *backend_state;
    lv_obj_t *source;
    lv_obj_t *control_mode;
    lv_obj_t *grid_value;
    lv_obj_t *solar_value;
    lv_obj_t *requested_value;
    lv_obj_t *applied_value;
    lv_obj_t *meter_state;
    lv_obj_t *network_state;
    lv_obj_t *controller_state;
    lv_obj_t *alarm_state;
    lv_obj_t *inhibit_reason;
    lv_obj_t *firmware_version;
} overview_widgets_t;

static overview_widgets_t s_ui;

static void style_panel(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x151B24), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2E3948), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 12, LV_PART_MAIN);
}

static lv_obj_t *make_value_card(lv_obj_t *parent, const char *title, lv_obj_t **value_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    lv_obj_set_height(card, 112);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x9EADBF), LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_28, LV_PART_MAIN);
    if (value_out) *value_out = value;
    return card;
}

static lv_obj_t *make_state_row(lv_obj_t *parent, const char *name, lv_obj_t **value_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0x9EADBF), LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_color(value, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    if (value_out) *value_out = value;
    return row;
}

static void set_kw(lv_obj_t *label, bool available, double value)
{
    if (!label) return;
    if (!available) {
        lv_label_set_text(label, "-- kW");
        return;
    }
    lv_label_set_text_fmt(label, "%.1f kW", value);
}

static const char *safe_text(const char *value, const char *fallback)
{
    return value && value[0] != '\0' ? value : fallback;
}

lv_obj_t *overview_screen_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));

    lv_obj_t *root = lv_obj_create(parent ? parent : lv_screen_active());
    s_ui.root = root;
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0B1017), LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 14, LV_PART_MAIN);
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 10, LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 54);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Automatrix PV-DG");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);

    s_ui.backend_state = lv_label_create(header);
    lv_label_set_text(s_ui.backend_state, "BACKEND: WAITING");
    lv_obj_set_style_text_color(s_ui.backend_state, lv_color_hex(0xF2B84B), LV_PART_MAIN);

    lv_obj_t *chips = lv_obj_create(root);
    lv_obj_remove_style_all(chips);
    lv_obj_set_width(chips, LV_PCT(100));
    lv_obj_set_height(chips, 34);
    lv_obj_set_layout(chips, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, 18, LV_PART_MAIN);

    s_ui.source = lv_label_create(chips);
    lv_label_set_text(s_ui.source, "Source: unknown");
    lv_obj_set_style_text_color(s_ui.source, lv_color_hex(0xD5DEE8), LV_PART_MAIN);

    s_ui.control_mode = lv_label_create(chips);
    lv_label_set_text(s_ui.control_mode, "Control: --");
    lv_obj_set_style_text_color(s_ui.control_mode, lv_color_hex(0xD5DEE8), LV_PART_MAIN);

    lv_obj_t *values = lv_obj_create(root);
    lv_obj_remove_style_all(values);
    lv_obj_set_width(values, LV_PCT(100));
    lv_obj_set_height(values, 112);
    lv_obj_set_layout(values, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(values, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(values, 10, LV_PART_MAIN);

    make_value_card(values, "GRID / ACTIVE SOURCE", &s_ui.grid_value);
    make_value_card(values, "SOLAR", &s_ui.solar_value);
    make_value_card(values, "PV REQUESTED", &s_ui.requested_value);
    make_value_card(values, "PV APPLIED", &s_ui.applied_value);

    lv_obj_t *bottom = lv_obj_create(root);
    style_panel(bottom);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_flex_grow(bottom, 1);
    lv_obj_set_layout(bottom, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bottom, 8, LV_PART_MAIN);

    make_state_row(bottom, "Meter", &s_ui.meter_state);
    make_state_row(bottom, "Network", &s_ui.network_state);
    make_state_row(bottom, "Controller", &s_ui.controller_state);
    make_state_row(bottom, "Alarms", &s_ui.alarm_state);

    s_ui.inhibit_reason = lv_label_create(bottom);
    lv_label_set_text(s_ui.inhibit_reason, "Control reason: --");
    lv_label_set_long_mode(s_ui.inhibit_reason, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.inhibit_reason, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.inhibit_reason, lv_color_hex(0xC7D0DA), LV_PART_MAIN);

    s_ui.firmware_version = lv_label_create(bottom);
    lv_label_set_text(s_ui.firmware_version, "Firmware: --");
    lv_obj_set_style_text_color(s_ui.firmware_version, lv_color_hex(0x7F8B99), LV_PART_MAIN);

    return root;
}

void overview_screen_apply_live(const screen_live_snapshot_t *snapshot)
{
    if (!snapshot || !snapshot->valid || !s_ui.root) return;

    lv_label_set_text(s_ui.backend_state, "BACKEND: ONLINE");
    lv_obj_set_style_text_color(s_ui.backend_state, lv_color_hex(0x62D28F), LV_PART_MAIN);

    set_kw(s_ui.grid_value, snapshot->has_grid_kw, snapshot->grid_kw);
    set_kw(s_ui.solar_value, snapshot->has_solar_kw, snapshot->solar_kw);
    set_kw(s_ui.requested_value, snapshot->has_requested_pv_kw, snapshot->requested_pv_kw);
    set_kw(s_ui.applied_value, snapshot->has_applied_pv_kw, snapshot->applied_pv_kw);

    lv_label_set_text_fmt(s_ui.source, "Source: %s", safe_text(snapshot->source, "unknown"));
    lv_label_set_text_fmt(s_ui.control_mode, "Control: %s",
                          safe_text(snapshot->mode_label, "unknown"));
    lv_label_set_text(s_ui.meter_state, snapshot->meter_online ? "Online" : "Offline");

    if (snapshot->inhibit_reason[0] != '\0') {
        lv_label_set_text_fmt(s_ui.inhibit_reason, "Control reason: %s", snapshot->inhibit_reason);
    } else if (snapshot->command_blocked_by[0] != '\0') {
        lv_label_set_text_fmt(s_ui.inhibit_reason, "Control reason: %s", snapshot->command_blocked_by);
    } else {
        lv_label_set_text(s_ui.inhibit_reason, "Control reason: --");
    }
}

void overview_screen_apply_status(const screen_status_snapshot_t *snapshot)
{
    if (!snapshot || !snapshot->valid || !s_ui.root) return;

    lv_label_set_text_fmt(s_ui.network_state, "%s  %d dBm",
                          snapshot->network_online ? "Online" : "Offline", snapshot->rssi);

    if (snapshot->controller_state[0] != '\0') {
        lv_label_set_text_fmt(s_ui.controller_state, "%s%s",
                              snapshot->controller_state,
                              snapshot->last_reboot_unexpected ? " / unexpected reboot" : "");
    } else {
        lv_label_set_text(s_ui.controller_state, "Unknown");
    }

    if (snapshot->alarms == 0U) {
        lv_label_set_text(s_ui.alarm_state, "None active");
    } else {
        lv_label_set_text_fmt(s_ui.alarm_state, "0x%08lX", (unsigned long)snapshot->alarms);
    }

    if (snapshot->source_attributed_to[0] != '\0') {
        lv_label_set_text_fmt(s_ui.source, "Source: %s", snapshot->source_attributed_to);
    }

    if (snapshot->control_mode_label[0] != '\0') {
        lv_label_set_text_fmt(s_ui.control_mode, "Control: %s", snapshot->control_mode_label);
    }

    if (snapshot->control_inhibit_reason[0] != '\0') {
        lv_label_set_text_fmt(s_ui.inhibit_reason, "Control reason: %s",
                              snapshot->control_inhibit_reason);
    }

    lv_label_set_text_fmt(s_ui.firmware_version, "Firmware: %s",
                          safe_text(snapshot->firmware_version, "unknown"));

    if (snapshot->meter_stale) {
        lv_label_set_text(s_ui.meter_state, "Stale / unavailable");
    } else {
        lv_label_set_text(s_ui.meter_state, snapshot->meter_online ? "Online" : "Offline");
    }
}

void overview_screen_show_backend_unavailable(void)
{
    if (!s_ui.root) return;
    lv_label_set_text(s_ui.backend_state, "BACKEND: UNAVAILABLE");
    lv_obj_set_style_text_color(s_ui.backend_state, lv_color_hex(0xF07178), LV_PART_MAIN);
    set_kw(s_ui.grid_value, false, 0.0);
    set_kw(s_ui.solar_value, false, 0.0);
    set_kw(s_ui.requested_value, false, 0.0);
    set_kw(s_ui.applied_value, false, 0.0);
    lv_label_set_text(s_ui.source, "Source: unknown");
    lv_label_set_text(s_ui.control_mode, "Control: unknown");
    lv_label_set_text(s_ui.meter_state, "Unavailable");
    lv_label_set_text(s_ui.network_state, "Unavailable");
    lv_label_set_text(s_ui.controller_state, "Unavailable");
    lv_label_set_text(s_ui.alarm_state, "Unknown");
    lv_label_set_text(s_ui.inhibit_reason, "Control reason: backend unavailable");
}
