#include "pvdg_ui_engineering.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pvdg_ui_theme.h"

extern void pvdg_ui_engineering_host_refresh(pvdg_ui_engineering_state_t *state)
    __attribute__((weak));
extern void pvdg_ui_engineering_host_submit_password(const char *password, void *user)
    __attribute__((weak));
extern void pvdg_ui_engineering_host_logout(void *user) __attribute__((weak));
extern void pvdg_ui_engineering_host_open_section(pvdg_ui_engineering_section_t section,
                                                  void *user)
    __attribute__((weak));

typedef struct {
    lv_obj_t *root;
    lv_obj_t *badge;
    lv_obj_t *message;
    lv_obj_t *password;
    lv_obj_t *auth_button;
    lv_obj_t *auth_caption;
    lv_obj_t *session;
    lv_obj_t *keyboard;
    lv_obj_t *section[PVDG_UI_ENGINEERING_SECTION_COUNT];
    pvdg_ui_engineering_callbacks_t callbacks;
    pvdg_ui_engineering_state_t state;
} engineering_ui_t;

static engineering_ui_t s;

static const char *const s_section_names[PVDG_UI_ENGINEERING_SECTION_COUNT] = {
    "Source Setup",
    "Meter Setup",
    "Inverter Setup",
    "Export Control",
    "Safety Limits",
    "Network",
    "Diagnostics",
    "Configuration",
};

static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text ? text : "");
    pvdg_ui_style_label(obj, lv_color_hex(color), PVDG_UI_FONT_BODY);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback,
                        void *user)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_height(obj, PVDG_UI_TOUCH_MIN);
    lv_obj_t *caption = label(obj, text, PVDG_UI_COLOR_TEXT);
    lv_obj_center(caption);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, user);
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

static void password_event(lv_event_t *event)
{
    (void)event;
    if (!s.keyboard || !s.password) return;
    lv_keyboard_set_textarea(s.keyboard, s.password);
    lv_obj_remove_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s.keyboard);
}

static void refresh_from_host(void)
{
    if (!pvdg_ui_engineering_host_refresh) return;
    pvdg_ui_engineering_state_t next = {0};
    pvdg_ui_engineering_host_refresh(&next);
    s.state = next;
}

static void refresh_event(lv_event_t *event)
{
    (void)event;
    if (s.callbacks.request_session_refresh) {
        s.callbacks.request_session_refresh(s.callbacks.user);
    }
    refresh_from_host();
    pvdg_ui_engineering_apply(NULL, &s.state);
}

static void auth_event(lv_event_t *event)
{
    (void)event;
    if (s.state.locked_out) return;

    if (s.state.authenticated) {
        if (s.callbacks.request_logout) s.callbacks.request_logout(s.callbacks.user);
        else if (pvdg_ui_engineering_host_logout) pvdg_ui_engineering_host_logout(NULL);
        lv_textarea_set_text(s.password, "");
        keyboard_hide();
    } else {
        const char *password = lv_textarea_get_text(s.password);
        if (s.callbacks.submit_password) {
            s.callbacks.submit_password(password ? password : "", s.callbacks.user);
        } else if (pvdg_ui_engineering_host_submit_password) {
            pvdg_ui_engineering_host_submit_password(password ? password : "", NULL);
        }
        lv_textarea_set_text(s.password, "");
        keyboard_hide();
    }

    refresh_from_host();
    pvdg_ui_engineering_apply(NULL, &s.state);
}

static void section_event(lv_event_t *event)
{
    const intptr_t index = (intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= PVDG_UI_ENGINEERING_SECTION_COUNT) return;
    const pvdg_ui_engineering_section_t section = (pvdg_ui_engineering_section_t)index;
    if (s.callbacks.open_section) s.callbacks.open_section(section, s.callbacks.user);
    else if (pvdg_ui_engineering_host_open_section) {
        pvdg_ui_engineering_host_open_section(section, NULL);
    }
}

