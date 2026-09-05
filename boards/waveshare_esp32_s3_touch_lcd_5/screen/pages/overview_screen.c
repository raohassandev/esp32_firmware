#include "overview_screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define OVERVIEW_VALUE_TEXT_MAX 24U
#define OVERVIEW_VALUE_DRAW_WIDTH 120
#define OVERVIEW_VALUE_DRAW_HEIGHT 28
#define OVERVIEW_STATUS_TEXT_MAX 64U
#define OVERVIEW_STATUS_DRAW_WIDTH 300

/* Presentation-only screen. No backend writes, control decisions or safety
 * policy. Labels come from the existing backend snapshots. */
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
    /* The four fast-changing values own fixed text buffers. Their labels point at
     * these buffers for their lifetime, so a live refresh never calls
     * lv_label_set_text() and therefore never asks LVGL to re-measure/re-layout a
     * value card. Only the small fixed label rectangle is invalidated when the
     * rendered one-decimal text actually changes. */
    char grid_text[OVERVIEW_VALUE_TEXT_MAX];
    char solar_text[OVERVIEW_VALUE_TEXT_MAX];
    char requested_text[OVERVIEW_VALUE_TEXT_MAX];
    char applied_text[OVERVIEW_VALUE_TEXT_MAX];
    /* RSSI is sampled on the 5-second status cadence and is expected to move by
     * a dBm or two even when the network is healthy. Keeping that value in a
     * static fixed-width label prevents each sample from re-measuring a flex row
     * and turning a tiny status change into visible RGB scanout movement. */
    char network_text[OVERVIEW_STATUS_TEXT_MAX];
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

static void make_fixed_surface(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t *make_value_card(lv_obj_t *parent,
                                 const char *title,
                                 lv_obj_t **value_out,
                                 char *value_buffer,
                                 size_t value_capacity)
{
    lv_obj_t *card = lv_obj_create(parent);
    style_panel(card);
    make_fixed_surface(card);
    /* Keep the cards fixed in the flex row, but keep the dynamic text itself in
     * a much smaller fixed draw box. A 100%-wide dynamic label made every kW
     * change invalidate almost the whole card on the physical 800x480 panel. */
    lv_obj_set_width(card, LV_PCT(24));
    lv_obj_set_height(card, 112);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x9EADBF), LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(card);
    if (value_buffer && value_capacity > 0U) {
        snprintf(value_buffer, value_capacity, "--");
        value_buffer[value_capacity - 1U] = '\0';
        lv_label_set_text_static(value, value_buffer);
    } else {
        lv_label_set_text(value, "--");
    }
    lv_obj_set_size(value, OVERVIEW_VALUE_DRAW_WIDTH, OVERVIEW_VALUE_DRAW_HEIGHT);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    if (value_out) *value_out = value;
    return card;
}

static lv_obj_t *make_state_row(lv_obj_t *parent,
                                const char *name,
                                lv_obj_t **value_out,
                                char *value_buffer,
                                size_t value_capacity)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    make_fixed_surface(row);
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
    if (value_buffer && value_capacity > 0U) {
        snprintf(value_buffer, value_capacity, "--");
        value_buffer[value_capacity - 1U] = '\0';
        lv_label_set_text_static(value, value_buffer);
    } else {
        lv_label_set_text(value, "--");
    }
    /* Status values may change length (especially RSSI and alarm text). Fix the
     * value width and clip to one line so changing text cannot alter flex-row
     * geometry. The row keeps its original content-height contract. */
    lv_obj_set_width(value, OVERVIEW_STATUS_DRAW_WIDTH);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    if (value_out) *value_out = value;
    return row;
}

/* LVGL invalidates a label when lv_label_set_text() is called even when the
 * visible string is identical. Suppress no-op writes at the UI edge. */
static bool set_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return false;
    const char *current = lv_label_get_text(label);
    if (current && strcmp(current, text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

static bool set_text_fmt_if_changed(lv_obj_t *label, const char *format, ...)
{
    if (!label || !format) return false;
    char text[256];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written < 0) return false;
    text[sizeof(text) - 1U] = '\0';
    return set_text_if_changed(label, text);
}

static void set_static_value_text(lv_obj_t *label,
                                  char *buffer,
                                  size_t capacity,
                                  const char *text)
{
    if (!label || !buffer || capacity == 0U || !text) return;
    if (strncmp(buffer, text, capacity) == 0) return;
    snprintf(buffer, capacity, "%s", text);
    buffer[capacity - 1U] = '\0';
    /* Fixed width + fixed height/line geometry means no text-size layout refresh
     * is required. The draw event reads the same static buffer pointer. */
    lv_obj_invalidate(label);
}

static void set_static_value_fmt(lv_obj_t *label,
                                 char *buffer,
                                 size_t capacity,
                                 const char *format,
                                 ...)
{
    if (!label || !buffer || capacity == 0U || !format) return;
    char text[OVERVIEW_STATUS_TEXT_MAX];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written < 0) return;
    text[sizeof(text) - 1U] = '\0';
    set_static_value_text(label, buffer, capacity, text);
}

