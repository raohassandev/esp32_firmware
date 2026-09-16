#include "pvdg_ui_inverter_setup.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvdg_ui_theme.h"

#define PROFILE_OPTIONS_BYTES 1200U

typedef struct {
    lv_obj_t *root;
    lv_obj_t *badge;
    lv_obj_t *msg;
    lv_obj_t *summary;
    lv_obj_t *profile_detail;
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
    lv_obj_t *rated_kw;
    lv_obj_t *profile;
    lv_obj_t *apply_profile;
    lv_obj_t *keyboard;
    pvdg_ui_inverter_list_t list;
    pvdg_ui_inverter_profile_catalog_t catalog;
    pvdg_ui_inverter_assignment_map_t assignments;
    pvdg_ui_inverter_setup_callbacks_t cb;
    char profile_options[PROFILE_OPTIONS_BYTES];
    uint8_t index;
    bool config_ok;
    bool profiles_ok;
    bool assignments_ok;
    bool write;
    bool busy;
} inverter_ui_t;

static inverter_ui_t s;

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
    lv_keyboard_set_textarea(s.keyboard, field);
    lv_obj_remove_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s.keyboard);
}

static lv_obj_t *row(lv_obj_t *parent, const char *name)
{
    lv_obj_t *obj = lv_obj_create(parent);
    pvdg_ui_style_root(obj);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_height(obj, 38);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *caption = pvdg_ui_make_muted(obj, name);
    lv_obj_set_width(caption, 125);
    return obj;
}

