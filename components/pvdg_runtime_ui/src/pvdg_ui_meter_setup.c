#include "pvdg_ui_meter_setup.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvdg_ui_theme.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *badge;
    lv_obj_t *msg;
    lv_obj_t *summary;
    lv_obj_t *refresh;
    lv_obj_t *save;
    lv_obj_t *prev;
    lv_obj_t *next;
    lv_obj_t *add;
    lv_obj_t *remove;
    lv_obj_t *enabled;
    lv_obj_t *name;
    lv_obj_t *host;
    lv_obj_t *port;
    lv_obj_t *unit_id;
    lv_obj_t *timeout_ms;
    lv_obj_t *function_code;
    lv_obj_t *address;
    lv_obj_t *data_type;
    lv_obj_t *word_order;
    lv_obj_t *scale;
    lv_obj_t *poll_ms;
    lv_obj_t *role;
    lv_obj_t *generator_index;
    lv_obj_t *keyboard;
    pvdg_ui_meter_list_t list;
    pvdg_ui_meter_setup_callbacks_t cb;
    uint8_t index;
    bool available;
    bool write;
    bool busy;
} meter_ui_t;

static meter_ui_t s;

static lv_obj_t *text_label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    pvdg_ui_style_label(label, lv_color_hex(color), PVDG_UI_FONT_BODY);
    return label;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text,
                        lv_event_cb_t callback, int32_t width)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_size(obj, width, PVDG_UI_TOUCH_MIN);
    lv_obj_t *caption = text_label(obj, text, PVDG_UI_COLOR_TEXT);
    lv_obj_center(caption);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    return obj;
}

static void keyboard_hide(void)
{
    if (!s.keyboard) return;
    lv_keyboard_set_textarea(s.keyboard, NULL);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) keyboard_hide();
}

static void field_event(lv_event_t *event)
{
    if (!s.keyboard) return;
    lv_obj_t *field = lv_event_get_target_obj(event);

    /* LV_EVENT_FOCUSED is followed by LV_EVENT_CLICKED for a normal tap.
     * Re-applying the same keyboard target and foreground operation on both
     * events forces an unnecessary layout/focus cycle that can make the form
     * appear to shake on the touch display. Keep repeat events idempotent. */
    if (lv_keyboard_get_textarea(s.keyboard) == field &&
        !lv_obj_has_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_keyboard_set_textarea(s.keyboard, field);
    lv_obj_remove_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s.keyboard);
}

static lv_obj_t *row(lv_obj_t *parent, const char *name)
{
    lv_obj_t *obj = lv_obj_create(parent);
    pvdg_ui_style_root(obj);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_height(obj, 34);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *caption = pvdg_ui_make_muted(obj, name);
    lv_obj_set_width(caption, 112);
    return obj;
}

