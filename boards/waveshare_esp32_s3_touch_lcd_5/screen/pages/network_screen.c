#include "network_screen.h"

#include <stdio.h>
#include <string.h>

#include "screen_widgets.h"

#define NETWORK_FORM_CONTROL_WIDTH 330
#define NETWORK_KEYBOARD_HEIGHT 190
#define NETWORK_POLL_PERIOD_MS 1500U

typedef struct {
    lv_obj_t *root;
    lv_obj_t *body;
    lv_obj_t *message;
    lv_obj_t *keyboard;
    lv_obj_t *credential;
    lv_obj_t *status_label;
    lv_obj_t *status_icon;
    lv_obj_t *ssid_field;
    lv_obj_t *password_field;
    lv_obj_t *list;
    lv_timer_t *poll_timer;
    network_screen_backend_t backend;
    network_screen_status_t status;
    network_screen_scan_t scan;
    bool backend_set;
    bool unlocked;
} network_ui_t;

static network_ui_t s_ui;

static void render(void);
static void render_locked(void);
static void render_unlocked(void);
static void rebuild_status(void);
static void rebuild_list(void);

static void render_async(void *data)
{
    (void)data;
    render();
}

static void queue_render(void)
{
    (void)lv_async_call(render_async, NULL);
}

static void make_fixed(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void set_message(const char *text, bool good)
{
    if (!s_ui.message) return;
    lv_label_set_text(s_ui.message, text ? text : "");
    lv_obj_set_style_text_color(s_ui.message,
                                lv_color_hex(good ? 0x62D28F : 0xF07178),
                                LV_PART_MAIN);
}

static void keyboard_hide(void)
{
    if (!s_ui.keyboard) return;
    lv_keyboard_set_textarea(s_ui.keyboard, NULL);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) keyboard_hide();
}

static void textarea_focus(lv_event_t *event)
{
    if (!s_ui.keyboard) return;
    lv_obj_t *textarea = lv_event_get_target_obj(event);
    lv_keyboard_set_textarea(s_ui.keyboard, textarea);
    lv_obj_remove_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.keyboard);
}

static lv_obj_t *row(lv_obj_t *parent, const char *label)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_layout(item, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *caption = lv_label_create(item);
    lv_label_set_text(caption, label);
    lv_obj_set_width(caption, 150);
    lv_obj_set_style_text_color(caption, lv_color_hex(0xC7D0DA), LV_PART_MAIN);
    return item;
}

static lv_obj_t *field(lv_obj_t *parent, const char *label, const char *value, bool password)
{
    lv_obj_t *item = row(parent, label);
    lv_obj_t *input = lv_textarea_create(item);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_password_mode(input, password);
    lv_textarea_set_text(input, value ? value : "");
    lv_obj_set_width(input, NETWORK_FORM_CONTROL_WIDTH);
    lv_obj_add_event_cb(input, textarea_focus, LV_EVENT_FOCUSED, NULL);
    return input;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    make_fixed(obj);
    lv_obj_set_height(obj, 38);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return obj;
}