static void set_kw_static(lv_obj_t *label,
                          char *buffer,
                          size_t capacity,
                          bool available,
                          double value)
{
    char text[OVERVIEW_VALUE_TEXT_MAX];
    if (!available) {
        snprintf(text, sizeof(text), "-- kW");
    } else {
        snprintf(text, sizeof(text), "%.1f kW", value);
    }
    text[sizeof(text) - 1U] = '\0';
    set_static_value_text(label, buffer, capacity, text);
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
    make_fixed_surface(root);
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
    make_fixed_surface(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 54);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Automatrix PV-DG");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F6FA), LV_PART_MAIN);

    s_ui.backend_state = lv_label_create(header);
    lv_label_set_text(s_ui.backend_state, "BACKEND: WAITING");
    lv_obj_set_style_text_color(s_ui.backend_state, lv_color_hex(0xF2B84B), LV_PART_MAIN);

    lv_obj_t *chips = lv_obj_create(root);
    lv_obj_remove_style_all(chips);
    make_fixed_surface(chips);
    lv_obj_set_width(chips, LV_PCT(100));
    lv_obj_set_height(chips, 34);
    lv_obj_set_layout(chips, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, 18, LV_PART_MAIN);

    s_ui.source = lv_label_create(chips);
    lv_label_set_text(s_ui.source, "Source: unknown");
    lv_obj_set_width(s_ui.source, 220);
    lv_label_set_long_mode(s_ui.source, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.source, lv_color_hex(0xD5DEE8), LV_PART_MAIN);

    s_ui.control_mode = lv_label_create(chips);
    lv_label_set_text(s_ui.control_mode, "Control: --");
    lv_obj_set_flex_grow(s_ui.control_mode, 1);
    lv_label_set_long_mode(s_ui.control_mode, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.control_mode, lv_color_hex(0xD5DEE8), LV_PART_MAIN);

    lv_obj_t *values = lv_obj_create(root);
    lv_obj_remove_style_all(values);
    make_fixed_surface(values);
    lv_obj_set_width(values, LV_PCT(100));
    lv_obj_set_height(values, 112);
    lv_obj_set_layout(values, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(values, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(values, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(values, 0, LV_PART_MAIN);

    make_value_card(values, "GRID / ACTIVE SOURCE", &s_ui.grid_value,
                    s_ui.grid_text, sizeof(s_ui.grid_text));
    make_value_card(values, "SOLAR", &s_ui.solar_value,
                    s_ui.solar_text, sizeof(s_ui.solar_text));
    make_value_card(values, "PV REQUESTED", &s_ui.requested_value,
                    s_ui.requested_text, sizeof(s_ui.requested_text));
    make_value_card(values, "PV APPLIED", &s_ui.applied_value,
                    s_ui.applied_text, sizeof(s_ui.applied_text));

    lv_obj_t *bottom = lv_obj_create(root);
    style_panel(bottom);
    make_fixed_surface(bottom);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_flex_grow(bottom, 1);
    lv_obj_set_layout(bottom, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(bottom, 8, LV_PART_MAIN);

    make_state_row(bottom, "Meter", &s_ui.meter_state, NULL, 0U);
    make_state_row(bottom, "Network", &s_ui.network_state,
                   s_ui.network_text, sizeof(s_ui.network_text));
    make_state_row(bottom, "Controller", &s_ui.controller_state, NULL, 0U);
    make_state_row(bottom, "Alarms", &s_ui.alarm_state, NULL, 0U);

    s_ui.inhibit_reason = lv_label_create(bottom);
    lv_label_set_text(s_ui.inhibit_reason, "Control reason: --");
    lv_label_set_long_mode(s_ui.inhibit_reason, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.inhibit_reason, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.inhibit_reason, lv_color_hex(0xC7D0DA), LV_PART_MAIN);

    s_ui.firmware_version = lv_label_create(bottom);
    lv_label_set_text(s_ui.firmware_version, "Firmware: --");
    lv_obj_set_width(s_ui.firmware_version, LV_PCT(100));
    lv_label_set_long_mode(s_ui.firmware_version, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_ui.firmware_version, lv_color_hex(0x7F8B99), LV_PART_MAIN);
    return root;
}

void overview_screen_apply_live(const screen_live_snapshot_t *snapshot)
{
    if (!snapshot || !snapshot->valid || !s_ui.root) return;

    if (set_text_if_changed(s_ui.backend_state, "BACKEND: ONLINE")) {
        lv_obj_set_style_text_color(s_ui.backend_state, lv_color_hex(0x62D28F), LV_PART_MAIN);
    }
    set_kw_static(s_ui.grid_value, s_ui.grid_text, sizeof(s_ui.grid_text),
                  snapshot->has_grid_kw, snapshot->grid_kw);
    set_kw_static(s_ui.solar_value, s_ui.solar_text, sizeof(s_ui.solar_text),
                  snapshot->has_solar_kw, snapshot->solar_kw);
    set_kw_static(s_ui.requested_value, s_ui.requested_text, sizeof(s_ui.requested_text),
                  snapshot->has_requested_pv_kw, snapshot->requested_pv_kw);
    set_kw_static(s_ui.applied_value, s_ui.applied_text, sizeof(s_ui.applied_text),
                  snapshot->has_applied_pv_kw, snapshot->applied_pv_kw);

    /* Do not render live.source. /api/status.source.attributed_to is already
     * fail-closed against stale/conflicting source evidence and is authoritative. */
    (void)set_text_fmt_if_changed(s_ui.control_mode, "Control: %s",
                                  safe_text(snapshot->mode_label, "unknown"));
    (void)set_text_if_changed(s_ui.meter_state, snapshot->meter_online ? "Online" : "Offline");

    if (snapshot->inhibit_reason[0] != '\0') {
        (void)set_text_fmt_if_changed(s_ui.inhibit_reason, "Control reason: %s",
                                      snapshot->inhibit_reason);
    } else if (snapshot->command_blocked_by[0] != '\0') {
        (void)set_text_fmt_if_changed(s_ui.inhibit_reason, "Control reason: %s",
                                      snapshot->command_blocked_by);
    } else {
        (void)set_text_if_changed(s_ui.inhibit_reason, "Control reason: --");
    }
}

void overview_screen_apply_status(const screen_status_snapshot_t *snapshot)
{
    if (!snapshot || !snapshot->valid || !s_ui.root) return;

    set_static_value_fmt(s_ui.network_state,
                         s_ui.network_text,
                         sizeof(s_ui.network_text),
                         "%s  %d dBm",
                         snapshot->network_online ? "Online" : "Offline",
                         snapshot->rssi);

    if (snapshot->controller_state[0] != '\0') {
        (void)set_text_fmt_if_changed(s_ui.controller_state, "%s%s",
                                      snapshot->controller_state,
                                      snapshot->last_reboot_unexpected ? " / unexpected reboot" : "");
    } else {
        (void)set_text_if_changed(s_ui.controller_state, "Unknown");
    }

    if (snapshot->alarms == 0U) {
        (void)set_text_if_changed(s_ui.alarm_state, "None active");
    } else if (snapshot->alarm_name_count == 1U) {
        (void)set_text_if_changed(s_ui.alarm_state, snapshot->alarm_names[0]);
    } else if (snapshot->alarm_name_count > 1U) {
        (void)set_text_fmt_if_changed(s_ui.alarm_state, "%s +%u more",
                                      snapshot->alarm_names[0],
                                      (unsigned)(snapshot->alarm_name_count - 1U));
    } else {
        (void)set_text_if_changed(s_ui.alarm_state, "Active alarm(s)");
    }

    (void)set_text_fmt_if_changed(s_ui.source, "Source: %s",
                                  safe_text(snapshot->source_attributed_to, "unknown"));
    if (snapshot->control_mode_label[0] != '\0') {
        (void)set_text_fmt_if_changed(s_ui.control_mode, "Control: %s",
                                      snapshot->control_mode_label);
    }
    if (snapshot->control_inhibit_reason[0] != '\0') {
        (void)set_text_fmt_if_changed(s_ui.inhibit_reason, "Control reason: %s",
                                      snapshot->control_inhibit_reason);
    }
    (void)set_text_fmt_if_changed(s_ui.firmware_version, "Firmware: %s",
                                  safe_text(snapshot->firmware_version, "unknown"));

    if (snapshot->meter_stale) {
        (void)set_text_if_changed(s_ui.meter_state, "Stale / unavailable");
    } else {
        (void)set_text_if_changed(s_ui.meter_state,
                                  snapshot->meter_online ? "Online" : "Offline");
    }
}

void overview_screen_show_backend_unavailable(void)
{
    if (!s_ui.root) return;
    if (set_text_if_changed(s_ui.backend_state, "BACKEND: UNAVAILABLE")) {
        lv_obj_set_style_text_color(s_ui.backend_state, lv_color_hex(0xF07178), LV_PART_MAIN);
    }
    set_kw_static(s_ui.grid_value, s_ui.grid_text, sizeof(s_ui.grid_text), false, 0.0);
    set_kw_static(s_ui.solar_value, s_ui.solar_text, sizeof(s_ui.solar_text), false, 0.0);
    set_kw_static(s_ui.requested_value, s_ui.requested_text, sizeof(s_ui.requested_text), false, 0.0);
    set_kw_static(s_ui.applied_value, s_ui.applied_text, sizeof(s_ui.applied_text), false, 0.0);
    (void)set_text_if_changed(s_ui.source, "Source: unknown");
    (void)set_text_if_changed(s_ui.control_mode, "Control: unknown");
    (void)set_text_if_changed(s_ui.meter_state, "Unavailable");
    set_static_value_text(s_ui.network_state,
                          s_ui.network_text,
                          sizeof(s_ui.network_text),
                          "Unavailable");
    (void)set_text_if_changed(s_ui.controller_state, "Unavailable");
    (void)set_text_if_changed(s_ui.alarm_state, "Unknown");
    (void)set_text_if_changed(s_ui.inhibit_reason, "Control reason: backend unavailable");
}