static lv_obj_t *text_field(lv_obj_t *parent, const char *name)
{
    lv_obj_t *container = row(parent, name);
    lv_obj_t *field = lv_textarea_create(container);
    lv_textarea_set_one_line(field, true);
    lv_obj_set_height(field, 30);
    lv_obj_set_flex_grow(field, 1);
    lv_obj_add_event_cb(field, field_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(field, field_event, LV_EVENT_CLICKED, NULL);
    return field;
}

static lv_obj_t *dropdown_field(lv_obj_t *parent, const char *name,
                                const char *options)
{
    lv_obj_t *container = row(parent, name);
    lv_obj_t *dropdown = lv_dropdown_create(container);
    lv_dropdown_set_options(dropdown, options);
    lv_obj_set_flex_grow(dropdown, 1);
    return dropdown;
}

static lv_obj_t *switch_field(lv_obj_t *parent, const char *name)
{
    lv_obj_t *container = row(parent, name);
    return lv_switch_create(container);
}

static bool parse_u32(lv_obj_t *field, uint32_t minimum, uint32_t maximum,
                      uint32_t *out, const char *name)
{
    const char *text = lv_textarea_get_text(field);
    if (!text || !text[0]) {
        char message[96];
        snprintf(message, sizeof(message), "%s is required.", name);
        pvdg_ui_label_set_if_changed(s.msg, message);
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || !end || end == text || *end != '\0' ||
        value < minimum || value > maximum) {
        char message[128];
        snprintf(message, sizeof(message), "%s must be %lu-%lu.", name,
                 (unsigned long)minimum, (unsigned long)maximum);
        pvdg_ui_label_set_if_changed(s.msg, message);
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_double(lv_obj_t *field, double minimum, double maximum,
                         bool nonzero, double *out, const char *name)
{
    const char *text = lv_textarea_get_text(field);
    errno = 0;
    char *end = NULL;
    double value = strtod(text ? text : "", &end);
    if (errno != 0 || !end || end == text || *end != '\0' || !isfinite(value) ||
        value < minimum || value > maximum || (nonzero && value == 0.0)) {
        char message[128];
        snprintf(message, sizeof(message), "%s is outside its valid range.", name);
        pvdg_ui_label_set_if_changed(s.msg, message);
        return false;
    }
    *out = value;
    return true;
}

static void set_u32(lv_obj_t *field, uint32_t value)
{
    char text[24];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    lv_textarea_set_text(field, text);
}

static void set_double(lv_obj_t *field, double value)
{
    char text[32];
    snprintf(text, sizeof(text), "%.6g", value);
    lv_textarea_set_text(field, text);
}

static void update_generator_state(void)
{
    const bool generator_role =
        lv_dropdown_get_selected(s.role) == PVDG_UI_METER_ROLE_GENERATOR;
    if (generator_role && s.write && !s.busy) {
        lv_obj_remove_state(s.generator_index, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s.generator_index, LV_STATE_DISABLED);
    }
}

static void update_navigation_state(void)
{
    if (!s.prev || !s.next) return;
    const bool have = s.available && s.list.count > 0U && !s.busy;
    if (have && s.index > 0U) lv_obj_remove_state(s.prev, LV_STATE_DISABLED);
    else lv_obj_add_state(s.prev, LV_STATE_DISABLED);
    if (have && s.index + 1U < s.list.count) lv_obj_remove_state(s.next, LV_STATE_DISABLED);
    else lv_obj_add_state(s.next, LV_STATE_DISABLED);
}

static void role_event(lv_event_t *event)
{
    (void)event;
    update_generator_state();
}

static bool commit_editor(void)
{
    if (!s.available || s.list.count == 0U || s.index >= s.list.count) return true;

    pvdg_ui_meter_config_t candidate = s.list.meters[s.index];
    const char *name = lv_textarea_get_text(s.name);
    const char *host = lv_textarea_get_text(s.host);
    if (!name || !name[0] || strlen(name) >= sizeof(candidate.name)) {
        pvdg_ui_label_set_if_changed(
            s.msg, "Meter name is required and must be shorter than 24 bytes.");
        return false;
    }
    if (host && strlen(host) >= sizeof(candidate.host)) {
        pvdg_ui_label_set_if_changed(s.msg, "Meter host is too long.");
        return false;
    }

    candidate.enabled = lv_obj_has_state(s.enabled, LV_STATE_CHECKED);
    snprintf(candidate.name, sizeof(candidate.name), "%s", name);
    snprintf(candidate.host, sizeof(candidate.host), "%s", host ? host : "");

    uint32_t value = 0U;
    if (!parse_u32(s.port, 1U, 65535U, &value, "Port")) return false;
    candidate.port = (uint16_t)value;
    if (!parse_u32(s.unit_id, 1U, 247U, &value, "Unit ID")) return false;
    candidate.unit_id = (uint8_t)value;
    if (!parse_u32(s.timeout_ms, 100U, 60000U, &value, "Timeout")) return false;
    candidate.timeout_ms = value;
    candidate.function_code =
        lv_dropdown_get_selected(s.function_code) == 0U ? 3U : 4U;
    if (!parse_u32(s.address, 0U, 65535U, &value, "Register address")) return false;
    candidate.active_power_address = (uint16_t)value;
    candidate.data_type =
        (pvdg_ui_meter_data_type_t)lv_dropdown_get_selected(s.data_type);
    candidate.word_order =
        (pvdg_ui_meter_word_order_t)lv_dropdown_get_selected(s.word_order);
    if (!parse_double(s.scale, -1000000.0, 1000000.0, true,
                      &candidate.scale, "Scale")) return false;
    if (!parse_u32(s.poll_ms, 100U, 60000U, &value, "Poll interval")) return false;
    candidate.poll_ms = value;
    candidate.role = (pvdg_ui_meter_role_t)lv_dropdown_get_selected(s.role);
    candidate.generator_index = candidate.role == PVDG_UI_METER_ROLE_GENERATOR
                                    ? (uint8_t)lv_dropdown_get_selected(s.generator_index)
                                    : PVDG_UI_METER_GENERATOR_NONE;

    if (candidate.enabled && !candidate.host[0]) {
        pvdg_ui_label_set_if_changed(s.msg, "An enabled meter requires a host.");
        return false;
    }

    s.list.meters[s.index] = candidate;
    return true;
}

static void update_summary(void)
{
    char text[192];
    if (!s.available) {
        snprintf(text, sizeof(text), "Meters: --");
    } else if (s.list.count == 0U) {
        snprintf(text, sizeof(text), "Meters: 0 / %u",
                 (unsigned)PVDG_UI_MAX_METERS);
    } else {
        const pvdg_ui_meter_role_state_t role_state =
            pvdg_ui_meter_role_state(&s.list);
        snprintf(text, sizeof(text), "Meter %u/%u · Grid roles %u · role state %s",
                 (unsigned)s.index + 1U, (unsigned)s.list.count,
                 (unsigned)role_state.grid_count,
                 role_state.valid ? "valid" : "fail-closed");
    }
    pvdg_ui_label_set_if_changed(s.summary, text);
}

static void load_editor(void)
{
    if (!s.root) return;
    if (!s.available || s.list.count == 0U || s.index >= s.list.count) {
        lv_textarea_set_text(s.name, "");
        lv_textarea_set_text(s.host, "");
        update_summary();
        update_navigation_state();
        return;
    }

    const pvdg_ui_meter_config_t *meter = &s.list.meters[s.index];
    if (meter->enabled) lv_obj_add_state(s.enabled, LV_STATE_CHECKED);
    else lv_obj_remove_state(s.enabled, LV_STATE_CHECKED);
    lv_textarea_set_text(s.name, meter->name);
    lv_textarea_set_text(s.host, meter->host);
    set_u32(s.port, meter->port);
    set_u32(s.unit_id, meter->unit_id);
    set_u32(s.timeout_ms, meter->timeout_ms);
    lv_dropdown_set_selected(s.function_code, meter->function_code == 4U ? 1U : 0U);
    set_u32(s.address, meter->active_power_address);
    lv_dropdown_set_selected(s.data_type, (uint16_t)meter->data_type);
    lv_dropdown_set_selected(s.word_order, (uint16_t)meter->word_order);
    set_double(s.scale, meter->scale);
    set_u32(s.poll_ms, meter->poll_ms);
    lv_dropdown_set_selected(s.role, (uint16_t)meter->role);
    lv_dropdown_set_selected(
        s.generator_index,
        meter->role == PVDG_UI_METER_ROLE_GENERATOR && meter->generator_index < 3U
            ? meter->generator_index
            : 0U);
    update_generator_state();
    update_summary();
    update_navigation_state();
}

static void set_form_disabled(bool disabled)
{
    lv_obj_t *controls[] = {
        s.enabled, s.name, s.host, s.port, s.unit_id, s.timeout_ms,
        s.function_code, s.address, s.data_type, s.word_order, s.scale,
        s.poll_ms, s.role,
    };
    for (size_t i = 0U; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        if (!controls[i]) continue;
        if (disabled) lv_obj_add_state(controls[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(controls[i], LV_STATE_DISABLED);
    }
    update_generator_state();
}

static void refresh_event(lv_event_t *event)
{
    (void)event;
    keyboard_hide();
    if (!s.busy && s.cb.request_list) s.cb.request_list(s.cb.user);
}

static void save_event(lv_event_t *event)
{
    (void)event;
    if (!s.available || !s.write || s.busy || !s.cb.submit_list) return;
    if (!commit_editor()) return;
    char error[192] = {0};
    if (pvdg_ui_meter_list_validate(&s.list, error, sizeof(error)) !=
        PVDG_UI_METER_CONFIG_OK) {
        pvdg_ui_label_set_if_changed(s.msg, error);
        return;
    }
    keyboard_hide();
    s.cb.submit_list(&s.list, s.cb.user);
}

static void previous_event(lv_event_t *event)
{
    (void)event;
    if (!s.available || s.list.count == 0U || s.index == 0U) return;
    if (s.write && !commit_editor()) return;
    s.index--;
    load_editor();
}

static void next_event(lv_event_t *event)
{
    (void)event;
    if (!s.available || s.list.count == 0U || s.index + 1U >= s.list.count) return;
    if (s.write && !commit_editor()) return;
    s.index++;
    load_editor();
}

static void add_event(lv_event_t *event)
{
    (void)event;
    if (!s.available || !s.write || s.busy ||
        s.list.count >= PVDG_UI_MAX_METERS) return;
    if (s.list.count > 0U && !commit_editor()) return;
    const uint8_t index = s.list.count;
    pvdg_ui_meter_defaults(&s.list.meters[index], index);
    s.list.count++;
    s.index = index;
    load_editor();
    pvdg_ui_label_set_if_changed(
        s.msg, "New meter added locally. Validate + Save to persist it.");
}

static void remove_event(lv_event_t *event)
{
    (void)event;
    if (!s.available || !s.write || s.busy || s.list.count == 0U) return;
    for (uint8_t i = s.index; i + 1U < s.list.count; ++i) {
        s.list.meters[i] = s.list.meters[i + 1U];
    }
    memset(&s.list.meters[s.list.count - 1U], 0, sizeof(s.list.meters[0]));
    s.list.count--;
    if (s.list.count == 0U) s.index = 0U;
    else if (s.index >= s.list.count) s.index = (uint8_t)(s.list.count - 1U);
    load_editor();
    pvdg_ui_label_set_if_changed(
        s.msg, "Meter removed locally. Validate + Save to persist the list.");
}

lv_obj_t *pvdg_ui_meter_setup_create(
    lv_obj_t *parent,
    const pvdg_ui_meter_setup_callbacks_t *callbacks)
{
    memset(&s, 0, sizeof(s));
    if (callbacks) s.cb = *callbacks;

    s.root = lv_obj_create(parent);
    pvdg_ui_style_root(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(s.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s.root, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s.root, 3, LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(s.root);
    pvdg_ui_style_root(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 34);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    pvdg_ui_make_title(header, "Meter Setup");
    s.badge = pvdg_ui_make_badge(
        header, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));

    lv_obj_t *actions = lv_obj_create(s.root);
    pvdg_ui_style_root(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 44);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 4, LV_PART_MAIN);
    s.summary = pvdg_ui_make_muted(actions, "Meters: --");
    lv_obj_set_flex_grow(s.summary, 1);
    s.prev = button(actions, "<", previous_event, 42);
    s.next = button(actions, ">", next_event, 42);
    s.add = button(actions, "+", add_event, 42);
    s.remove = button(actions, "-", remove_event, 42);
    s.refresh = button(actions, "Refresh", refresh_event, 78);
    s.save = button(actions, "Save", save_event, 70);

    s.msg = pvdg_ui_make_muted(
        s.root, "Load the complete meter list before saving.");
    lv_obj_set_width(s.msg, LV_PCT(100));
    lv_label_set_long_mode(s.msg, LV_LABEL_LONG_DOT);

    lv_obj_t *form = pvdg_ui_make_card(s.root);
    lv_obj_set_width(form, LV_PCT(100));
    lv_obj_set_flex_grow(form, 1);
    lv_obj_set_layout(form, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(form, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(form, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(form, 8, LV_PART_MAIN);

    lv_obj_t *left = lv_obj_create(form);
    pvdg_ui_style_root(left);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(left, 1, LV_PART_MAIN);

    lv_obj_t *right = lv_obj_create(form);
    pvdg_ui_style_root(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(right, 1, LV_PART_MAIN);

    s.enabled = switch_field(left, "Enabled");
    s.name = text_field(left, "Name");
    s.host = text_field(left, "Host / IP");
    s.port = text_field(left, "TCP port");
    s.unit_id = text_field(left, "Unit ID");
    s.timeout_ms = text_field(left, "Timeout ms");
    s.role = dropdown_field(left, "Role", "Unassigned\nGrid\nGenerator\nLoad\nPV");
    s.generator_index = dropdown_field(
        left, "Generator", "Generator 1\nGenerator 2\nGenerator 3");
    lv_obj_add_event_cb(s.role, role_event, LV_EVENT_VALUE_CHANGED, NULL);

    s.function_code = dropdown_field(right, "Function", "FC03 Holding\nFC04 Input");
    s.address = text_field(right, "Power address");
    s.data_type = dropdown_field(
        right, "Data type", "UINT16\nINT16\nUINT32\nINT32\nFLOAT32");
    s.word_order = dropdown_field(right, "Word order", "ABCD\nCDAB\nBADC\nDCBA");
    s.scale = text_field(right, "Scale");
    s.poll_ms = text_field(right, "Poll ms");

    /* Keep the keyboard local to Meter Setup and out of flex layout. This
     * avoids global top-layer focus/scroll interactions with the form. */
    s.keyboard = lv_keyboard_create(s.root);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s.keyboard, 718, 190);
    lv_obj_align(s.keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);

    set_form_disabled(true);
    lv_obj_add_state(s.save, LV_STATE_DISABLED);
    lv_obj_add_state(s.add, LV_STATE_DISABLED);
    lv_obj_add_state(s.remove, LV_STATE_DISABLED);
    update_navigation_state();
    if (s.cb.request_list) s.cb.request_list(s.cb.user);
    return s.root;
}

void pvdg_ui_meter_setup_set_list(const pvdg_ui_meter_list_t *list)
{
    if (!s.root || !list) return;
    s.list = *list;
    s.available = true;
    if (s.list.count == 0U) s.index = 0U;
    else if (s.index >= s.list.count) s.index = (uint8_t)(s.list.count - 1U);

    char error[192] = {0};
    const bool valid =
        pvdg_ui_meter_list_validate(list, error, sizeof(error)) ==
        PVDG_UI_METER_CONFIG_OK;
    pvdg_ui_badge_set(s.badge, valid ? "List valid" : "Config invalid",
                      lv_color_hex(valid ? PVDG_UI_COLOR_SUCCESS
                                         : PVDG_UI_COLOR_DANGER));
    if (!valid && error[0]) pvdg_ui_label_set_if_changed(s.msg, error);
    load_editor();
}

void pvdg_ui_meter_setup_apply_state(const pvdg_ui_meter_setup_state_t *state)
{
    s.write = state && state->write_allowed;
    s.busy = state && state->busy;
    if (state && state->message) {
        pvdg_ui_label_set_if_changed(s.msg, state->message);
    }

    const bool can_edit = s.available && s.write && !s.busy;
    set_form_disabled(!can_edit);
    if (can_edit) {
        lv_obj_remove_state(s.save, LV_STATE_DISABLED);
        if (s.list.count < PVDG_UI_MAX_METERS) {
            lv_obj_remove_state(s.add, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s.add, LV_STATE_DISABLED);
        }
        if (s.list.count > 0U) lv_obj_remove_state(s.remove, LV_STATE_DISABLED);
        else lv_obj_add_state(s.remove, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s.save, LV_STATE_DISABLED);
        lv_obj_add_state(s.add, LV_STATE_DISABLED);
        lv_obj_add_state(s.remove, LV_STATE_DISABLED);
    }
    update_navigation_state();
}