static void heading(lv_obj_t *parent, const char *title, const char *detail)
{
    lv_obj_t *h = lv_label_create(parent);
    lv_label_set_text(h, title);
    lv_obj_set_style_text_color(h, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    lv_obj_t *d = lv_label_create(parent);
    lv_label_set_text(d, detail);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(d, LV_PCT(100));
    lv_obj_set_style_text_color(d, lv_color_hex(0x9EADBF), LV_PART_MAIN);
}

static lv_obj_t *form_container(void)
{
    lv_obj_t *form = lv_obj_create(s_ui.body);
    lv_obj_set_size(form, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(form, lv_color_hex(0x101720), LV_PART_MAIN);
    lv_obj_set_style_border_width(form, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(form, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(form, 10, LV_PART_MAIN);
    lv_obj_set_layout(form, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(form, 7, LV_PART_MAIN);
    lv_obj_set_scroll_dir(form, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(form, LV_SCROLLBAR_MODE_AUTO);
    return form;
}

static void unlock_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.unlock || !s_ui.credential) return;
    uint32_t retry = 0U;
    bool setup = false;
    const network_screen_auth_result_t result =
        s_ui.backend.unlock(s_ui.backend.context, lv_textarea_get_text(s_ui.credential),
                            &retry, &setup);
    lv_textarea_set_text(s_ui.credential, "");
    keyboard_hide();
    if (result == NETWORK_SCREEN_AUTH_OK) {
        s_ui.unlocked = true;
        set_message("Engineering unlocked for network settings.", true);
        if (s_ui.backend.read_status) {
            (void)s_ui.backend.read_status(s_ui.backend.context, &s_ui.status);
        }
        if (s_ui.backend.request_scan) (void)s_ui.backend.request_scan(s_ui.backend.context);
        queue_render();
    } else if (result == NETWORK_SCREEN_AUTH_LOCKED) {
        char text[96];
        snprintf(text, sizeof(text), "Engineering login locked. Retry in %lu s.",
                 (unsigned long)((retry + 999U) / 1000U));
        set_message(text, false);
    } else {
        set_message("Engineering credential rejected.", false);
    }
}

static void lock_clicked(lv_event_t *event)
{
    (void)event;
    keyboard_hide();
    if (s_ui.backend.lock) s_ui.backend.lock(s_ui.backend.context);
    s_ui.unlocked = false;
    queue_render();
}

static void scan_clicked(lv_event_t *event)
{
    (void)event;
    if (s_ui.backend.request_scan) (void)s_ui.backend.request_scan(s_ui.backend.context);
    set_message("Scanning for networks...", true);
}

static void select_network(lv_event_t *event)
{
    const uint16_t index = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= s_ui.scan.count || !s_ui.ssid_field) return;
    lv_textarea_set_text(s_ui.ssid_field, s_ui.scan.items[index].ssid);
    if (s_ui.password_field) lv_textarea_set_text(s_ui.password_field, "");
}

static void connect_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.connect || !s_ui.ssid_field) return;
    const char *ssid = lv_textarea_get_text(s_ui.ssid_field);
    const char *password = s_ui.password_field ? lv_textarea_get_text(s_ui.password_field) : "";
    if (!ssid || !ssid[0]) {
        set_message("Enter or select a network SSID first.", false);
        return;
    }
    network_screen_action_result_t result = {0};
    (void)s_ui.backend.connect(s_ui.backend.context, ssid, password, &result);
    set_message(result.message[0] ? result.message
                                   : (result.ok ? "Saved" : "Connect failed"), result.ok);
    if (result.ok && result.restart_required) {
        set_message("Wi-Fi saved. Tap Restart to apply and join the new network.", true);
    }
}

static void restart_clicked(lv_event_t *event)
{
    (void)event;
    if (!s_ui.backend.restart_controller) return;
    network_screen_action_result_t result = {0};
    (void)s_ui.backend.restart_controller(s_ui.backend.context, &result);
    set_message(result.message[0] ? result.message : "Restart request failed.", result.ok);
}

static const char *auth_mode_tag(bool secured)
{
    return secured ? "secured" : "open";
}

static void rebuild_list(void)
{
    if (!s_ui.list) return;
    lv_obj_clean(s_ui.list);
    if (s_ui.scan.count == 0U) {
        lv_obj_t *empty = lv_label_create(s_ui.list);
        lv_label_set_text(empty, s_ui.scan.scanning
                                      ? "Scanning..."
                                      : "No networks found yet. Tap Scan.");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9EADBF), LV_PART_MAIN);
        return;
    }
    for (uint16_t i = 0U; i < s_ui.scan.count; ++i) {
        const network_screen_ap_t *ap = &s_ui.scan.items[i];
        lv_obj_t *item = lv_obj_create(s_ui.list);
        lv_obj_remove_style_all(item);
        lv_obj_set_width(item, LV_PCT(100));
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        char text[80];
        snprintf(text, sizeof(text), "%s%s  %d dBm  %s",
                 ap->ssid[0] ? ap->ssid : "(hidden)",
                 ap->connected ? "  [connected]" : (ap->configured ? "  [saved]" : ""),
                 (int)ap->rssi, auth_mode_tag(ap->secured));
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0xF2F6FA), LV_PART_MAIN);

        lv_obj_t *pick = lv_button_create(item);
        make_fixed(pick);
        lv_obj_set_height(pick, 34);
        lv_obj_add_event_cb(pick, select_network, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *pick_label = lv_label_create(pick);
        lv_label_set_text(pick_label, "Select");
        lv_obj_center(pick_label);
    }
}

static void rebuild_status(void)
{
    if (!s_ui.status_label) return;
    char text[192];
    if (!s_ui.status.valid) {
        snprintf(text, sizeof(text), "Status unavailable");
    } else if (s_ui.status.fallback_ap_active) {
        snprintf(text, sizeof(text), "Setup access point active: %s (no configured network in range)",
                 s_ui.status.ssid);
    } else if (s_ui.status.network_online) {
        snprintf(text, sizeof(text), "Connected: %s  IP %s",
                 s_ui.status.ssid, s_ui.status.ip[0] ? s_ui.status.ip : "--");
    } else {
        snprintf(text, sizeof(text), "Disconnected");
    }
    lv_label_set_text(s_ui.status_label, text);
    screen_ui_apply_wifi_indicator(s_ui.status_icon, s_ui.status.network_online, s_ui.status.rssi);
}

static void render_locked(void)
{
    lv_obj_t *form = form_container();
    heading(form, "Network · Engineering locked",
            "Use the same Engineering password as the protected web workspace. This page scans "
            "for Wi-Fi networks and lets you connect the panel directly from its own screen, "
            "without a phone, laptop or the ZLAN/EM500 bench network.");
    s_ui.credential = field(form, "Engineering credential", "", true);
    button(form, "Unlock network settings", unlock_clicked);
}