static void create_keyboard(void)
{
    s.keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s.keyboard, 718, 190);
    lv_obj_align(s.keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s.keyboard, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *pvdg_ui_engineering_create(lv_obj_t *parent,
                                      const pvdg_ui_engineering_callbacks_t *callbacks)
{
    memset(&s, 0, sizeof(s));
    if (callbacks) s.callbacks = *callbacks;

    s.root = lv_obj_create(parent);
    pvdg_ui_style_root(s.root);
    lv_obj_set_size(s.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s.root, PVDG_UI_GAP_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s.root, PVDG_UI_GAP_XS, LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(s.root);
    pvdg_ui_style_root(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 38);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    pvdg_ui_make_title(header, "Engineering");
    s.badge = pvdg_ui_make_badge(header, "Protected", lv_color_hex(PVDG_UI_COLOR_WARNING));

    lv_obj_t *auth = pvdg_ui_make_card(s.root);
    lv_obj_set_width(auth, LV_PCT(100));
    lv_obj_set_height(auth, 112);
    lv_obj_set_layout(auth, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(auth, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(auth, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(auth, 3, LV_PART_MAIN);

    lv_obj_t *auth_row = lv_obj_create(auth);
    pvdg_ui_style_root(auth_row);
    lv_obj_set_width(auth_row, LV_PCT(100));
    lv_obj_set_height(auth_row, 44);
    lv_obj_set_layout(auth_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(auth_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(auth_row, 5, LV_PART_MAIN);

    s.password = lv_textarea_create(auth_row);
    lv_textarea_set_one_line(s.password, true);
    lv_textarea_set_password_mode(s.password, true);
    lv_textarea_set_placeholder_text(s.password, "Engineering password");
    lv_obj_set_flex_grow(s.password, 1);
    lv_obj_set_height(s.password, 40);
    lv_obj_add_event_cb(s.password, password_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s.password, password_event, LV_EVENT_CLICKED, NULL);

    s.auth_button = button(auth_row, "Authenticate", auth_event, NULL);
    s.auth_caption = lv_obj_get_child(s.auth_button, 0);
    lv_obj_t *refresh = button(auth_row, "Refresh", refresh_event, NULL);
    lv_obj_set_width(s.auth_button, 130);
    lv_obj_set_width(refresh, 95);

    s.session = label(auth, "Session: locked", PVDG_UI_COLOR_MUTED);
    s.message = label(auth, "Engineering authentication required for write actions.",
                      PVDG_UI_COLOR_MUTED);
    lv_obj_set_width(s.message, LV_PCT(100));
    lv_label_set_long_mode(s.message, LV_LABEL_LONG_DOT);

    lv_obj_t *grid = lv_obj_create(s.root);
    pvdg_ui_style_root(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    static int32_t columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                               LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, columns, rows);
    lv_obj_set_style_pad_row(grid, PVDG_UI_GAP_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, PVDG_UI_GAP_SM, LV_PART_MAIN);

    for (int i = 0; i < PVDG_UI_ENGINEERING_SECTION_COUNT; ++i) {
        s.section[i] = button(grid, s_section_names[i], section_event, (void *)(intptr_t)i);
        lv_obj_set_grid_cell(s.section[i], LV_GRID_ALIGN_STRETCH, i % 4, 1,
                             LV_GRID_ALIGN_STRETCH, i / 4, 1);
    }

    create_keyboard();
    refresh_from_host();
    pvdg_ui_engineering_apply(NULL, &s.state);
    return s.root;
}

void pvdg_ui_engineering_apply(const pvdg_ui_model_t *model,
                               const pvdg_ui_engineering_state_t *state)
{
    (void)model;
    if (!s.root) return;

    if (pvdg_ui_engineering_host_refresh) {
        pvdg_ui_engineering_state_t host = {0};
        pvdg_ui_engineering_host_refresh(&host);
        s.state = host;
    } else if (state) {
        s.state = *state;
    }
    const pvdg_ui_engineering_state_t *st = &s.state;

    const uint32_t color = st->locked_out ? PVDG_UI_COLOR_DANGER :
                           st->authenticated ? PVDG_UI_COLOR_SUCCESS :
                           PVDG_UI_COLOR_WARNING;
    pvdg_ui_badge_set(s.badge,
                      st->locked_out ? "Locked out" :
                      st->authenticated ? "Authenticated" :
                      st->setup_required ? "Setup required" : "Protected",
                      lv_color_hex(color));

    pvdg_ui_label_set_if_changed(s.auth_caption,
        st->authenticated ? "Logout" : "Authenticate");
    if (st->authenticated) {
        lv_obj_add_state(s.password, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s.password, LV_STATE_DISABLED);
    }
    if (st->locked_out) lv_obj_add_state(s.auth_button, LV_STATE_DISABLED);
    else lv_obj_remove_state(s.auth_button, LV_STATE_DISABLED);

    char session[96];
    if (st->locked_out) {
        snprintf(session, sizeof(session), "Retry in %lu s",
                 (unsigned long)st->lockout_remaining_seconds);
    } else if (st->authenticated) {
        snprintf(session, sizeof(session), "Session: %lu s remaining",
                 (unsigned long)st->session_remaining_seconds);
    } else {
        snprintf(session, sizeof(session), "Session: locked");
    }
    pvdg_ui_label_set_if_changed(s.session, session);
    pvdg_ui_label_set_if_changed(s.message,
        st->message && st->message[0] ? st->message :
        st->setup_required
            ? "Permanent Engineering password setup is required before panel writes."
            : "Authenticate to enable Engineering write sections.");

    const bool writable = st->authenticated && st->write_contract_verified;
    for (int i = 0; i < PVDG_UI_ENGINEERING_SECTION_COUNT; ++i) {
        /* Diagnostics is deliberately read-only and always available. Network
         * remains accessible from the top-bar Wi-Fi shortcut even while locked. */
        const bool read_only = i == PVDG_UI_ENGINEERING_DIAGNOSTICS;
        if (read_only || writable) lv_obj_remove_state(s.section[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s.section[i], LV_STATE_DISABLED);
    }
}