static lv_obj_t *text_field(lv_obj_t *parent, const char *name)
{
    lv_obj_t *container = row(parent, name);
    lv_obj_t *field = lv_textarea_create(container);
    lv_textarea_set_one_line(field, true);
    lv_obj_set_height(field, 32);
    lv_obj_set_flex_grow(field, 1);
    lv_obj_add_event_cb(field, field_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(field, field_event, LV_EVENT_CLICKED, NULL);
    return field;
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
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text ? text : "", &end, 10);
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

static bool parse_rated(lv_obj_t *field, double *out)
{
    const char *text = lv_textarea_get_text(field);
    errno = 0;
    char *end = NULL;
    double value = strtod(text ? text : "", &end);
    if (errno != 0 || !end || end == text || *end != '\0' || !isfinite(value) ||
        value < 0.0 || value > PVDG_UI_INVERTER_RATED_MAX_KW) {
        pvdg_ui_label_set_if_changed(s.msg, "Rated power must be 0-100000 kW.");
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

static const char *qualification_label(pvdg_ui_profile_qualification_t qualification)
{
    switch (qualification) {
    case PVDG_UI_PROFILE_DOCUMENTED: return "Documented";
    case PVDG_UI_PROFILE_SIMULATOR_VERIFIED: return "Simulator verified";
    case PVDG_UI_PROFILE_BENCH_VERIFIED: return "Bench verified";
    case PVDG_UI_PROFILE_READ_ONLY_QUALIFIED: return "Read-only qualified";
    case PVDG_UI_PROFILE_WRITE_QUALIFIED: return "Write qualified";
    case PVDG_UI_PROFILE_PRODUCTION_APPROVED: return "Production approved";
    default: return "Unknown";
    }
}

static int profile_index_for_id(const char *profile_id)
{
    if (!profile_id || !profile_id[0] || !s.profiles_ok) return -1;
    const uint8_t count = s.catalog.count > PVDG_UI_MAX_INVERTER_PROFILES
                              ? PVDG_UI_MAX_INVERTER_PROFILES : s.catalog.count;
    for (uint8_t i = 0U; i < count; ++i) {
        if (strcmp(s.catalog.profiles[i].id, profile_id) == 0) return (int)i;
    }
    return -1;
}

static void rebuild_profile_options(void)
{
    s.profile_options[0] = '\0';
    if (!s.profiles_ok || s.catalog.count == 0U) {
        snprintf(s.profile_options, sizeof(s.profile_options), "No compiled profiles");
        lv_dropdown_set_options(s.profile, s.profile_options);
        return;
    }

    size_t used = 0U;
    const uint8_t count = s.catalog.count > PVDG_UI_MAX_INVERTER_PROFILES
                              ? PVDG_UI_MAX_INVERTER_PROFILES : s.catalog.count;
    for (uint8_t i = 0U; i < count; ++i) {
        const char *id = s.catalog.profiles[i].id[0]
                             ? s.catalog.profiles[i].id : "<unnamed>";
        const int written = snprintf(s.profile_options + used,
                                     sizeof(s.profile_options) - used,
                                     "%s%s", i == 0U ? "" : "\n", id);
        if (written < 0 || (size_t)written >= sizeof(s.profile_options) - used) break;
        used += (size_t)written;
    }
    lv_dropdown_set_options(s.profile, s.profile_options);
}

static void update_profile_detail(void)
{
    if (!s.profile_detail) return;
    if (!s.profiles_ok || s.catalog.count == 0U) {
        pvdg_ui_label_set_if_changed(s.profile_detail,
                                     "Compiled profile catalog unavailable.");
        return;
    }
    uint16_t selected = lv_dropdown_get_selected(s.profile);
    if (selected >= s.catalog.count || selected >= PVDG_UI_MAX_INVERTER_PROFILES) {
        selected = 0U;
    }
    const pvdg_ui_inverter_profile_t *profile = &s.catalog.profiles[selected];
    char text[256];
    snprintf(text, sizeof(text), "%s · %s · %s · read %s · write %s%s",
             profile->manufacturer[0] ? profile->manufacturer : "Unknown maker",
             profile->model_family[0] ? profile->model_family : "Unknown family",
             qualification_label(profile->qualification),
             profile->read_allowed ? "allowed" : "blocked",
             profile->write_allowed ? "allowed" : "blocked",
             profile->simulator_only ? " · SIMULATOR ONLY" : "");
    pvdg_ui_label_set_if_changed(s.profile_detail, text);
}

static void load_profile_selection(void)
{
    if (!s.profile) return;
    int selected = -1;
    if (s.assignments_ok && s.index < PVDG_UI_MAX_INVERTER_CONFIGS) {
        selected = profile_index_for_id(s.assignments.assignments[s.index].profile_id);
    }
    if (selected < 0) selected = 0;
    lv_dropdown_set_selected(s.profile, (uint16_t)selected);
    update_profile_detail();
}

static void update_navigation_state(void)
{
    const bool have = s.config_ok && s.list.count > 0U && !s.busy;
    if (have && s.index > 0U) lv_obj_remove_state(s.prev, LV_STATE_DISABLED);
    else lv_obj_add_state(s.prev, LV_STATE_DISABLED);
    if (have && s.index + 1U < s.list.count) lv_obj_remove_state(s.next, LV_STATE_DISABLED);
    else lv_obj_add_state(s.next, LV_STATE_DISABLED);
}

static bool commit_editor(void)
{
    if (!s.config_ok || s.list.count == 0U || s.index >= s.list.count) return true;

    pvdg_ui_inverter_config_t candidate = s.list.inverters[s.index];
    const char *name = lv_textarea_get_text(s.name);
    const char *host = lv_textarea_get_text(s.host);
    if (!name || !name[0] || strlen(name) >= sizeof(candidate.name)) {
        pvdg_ui_label_set_if_changed(
            s.msg, "Inverter name is required and must be shorter than 24 bytes.");
        return false;
    }
    if (host && strlen(host) >= sizeof(candidate.host)) {
        pvdg_ui_label_set_if_changed(s.msg, "Inverter host is too long.");
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
    if (!parse_rated(s.rated_kw, &candidate.rated_kw)) return false;

    if (candidate.enabled && (!candidate.host[0] || candidate.rated_kw <= 0.0)) {
        pvdg_ui_label_set_if_changed(
            s.msg, "An enabled inverter requires a host and rated power greater than zero.");
        return false;
    }

    s.list.inverters[s.index] = candidate;
    return true;
}

static void update_summary(void)
{
    char text[160];
    if (!s.config_ok) {
        snprintf(text, sizeof(text), "Inverters: --");
    } else if (s.list.count == 0U) {
        snprintf(text, sizeof(text), "Inverters: 0 / %u",
                 (unsigned)PVDG_UI_MAX_INVERTER_CONFIGS);
    } else {
        snprintf(text, sizeof(text), "Inverter %u/%u · limit %u",
                 (unsigned)s.index + 1U, (unsigned)s.list.count,
                 (unsigned)PVDG_UI_MAX_INVERTER_CONFIGS);
    }
    pvdg_ui_label_set_if_changed(s.summary, text);
}

static void load_editor(void)
{
    if (!s.root) return;
    if (!s.config_ok || s.list.count == 0U || s.index >= s.list.count) {
        lv_textarea_set_text(s.name, "");
        lv_textarea_set_text(s.host, "");
        update_summary();
        load_profile_selection();
        update_navigation_state();
        return;
    }

    const pvdg_ui_inverter_config_t *inverter = &s.list.inverters[s.index];
    if (inverter->enabled) lv_obj_add_state(s.enabled, LV_STATE_CHECKED);
    else lv_obj_remove_state(s.enabled, LV_STATE_CHECKED);
    lv_textarea_set_text(s.name, inverter->name);
    lv_textarea_set_text(s.host, inverter->host);
    set_u32(s.port, inverter->port);
    set_u32(s.unit_id, inverter->unit_id);
    set_u32(s.timeout_ms, inverter->timeout_ms);
    set_double(s.rated_kw, inverter->rated_kw);
    update_summary();
    load_profile_selection();
    update_navigation_state();
}

static void set_form_disabled(bool disabled)
{
    lv_obj_t *controls[] = {
        s.enabled, s.name, s.host, s.port, s.unit_id, s.timeout_ms, s.rated_kw,
    };
    for (size_t i = 0U; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        if (!controls[i]) continue;
        if (disabled) lv_obj_add_state(controls[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(controls[i], LV_STATE_DISABLED);
    }
}

static void refresh_event(lv_event_t *event)
{
    (void)event;
    if (s.busy) return;
    keyboard_hide();
    if (s.cb.request_config) s.cb.request_config(s.cb.user);
    if (s.cb.request_profiles) s.cb.request_profiles(s.cb.user);
    if (s.cb.request_assignments) s.cb.request_assignments(s.cb.user);
}

static void save_event(lv_event_t *event)
{
    (void)event;
    if (!s.config_ok || !s.write || s.busy || !s.cb.submit_config) return;
    if (!commit_editor()) return;
    char error[192] = {0};
    if (pvdg_ui_inverter_list_validate(&s.list, error, sizeof(error)) !=
        PVDG_UI_INVERTER_CONFIG_OK) {
        pvdg_ui_label_set_if_changed(s.msg, error);
        return;
    }
    keyboard_hide();
    s.cb.submit_config(&s.list, s.cb.user);
}

static void previous_event(lv_event_t *event)
{
    (void)event;
    if (!s.config_ok || s.list.count == 0U || s.index == 0U) return;
    if (s.write && !commit_editor()) return;
    s.index--;
    load_editor();
}

static void next_event(lv_event_t *event)
{
    (void)event;
    if (!s.config_ok || s.list.count == 0U || s.index + 1U >= s.list.count) return;
    if (s.write && !commit_editor()) return;
    s.index++;
    load_editor();
}

static void add_event(lv_event_t *event)
{
    (void)event;
    if (!s.config_ok || !s.write || s.busy ||
        s.list.count >= PVDG_UI_MAX_INVERTER_CONFIGS) return;
    if (s.list.count > 0U && !commit_editor()) return;
    const uint8_t index = s.list.count;
    pvdg_ui_inverter_defaults(&s.list.inverters[index], index);
    s.list.count++;
    s.index = index;
    load_editor();
    pvdg_ui_label_set_if_changed(
        s.msg, "New inverter added at the next profile slot. Save to persist it.");
}

static void remove_event(lv_event_t *event)
{
    (void)event;
    if (!s.config_ok || !s.write || s.busy || s.list.count == 0U) return;
    if (s.index + 1U != s.list.count) {
        pvdg_ui_label_set_if_changed(
            s.msg,
            "Only the last inverter slot can be removed. Disable an earlier slot to preserve profile-index mapping.");
        return;
    }
    memset(&s.list.inverters[s.index], 0, sizeof(s.list.inverters[s.index]));
    s.list.count--;
    if (s.list.count == 0U) s.index = 0U;
    else s.index = (uint8_t)(s.list.count - 1U);
    load_editor();
    pvdg_ui_label_set_if_changed(
        s.msg, "Last inverter removed locally. Save to persist the new count.");
}

static void profile_changed_event(lv_event_t *event)
{
    (void)event;
    update_profile_detail();
}

static void apply_profile_event(lv_event_t *event)
{
    (void)event;
    if (!s.config_ok || !s.write || s.busy || !s.profiles_ok ||
        !s.assignments_ok || !s.cb.assign_profile || s.list.count == 0U ||
        s.index >= s.list.count || s.catalog.count == 0U) return;

    uint16_t selected = lv_dropdown_get_selected(s.profile);
    if (selected >= s.catalog.count || selected >= PVDG_UI_MAX_INVERTER_PROFILES) {
        pvdg_ui_label_set_if_changed(s.msg, "Select a valid compiled profile.");
        return;
    }
    const char *profile_id = s.catalog.profiles[selected].id;
    if (!profile_id[0]) {
        pvdg_ui_label_set_if_changed(s.msg, "Selected profile has no compiled ID.");
        return;
    }
    s.cb.assign_profile(s.index, profile_id, s.cb.user);
}

lv_obj_t *pvdg_ui_inverter_setup_create(
    lv_obj_t *parent,
    const pvdg_ui_inverter_setup_callbacks_t *callbacks)
{
    memset(&s, 0, sizeof(s));
    if (callbacks) s.cb = *callbacks;

    s.root = lv_obj_create(parent);
    pvdg_ui_style_root(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
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
    pvdg_ui_make_title(header, "Inverter Setup");
    s.badge = pvdg_ui_make_badge(
        header, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));

    lv_obj_t *actions = lv_obj_create(s.root);
    pvdg_ui_style_root(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 44);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 4, LV_PART_MAIN);
    s.summary = pvdg_ui_make_muted(actions, "Inverters: --");
    lv_obj_set_flex_grow(s.summary, 1);
    s.prev = button(actions, "<", previous_event, 42);
    s.next = button(actions, ">", next_event, 42);
    s.add = button(actions, "+", add_event, 42);
    s.remove = button(actions, "-", remove_event, 42);
    s.refresh = button(actions, "Refresh", refresh_event, 78);
    s.save = button(actions, "Save", save_event, 70);

    s.msg = pvdg_ui_make_muted(s.root, "Load inverter configuration before editing.");
    lv_obj_set_width(s.msg, LV_PCT(100));
    lv_label_set_long_mode(s.msg, LV_LABEL_LONG_DOT);

    lv_obj_t *form = pvdg_ui_make_card(s.root);
    lv_obj_set_width(form, LV_PCT(100));
    lv_obj_set_flex_grow(form, 1);
    lv_obj_set_layout(form, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(form, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_column(form, 12, LV_PART_MAIN);

    lv_obj_t *left = lv_obj_create(form);
    pvdg_ui_style_root(left);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 2, LV_PART_MAIN);

    lv_obj_t *right = lv_obj_create(form);
    pvdg_ui_style_root(right);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 2, LV_PART_MAIN);

    s.enabled = switch_field(left, "Enabled");
    s.name = text_field(left, "Name");
    s.host = text_field(left, "Host / IP");
    s.port = text_field(left, "TCP port");

    s.unit_id = text_field(right, "Unit ID");
    s.timeout_ms = text_field(right, "Timeout ms");
    s.rated_kw = text_field(right, "Rated kW");

    lv_obj_t *profile_row = row(right, "Profile");
    s.profile = lv_dropdown_create(profile_row);
    lv_dropdown_set_options(s.profile, "No compiled profiles");
    lv_obj_set_flex_grow(s.profile, 1);
    lv_obj_add_event_cb(s.profile, profile_changed_event, LV_EVENT_VALUE_CHANGED, NULL);
    s.apply_profile = button(right, "Apply Selected Profile", apply_profile_event, 215);
    s.profile_detail = pvdg_ui_make_muted(right, "Compiled profile catalog unavailable.");
    lv_obj_set_width(s.profile_detail, LV_PCT(100));
    lv_label_set_long_mode(s.profile_detail, LV_LABEL_LONG_WRAP);

    s.keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s.keyboard, 718, 190);
    lv_obj_align(s.keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);

    set_form_disabled(true);
    lv_obj_add_state(s.save, LV_STATE_DISABLED);
    lv_obj_add_state(s.add, LV_STATE_DISABLED);
    lv_obj_add_state(s.remove, LV_STATE_DISABLED);
    lv_obj_add_state(s.profile, LV_STATE_DISABLED);
    lv_obj_add_state(s.apply_profile, LV_STATE_DISABLED);
    update_navigation_state();
    refresh_event(NULL);
    return s.root;
}

void pvdg_ui_inverter_setup_set_config(const pvdg_ui_inverter_list_t *list)
{
    if (!s.root || !list) return;
    s.list = *list;
    s.config_ok = true;
    if (s.list.count == 0U) s.index = 0U;
    else if (s.index >= s.list.count) s.index = (uint8_t)(s.list.count - 1U);

    char error[192] = {0};
    const bool valid = pvdg_ui_inverter_list_validate(list, error, sizeof(error)) ==
                       PVDG_UI_INVERTER_CONFIG_OK;
    pvdg_ui_badge_set(s.badge, valid ? "Config valid" : "Config invalid",
                      lv_color_hex(valid ? PVDG_UI_COLOR_SUCCESS
                                         : PVDG_UI_COLOR_DANGER));
    if (!valid && error[0]) pvdg_ui_label_set_if_changed(s.msg, error);
    load_editor();
}

void pvdg_ui_inverter_setup_set_profiles(
    const pvdg_ui_inverter_profile_catalog_t *catalog)
{
    if (!s.root || !catalog) return;
    s.catalog = *catalog;
    s.profiles_ok = true;
    rebuild_profile_options();
    load_profile_selection();
}

void pvdg_ui_inverter_setup_set_assignments(
    const pvdg_ui_inverter_assignment_map_t *assignments)
{
    if (!s.root || !assignments) return;
    s.assignments = *assignments;
    s.assignments_ok = true;
    load_profile_selection();
}

void pvdg_ui_inverter_setup_apply_state(const pvdg_ui_inverter_setup_state_t *state)
{
    s.write = state && state->write_allowed;
    s.busy = state && state->busy;
    if (state && state->message) pvdg_ui_label_set_if_changed(s.msg, state->message);

    const bool can_edit = s.config_ok && s.write && !s.busy;
    set_form_disabled(!can_edit);
    if (can_edit) {
        lv_obj_remove_state(s.save, LV_STATE_DISABLED);
        if (s.list.count < PVDG_UI_MAX_INVERTER_CONFIGS) {
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

    const bool can_assign = can_edit && s.profiles_ok && s.assignments_ok &&
                            s.catalog.count > 0U && s.list.count > 0U &&
                            s.cb.assign_profile != NULL;
    if (can_assign) {
        lv_obj_remove_state(s.profile, LV_STATE_DISABLED);
        lv_obj_remove_state(s.apply_profile, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s.profile, LV_STATE_DISABLED);
        lv_obj_add_state(s.apply_profile, LV_STATE_DISABLED);
    }
    update_navigation_state();
}