static void render_unlocked(void)
{
    lv_obj_t *form = form_container();

    lv_obj_t *top = lv_obj_create(form);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_layout(top, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_ui.status_icon = lv_label_create(top);
    s_ui.status_label = lv_label_create(top);
    lv_obj_set_style_text_color(s_ui.status_label, lv_color_hex(0xF2F6FA), LV_PART_MAIN);
    lv_obj_set_flex_grow(s_ui.status_label, 1);
    button(top, "Lock", lock_clicked);
    rebuild_status();

    button(form, "Scan for networks", scan_clicked);

    s_ui.list = lv_obj_create(form);
    lv_obj_remove_style_all(s_ui.list);
    lv_obj_set_width(s_ui.list, LV_PCT(100));
    lv_obj_set_height(s_ui.list, 160);
    lv_obj_set_layout(s_ui.list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.list, 4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_ui.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.list, LV_SCROLLBAR_MODE_AUTO);
    rebuild_list();

    heading(form, "Connect",
            "Select a network above or type its SSID directly (needed for a hidden network), "
            "enter its password, then Connect. Saving requires a restart to take effect -- "
            "automatic control stays disabled until it comes back up and re-qualifies, the same "
            "as every other commissioning change on this panel.");
    s_ui.ssid_field = field(form, "SSID", "", false);
    s_ui.password_field = field(form, "Password", "", true);
    button(form, "Connect", connect_clicked);
    button(form, "Restart to apply", restart_clicked);
}

static void render(void)
{
    if (!s_ui.root || !s_ui.body) return;
    keyboard_hide();
    lv_obj_clean(s_ui.body);
    s_ui.credential = NULL;
    s_ui.status_label = NULL;
    s_ui.status_icon = NULL;
    s_ui.list = NULL;
    s_ui.ssid_field = NULL;
    s_ui.password_field = NULL;
    if (!s_ui.backend_set || !s_ui.unlocked) render_locked();
    else render_unlocked();
}

static void poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ui.root || !s_ui.backend_set || !s_ui.unlocked) return;
    if (lv_obj_has_flag(s_ui.root, LV_OBJ_FLAG_HIDDEN)) return;

    bool changed = false;
    if (s_ui.backend.read_status) {
        network_screen_status_t next = {0};
        if (s_ui.backend.read_status(s_ui.backend.context, &next) &&
            memcmp(&next, &s_ui.status, sizeof(next)) != 0) {
            s_ui.status = next;
            changed = true;
        }
    }
    if (s_ui.backend.read_scan) {
        network_screen_scan_t next = {0};
        if (s_ui.backend.read_scan(s_ui.backend.context, &next) &&
            memcmp(&next, &s_ui.scan, sizeof(next)) != 0) {
            s_ui.scan = next;
            rebuild_list();
        }
    }
    if (changed) rebuild_status();
}

lv_obj_t *network_screen_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.root = lv_obj_create(parent ? parent : lv_screen_active());
    make_fixed(s_ui.root);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ui.root, lv_color_hex(0x0B1017), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.root, 8, LV_PART_MAIN);
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 5, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(s_ui.root);
    lv_label_set_text(title, "Network / Wi-Fi Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F6FA), LV_PART_MAIN);

    s_ui.body = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.body);
    make_fixed(s_ui.body);
    lv_obj_set_width(s_ui.body, LV_PCT(100));
    lv_obj_set_flex_grow(s_ui.body, 1);

    s_ui.message = lv_label_create(s_ui.root);
    lv_label_set_text(s_ui.message, "");
    lv_label_set_long_mode(s_ui.message, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_ui.message, LV_PCT(100));
    lv_obj_set_height(s_ui.message, 22);

    s_ui.keyboard = lv_keyboard_create(s_ui.root);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(s_ui.keyboard, LV_PCT(100), NETWORK_KEYBOARD_HEIGHT);
    lv_obj_align(s_ui.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_ui.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ui.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_ui.keyboard, LV_OBJ_FLAG_HIDDEN);

    s_ui.poll_timer = lv_timer_create(poll_timer_cb, NETWORK_POLL_PERIOD_MS, NULL);

    render();
    return s_ui.root;
}

void network_screen_set_backend(const network_screen_backend_t *backend)
{
    if (!backend) {
        memset(&s_ui.backend, 0, sizeof(s_ui.backend));
        memset(&s_ui.status, 0, sizeof(s_ui.status));
        memset(&s_ui.scan, 0, sizeof(s_ui.scan));
        s_ui.backend_set = false;
        s_ui.unlocked = false;
    } else {
        s_ui.backend = *backend;
        s_ui.backend_set = true;
    }
    queue_render();
}

void network_screen_apply_status(const network_screen_status_t *status)
{
    if (!status || !s_ui.root || !s_ui.unlocked) return;
    if (memcmp(status, &s_ui.status, sizeof(*status)) == 0) return;
    s_ui.status = *status;
    rebuild_status();
}

void network_screen_show_unavailable(void)
{
    if (!s_ui.unlocked) return;
    set_message("Network backend unavailable.", false);
}
