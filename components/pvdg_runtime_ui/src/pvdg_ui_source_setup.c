#include "pvdg_ui_source_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvdg_ui_theme.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *badge;
    lv_obj_t *msg;
    lv_obj_t *refresh;
    lv_obj_t *save;
    lv_obj_t *grid;
    lv_obj_t *gen;
    lv_obj_t *poll;
    lv_obj_t *stale;
    lv_obj_t *keyboard;
    pvdg_ui_source_config_t cfg;
    pvdg_ui_source_setup_callbacks_t cb;
    bool available;
    bool write;
    bool busy;
} ui_t;

static ui_t s;

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

static lv_obj_t *field(lv_obj_t *parent, const char *name)
{
    lv_obj_t *row = lv_obj_create(parent);
    pvdg_ui_style_root(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 44);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_t *label = pvdg_ui_make_muted(row, name);
    lv_obj_set_width(label, 260);
    lv_obj_t *input = lv_textarea_create(row);
    lv_textarea_set_one_line(input, true);
    lv_obj_set_flex_grow(input, 1);
    lv_obj_add_event_cb(input, field_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(input, field_event, LV_EVENT_CLICKED, NULL);
    return input;
}

static void refresh_event(lv_event_t *event)
{
    (void)event;
    if (!s.busy && s.cb.request_config) s.cb.request_config(s.cb.user);
}

static void save_event(lv_event_t *event)
{
    (void)event;
    if (!s.available || !s.write || s.busy || !s.cb.submit_config) return;
    pvdg_ui_source_config_t candidate = s.cfg;
    const char *poll = lv_textarea_get_text(s.poll);
    const char *stale = lv_textarea_get_text(s.stale);
    if (poll && poll[0]) candidate.evidence_poll_interval_ms = (uint32_t)strtoul(poll, NULL, 0);
    if (stale && stale[0]) candidate.evidence_stale_timeout_ms = (uint32_t)strtoul(stale, NULL, 0);
    char error[192] = {0};
    if (pvdg_ui_source_config_validate(&candidate, error, sizeof(error)) != PVDG_UI_SOURCE_CONFIG_OK) {
        pvdg_ui_label_set_if_changed(s.msg, error);
        return;
    }
    keyboard_hide();
    s.cb.submit_config(&candidate, s.cb.user);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_height(obj, PVDG_UI_TOUCH_MIN);
    lv_obj_t *label = pvdg_ui_make_muted(obj, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    return obj;
}

lv_obj_t *pvdg_ui_source_setup_create(lv_obj_t *parent,
                                      const pvdg_ui_source_setup_callbacks_t *callbacks)
{
    memset(&s, 0, sizeof(s));
    if (callbacks) s.cb = *callbacks;
    s.root = lv_obj_create(parent);
    pvdg_ui_style_root(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s.root, PVDG_UI_GAP_MD, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s.root, PVDG_UI_GAP_SM, LV_PART_MAIN);

    pvdg_ui_make_title(s.root, "Source Setup");
    s.badge = pvdg_ui_make_badge(s.root, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));
    s.msg = pvdg_ui_make_muted(s.root, "Load current source configuration.");
    s.grid = pvdg_ui_make_muted(s.root, "Grid evidence: --");
    s.gen = pvdg_ui_make_muted(s.root, "Generator evidence: --");
    s.poll = field(s.root, "Evidence poll interval (ms)");
    s.stale = field(s.root, "Evidence stale timeout (ms)");

    lv_obj_t *actions = lv_obj_create(s.root);
    pvdg_ui_style_root(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 50);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 6, LV_PART_MAIN);
    s.refresh = button(actions, "Refresh", refresh_event);
    s.save = button(actions, "Validate + Save", save_event);

    /* Keep the keyboard inside the page so changing Engineering sections or
     * closing the overlay cannot leave a global top-layer object intercepting
     * touches on the next page. FLOATING keeps it out of the flex layout. */
    s.keyboard = lv_keyboard_create(s.root);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s.keyboard, 718, 190);
    lv_obj_align(s.keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);

    if (s.cb.request_config) s.cb.request_config(s.cb.user);
    return s.root;
}

void pvdg_ui_source_setup_set_config(const pvdg_ui_source_config_t *config)
{
    if (!s.root || !config) return;
    s.cfg = *config;
    s.available = true;
    char text[96];
    snprintf(text, sizeof(text), "Grid evidence: %s",
             pvdg_ui_source_grid_evidence_complete(config) ? "configured" : "not configured");
    pvdg_ui_label_set_if_changed(s.grid, text);
    unsigned generator_count = 0U;
    for (uint8_t i = 0U; i < PVDG_UI_SOURCE_MAX_GENERATORS; ++i) {
        if (pvdg_ui_source_generator_evidence_complete(config, i)) generator_count++;
    }
    snprintf(text, sizeof(text), "Generator evidence channels: %u / 3", generator_count);
    pvdg_ui_label_set_if_changed(s.gen, text);
    snprintf(text, sizeof(text), "%lu", (unsigned long)config->evidence_poll_interval_ms);
    lv_textarea_set_text(s.poll, text);
    snprintf(text, sizeof(text), "%lu", (unsigned long)config->evidence_stale_timeout_ms);
    lv_textarea_set_text(s.stale, text);
    pvdg_ui_badge_set(s.badge, "Schema 4 loaded", lv_color_hex(PVDG_UI_COLOR_SUCCESS));
}

void pvdg_ui_source_setup_apply_state(const pvdg_ui_source_setup_state_t *state)
{
    s.write = state && state->write_allowed;
    s.busy = state && state->busy;
    if (state && state->message) pvdg_ui_label_set_if_changed(s.msg, state->message);
    if (!s.available || !s.write || s.busy) lv_obj_add_state(s.save, LV_STATE_DISABLED);
    else lv_obj_remove_state(s.save, LV_STATE_DISABLED);
}
