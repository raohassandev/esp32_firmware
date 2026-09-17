#include "pvdg_ui_wifi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pvdg_ui_theme.h"

/* Optional product-host hooks. The reusable runtime UI keeps no direct
 * dependency on Product Core; the Waveshare product supplies these symbols. */
extern void pvdg_ui_wifi_host_refresh(pvdg_ui_model_t *model,
                                      pvdg_ui_wifi_scan_t *scan)
    __attribute__((weak));
extern void pvdg_ui_wifi_host_request_scan(void *user) __attribute__((weak));
extern void pvdg_ui_wifi_host_request_reconnect(void *user) __attribute__((weak));
extern bool pvdg_ui_wifi_host_load_config(pvdg_ui_wifi_config_t *config)
    __attribute__((weak));
extern void pvdg_ui_wifi_host_submit_config(const pvdg_ui_wifi_config_t *config,
                                            bool primary_changed,
                                            void *user)
    __attribute__((weak));

typedef struct {
    lv_obj_t *root;
    lv_obj_t *left;
    lv_obj_t *badge;
    lv_obj_t *ssid;
    lv_obj_t *ip;
    lv_obj_t *rssi;
    lv_obj_t *scan;
    lv_obj_t *reconnect;
    lv_obj_t *scan_spinner;
    lv_obj_t *scan_empty;
    lv_obj_t *selected;
    lv_obj_t *password;
    lv_obj_t *password_toggle;
    lv_obj_t *password_toggle_label;
    lv_obj_t *save;
    lv_obj_t *msg;
    lv_obj_t *keyboard;
    lv_obj_t *rows[PVDG_UI_WIFI_MAX_NETWORKS];
    lv_obj_t *row_name[PVDG_UI_WIFI_MAX_NETWORKS];
    lv_obj_t *row_meta[PVDG_UI_WIFI_MAX_NETWORKS];
    pvdg_ui_wifi_callbacks_t cb;
    pvdg_ui_wifi_config_t config;
    char selected_ssid[PVDG_UI_TEXT_MEDIUM];
    bool secure[PVDG_UI_WIFI_MAX_NETWORKS];
    bool supported[PVDG_UI_WIFI_MAX_NETWORKS];
    bool selected_secure;
    bool password_visible;
    bool config_available;
    bool write_authorized;
    bool busy;
    bool scan_visual_running;
    bool rendered_scan_valid;
    uint32_t rendered_scan_signature;
} wifi_ui_t;

static wifi_ui_t s;

/* pvdg_ui_model_t intentionally carries the full runtime inverter surface and
 * is several kilobytes. Do not place a temporary copy on the LVGL task stack:
 * Wi-Fi rendering nests into the product host refresh, which also needs scan
 * and configuration scratch space. Keep the single UI-owned refresh snapshot
 * in module storage, matching the screen runtime's stack-safety contract. */
static pvdg_ui_model_t s_host_model;
static pvdg_ui_wifi_scan_t s_host_scan;

static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text ? text : "");
    pvdg_ui_style_label(obj, lv_color_hex(color), PVDG_UI_FONT_BODY);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_height(obj, PVDG_UI_TOUCH_MIN);
    lv_obj_t *caption = label(obj, text, PVDG_UI_COLOR_TEXT);
    lv_obj_center(caption);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    return obj;
}

static void set_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) return;
    const bool is_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (hidden && !is_hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else if (!hidden && is_hidden) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void set_scan_indicator(bool running)
{
    if (s.scan_visual_running == running) return;
    s.scan_visual_running = running;
    set_hidden(s.scan_spinner, !running);
}

static void keyboard_hide(void)
{
    if (!s.keyboard) return;
    lv_keyboard_set_textarea(s.keyboard, NULL);
    set_hidden(s.keyboard, true);
    if (s.root) lv_obj_update_layout(s.root);
}

static void keyboard_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        keyboard_hide();
        if (s.save) lv_obj_scroll_to_view(s.save, LV_ANIM_OFF);
    } else if (code == LV_EVENT_CANCEL) {
        keyboard_hide();
        if (s.left) lv_obj_scroll_to_y(s.left, 0, LV_ANIM_OFF);
    }
}

static void password_event(lv_event_t *event)
{
    (void)event;
    if (!s.keyboard || !s.password || !s.selected_secure) return;
    lv_keyboard_set_textarea(s.keyboard, s.password);
    set_hidden(s.keyboard, false);
    if (s.root) lv_obj_update_layout(s.root);
    lv_obj_scroll_to_view(s.password, LV_ANIM_OFF);
}

static void password_visibility_apply(void)
{
    if (!s.password) return;
    lv_textarea_set_password_mode(s.password, !s.password_visible);
    if (s.password_toggle_label) {
        pvdg_ui_label_set_if_changed(s.password_toggle_label,
                                     s.password_visible ? "Hide" : "Show");
    }
}

static void password_toggle_event(lv_event_t *event)
{
    (void)event;
    if (!s.password || !s.selected_secure) return;
    s.password_visible = !s.password_visible;
    password_visibility_apply();
    lv_obj_scroll_to_view(s.password, LV_ANIM_OFF);
}

static void set_password_controls(bool secure)
{
    if (!s.password || !s.password_toggle) return;
    if (secure) {
        lv_obj_remove_state(s.password, LV_STATE_DISABLED);
        lv_obj_remove_state(s.password_toggle, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s.password, LV_STATE_DISABLED);
        lv_obj_add_state(s.password_toggle, LV_STATE_DISABLED);
        s.password_visible = false;
        password_visibility_apply();
        keyboard_hide();
    }
}

static void request_scan_from_host(void)
{
    if (s.cb.request_scan) s.cb.request_scan(s.cb.user);
    else if (pvdg_ui_wifi_host_request_scan) pvdg_ui_wifi_host_request_scan(NULL);
}

static void scan_event(lv_event_t *event)
{
    (void)event;
    if (s.busy) return;

    /* Give immediate operator feedback; the host snapshot will keep this state
     * authoritative on the next refresh. Existing rows intentionally remain
     * visible while a new scan is running so the layout does not thrash. */
    set_scan_indicator(true);
    pvdg_ui_label_set_if_changed(s.msg, "Scanning for nearby Wi-Fi networks...");
    request_scan_from_host();
}

static void reconnect_event(lv_event_t *event)
{
    (void)event;
    if (s.busy) return;
    keyboard_hide();
    if (s.cb.request_reconnect) s.cb.request_reconnect(s.cb.user);
    else if (pvdg_ui_wifi_host_request_reconnect) pvdg_ui_wifi_host_request_reconnect(NULL);
}

static void update_save_state(void)
{
    if (!s.save) return;
    const bool can_submit = s.cb.submit_config || pvdg_ui_wifi_host_submit_config;
    if (!s.busy && s.config_available && s.write_authorized &&
        s.selected_ssid[0] && can_submit) {
        lv_obj_remove_state(s.save, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s.save, LV_STATE_DISABLED);
    }
}

static void row_event(lv_event_t *event)
{
    const intptr_t index = (intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= PVDG_UI_WIFI_MAX_NETWORKS || !s.supported[index]) return;

    const char *name = lv_label_get_text(s.row_name[index]);
    if (!name || !name[0] || strcmp(name, "<hidden>") == 0) return;

    snprintf(s.selected_ssid, sizeof(s.selected_ssid), "%s", name);
    s.selected_secure = s.secure[index];
    s.password_visible = false;
    pvdg_ui_label_set_if_changed(s.selected, s.selected_ssid);
    lv_textarea_set_text(s.password, "");
    password_visibility_apply();
    set_password_controls(s.selected_secure);
    update_save_state();

    if (s.selected_secure) {
        if (s.write_authorized) {
            pvdg_ui_label_set_if_changed(s.msg,
                "Secured network selected. Enter the password, then tap Save & Connect.");
        }
        password_event(NULL);
    } else {
        if (s.write_authorized) {
            pvdg_ui_label_set_if_changed(s.msg,
                "Open network selected. No password is required; tap Save & Connect.");
        }
        if (s.save) lv_obj_scroll_to_view(s.save, LV_ANIM_OFF);
    }
}

static void save_event(lv_event_t *event)
{
    (void)event;
    const bool host_submit = pvdg_ui_wifi_host_submit_config != NULL;
    if (s.busy || (!s.cb.submit_config && !host_submit) ||
        !s.config_available || !s.selected_ssid[0]) return;

    if (!s.write_authorized) {
        pvdg_ui_label_set_if_changed(
            s.msg,
            "Engineering login required. Tap the gear icon, sign in, then return and tap Save & Connect.");
        return;
    }

    pvdg_ui_wifi_config_t next;
    bool changed = false;
    char error[160] = {0};
    const char *password = lv_textarea_get_text(s.password);
    const pvdg_ui_wifi_config_result_t result = pvdg_ui_wifi_prepare_primary(
        &s.config, s.selected_ssid, password ? password : "", s.selected_secure,
        &next, &changed, error, sizeof(error));

    if (result != PVDG_UI_WIFI_CONFIG_OK) {
        pvdg_ui_label_set_if_changed(s.msg,
                                     error[0] ? error : "Wi-Fi validation failed.");
        if (result == PVDG_UI_WIFI_CONFIG_PASSWORD_REQUIRED ||
            result == PVDG_UI_WIFI_CONFIG_INVALID_PASSWORD) {
            password_event(NULL);
        }
        return;
    }

    keyboard_hide();
    if (s.cb.submit_config) s.cb.submit_config(&next, changed, s.cb.user);
    else pvdg_ui_wifi_host_submit_config(&next, changed, NULL);
    /* Do not clear the credential here. The product host may reject the write
     * (for example, an expired Engineering session). It clears the field only
     * after persistence succeeds, so the operator does not have to re-enter it. */
}

static void create_keyboard(void)
{
    /* Keep the keyboard inside the Wi-Fi page's flex column instead of the
     * global top layer. When visible it consumes layout space, shrinking the
     * body rather than covering the password field or intercepting other pages. */
    s.keyboard = lv_keyboard_create(s.root);
    lv_obj_set_width(s.keyboard, LV_PCT(100));
    lv_obj_set_height(s.keyboard, 168);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s.keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    set_hidden(s.keyboard, true);
}

static uint32_t hash_byte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * 16777619U;
}

static uint32_t hash_text(uint32_t hash, const char *text)
{
    if (!text) return hash_byte(hash, 0U);
    while (*text) hash = hash_byte(hash, (uint8_t)*text++);
    return hash_byte(hash, 0U);
}

static uint32_t scan_signature(const pvdg_ui_wifi_scan_t *scan)
{
    uint32_t hash = 2166136261U;
    if (!scan) return hash;
    hash = hash_byte(hash, scan->count);
    for (uint8_t i = 0U; i < scan->count && i < PVDG_UI_WIFI_MAX_NETWORKS; ++i) {
        const pvdg_ui_wifi_network_t *network = &scan->networks[i];
        hash = hash_text(hash, network->ssid);
        hash = hash_byte(hash, (uint8_t)network->rssi);
        hash = hash_byte(hash, network->channel);
        hash = hash_byte(hash, network->auth_mode);
        hash = hash_byte(hash, network->connected ? 1U : 0U);
        hash = hash_byte(hash, network->configured_primary ? 1U : 0U);
        hash = hash_byte(hash, network->configured_fallback ? 1U : 0U);
        hash = hash_byte(hash, network->supported ? 1U : 0U);
    }
    return hash;
}

static void render_scan_rows(const pvdg_ui_wifi_scan_t *scan)
{
    if (!scan || scan->scan_running || scan->scan_failed) return;

    const uint32_t signature = scan_signature(scan);
    if (s.rendered_scan_valid && s.rendered_scan_signature == signature) return;

    const uint8_t count = scan->count > PVDG_UI_WIFI_MAX_NETWORKS
                              ? PVDG_UI_WIFI_MAX_NETWORKS : scan->count;
    bool selected_still_visible = s.selected_ssid[0] == '\0';

    for (uint8_t i = 0U; i < PVDG_UI_WIFI_MAX_NETWORKS; ++i) {
        if (i >= count) {
            s.secure[i] = false;
            s.supported[i] = false;
            set_hidden(s.rows[i], true);
            continue;
        }

        const pvdg_ui_wifi_network_t *network = &scan->networks[i];
        const char *name = network->ssid[0] ? network->ssid : "<hidden>";
        pvdg_ui_label_set_if_changed(s.row_name[i], name);

        char meta[96];
        snprintf(meta, sizeof(meta), "%d dBm | Ch %u | %s%s%s",
                 network->rssi, (unsigned)network->channel,
                 network->security[0] ? network->security : "Unknown",
                 network->connected ? " | Connected" : "",
                 network->configured_primary && !network->connected ? " | Primary" : "");
        pvdg_ui_label_set_if_changed(s.row_meta[i], meta);

        s.secure[i] = network->secure;
        s.supported[i] = network->supported && network->ssid[0];
        if (s.supported[i]) lv_obj_remove_state(s.rows[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s.rows[i], LV_STATE_DISABLED);
        set_hidden(s.rows[i], false);

        if (s.selected_ssid[0] && network->ssid[0] &&
            strcmp(s.selected_ssid, network->ssid) == 0) {
            selected_still_visible = true;
            s.selected_secure = network->secure;
        }
    }

    if (!selected_still_visible) {
        s.selected_ssid[0] = '\0';
        s.selected_secure = false;
        s.password_visible = false;
        pvdg_ui_label_set_if_changed(s.selected, "Select a network");
        lv_textarea_set_text(s.password, "");
        password_visibility_apply();
        set_password_controls(false);
    }

    if (count == 0U) {
        pvdg_ui_label_set_if_changed(
            s.scan_empty,
            scan->generation == 0U ? "Tap Scan to discover nearby Wi-Fi networks."
                                   : "No networks found. Tap Scan to retry.");
        set_hidden(s.scan_empty, false);
    } else {
        set_hidden(s.scan_empty, true);
    }

    s.rendered_scan_signature = signature;
    s.rendered_scan_valid = true;
    update_save_state();
}

lv_obj_t *pvdg_ui_wifi_create(lv_obj_t *parent,
                              const pvdg_ui_wifi_callbacks_t *callbacks)
{
    memset(&s, 0, sizeof(s));
    if (callbacks) s.cb = *callbacks;

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
    lv_obj_set_height(header, 34);
    lv_obj_set_layout(header, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    pvdg_ui_make_title(header, "Wi-Fi Manager");
    s.badge = pvdg_ui_make_badge(header, "Offline", lv_color_hex(PVDG_UI_COLOR_DANGER));

    lv_obj_t *body = lv_obj_create(s.root);
    pvdg_ui_style_root(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, PVDG_UI_GAP_SM, LV_PART_MAIN);

    s.left = pvdg_ui_make_card(body);
    lv_obj_set_width(s.left, 300);
    lv_obj_set_height(s.left, LV_PCT(100));
    lv_obj_set_layout(s.left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s.left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s.left, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s.left, 3, LV_PART_MAIN);
    lv_obj_add_flag(s.left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s.left, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s.left, LV_SCROLLBAR_MODE_OFF);

    pvdg_ui_make_metric_row(s.left, "SSID", &s.ssid);
    pvdg_ui_make_metric_row(s.left, "IP", &s.ip);
    pvdg_ui_make_metric_row(s.left, "Signal", &s.rssi);

    lv_obj_t *actions = lv_obj_create(s.left);
    pvdg_ui_style_root(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 46);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 4, LV_PART_MAIN);

    s.scan_spinner = lv_spinner_create(actions);
    lv_spinner_set_anim_params(s.scan_spinner, 800, 70);
    lv_obj_set_size(s.scan_spinner, 22, 22);
    set_hidden(s.scan_spinner, true);

    s.scan = button(actions, "Scan", scan_event);
    s.reconnect = button(actions, "Reconnect", reconnect_event);
    lv_obj_set_flex_grow(s.scan, 1);
    lv_obj_set_flex_grow(s.reconnect, 1);

    /* Keep action/auth feedback above the credential controls so an operator
     * can always see why Save & Connect is unavailable. */
    s.msg = pvdg_ui_make_muted(s.left, "Tap Scan to find nearby Wi-Fi networks.");
    lv_obj_set_width(s.msg, LV_PCT(100));
    lv_label_set_long_mode(s.msg, LV_LABEL_LONG_WRAP);

    pvdg_ui_make_muted(s.left, "Selected network");
    s.selected = label(s.left, "Select a network", PVDG_UI_COLOR_TEXT);
    lv_obj_set_width(s.selected, LV_PCT(100));
    lv_label_set_long_mode(s.selected, LV_LABEL_LONG_DOT);

    lv_obj_t *password_row = lv_obj_create(s.left);
    pvdg_ui_style_root(password_row);
    lv_obj_set_width(password_row, LV_PCT(100));
    lv_obj_set_height(password_row, 42);
    lv_obj_set_layout(password_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(password_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(password_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(password_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(password_row, 4, LV_PART_MAIN);

    s.password = lv_textarea_create(password_row);
    lv_textarea_set_one_line(s.password, true);
    lv_textarea_set_password_mode(s.password, true);
    lv_textarea_set_placeholder_text(s.password, "Wi-Fi password");
    lv_obj_set_height(s.password, 40);
    lv_obj_set_flex_grow(s.password, 1);
    lv_obj_add_event_cb(s.password, password_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s.password, password_event, LV_EVENT_CLICKED, NULL);

    s.password_toggle = button(password_row, "Show", password_toggle_event);
    lv_obj_set_width(s.password_toggle, 66);
    lv_obj_set_height(s.password_toggle, 40);
    s.password_toggle_label = lv_obj_get_child(s.password_toggle, 0);
    set_password_controls(false);

    s.save = button(s.left, "Save & Connect", save_event);
    lv_obj_set_width(s.save, LV_PCT(100));
    lv_obj_add_state(s.save, LV_STATE_DISABLED);

    lv_obj_t *right = pvdg_ui_make_card(body);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(right, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(right, 2, LV_PART_MAIN);
    lv_obj_add_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(right, LV_SCROLLBAR_MODE_AUTO);
    pvdg_ui_make_muted(right, "Nearby Networks | strongest 8");

    s.scan_empty = pvdg_ui_make_muted(right, "Tap Scan to discover nearby Wi-Fi networks.");
    lv_obj_set_width(s.scan_empty, LV_PCT(100));
    lv_label_set_long_mode(s.scan_empty, LV_LABEL_LONG_WRAP);

    for (uint8_t i = 0; i < PVDG_UI_WIFI_MAX_NETWORKS; ++i) {
        s.rows[i] = lv_button_create(right);
        lv_obj_set_width(s.rows[i], LV_PCT(100));
        lv_obj_set_height(s.rows[i], 40);
        lv_obj_set_layout(s.rows[i], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s.rows[i], LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s.rows[i], LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        s.row_name[i] = label(s.rows[i], "--", PVDG_UI_COLOR_TEXT);
        lv_obj_set_width(s.row_name[i], 125);
        lv_label_set_long_mode(s.row_name[i], LV_LABEL_LONG_DOT);
        s.row_meta[i] = label(s.rows[i], "", PVDG_UI_COLOR_MUTED);
        lv_obj_set_flex_grow(s.row_meta[i], 1);
        lv_obj_set_style_text_align(s.row_meta[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_add_event_cb(s.rows[i], row_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        set_hidden(s.rows[i], true);
    }

    create_keyboard();

    pvdg_ui_wifi_config_t current;
    if (pvdg_ui_wifi_host_load_config && pvdg_ui_wifi_host_load_config(&current)) {
        pvdg_ui_wifi_set_config_snapshot(&current, true);
    } else {
        pvdg_ui_wifi_set_config_snapshot(NULL, false);
    }

    return s.root;
}

void pvdg_ui_wifi_apply(const pvdg_ui_model_t *model,
                        const pvdg_ui_wifi_scan_t *scan)
{
    if (!s.root) return;

    if (!scan && pvdg_ui_wifi_host_refresh) {
        if (model) s_host_model = *model;
        else memset(&s_host_model, 0, sizeof(s_host_model));
        memset(&s_host_scan, 0, sizeof(s_host_scan));
        pvdg_ui_wifi_host_refresh(&s_host_model, &s_host_scan);
        model = &s_host_model;
        scan = &s_host_scan;
    }

    if (model) {
        pvdg_ui_badge_set(s.badge,
                          model->network.online ? "Online" : "Offline",
                          lv_color_hex(model->network.online
                                           ? PVDG_UI_COLOR_SUCCESS
                                           : PVDG_UI_COLOR_DANGER));
        pvdg_ui_label_set_if_changed(s.ssid,
                                     model->network.ssid[0] ? model->network.ssid : "--");
        pvdg_ui_label_set_if_changed(s.ip,
                                     model->network.ip[0] ? model->network.ip : "--");
        char rssi[24];
        snprintf(rssi, sizeof(rssi), "%d dBm", model->network.rssi);
        pvdg_ui_label_set_if_changed(s.rssi, rssi);
    }

    if (!scan) return;

    set_scan_indicator(scan->scan_running);

    /* Do not tear down and rebuild eight rows on every telemetry render. The
     * screen runtime can render this active page several times per refresh
     * cycle; toggling HIDDEN on every row each time caused unnecessary layout
     * invalidation exactly when a scan result arrived. Preserve the prior list
     * while scanning and render a completed generation only when its content
     * actually changed. */
    if (!scan->scan_running && !scan->scan_failed) render_scan_rows(scan);

    if (scan->scan_running) {
        lv_obj_add_state(s.scan, LV_STATE_DISABLED);
    } else if (!s.busy) {
        lv_obj_remove_state(s.scan, LV_STATE_DISABLED);
    }
    update_save_state();
}

void pvdg_ui_wifi_set_config_snapshot(const pvdg_ui_wifi_config_t *config,
                                      bool available)
{
    if (!s.root) return;

    char error[128] = {0};
    s.config_available = available && config &&
                         pvdg_ui_wifi_config_valid(config, error, sizeof(error));
    if (s.config_available) {
        s.config = *config;
        if (!s.write_authorized && !s.busy) {
            pvdg_ui_label_set_if_changed(
                s.msg,
                "Engineering login required before Save & Connect. Tap the gear icon to sign in.");
        }
    } else {
        memset(&s.config, 0, sizeof(s.config));
        if (!s.busy) {
            pvdg_ui_label_set_if_changed(s.msg,
                                         error[0] ? error : "Wi-Fi baseline unavailable.");
        }
    }
    update_save_state();
}

void pvdg_ui_wifi_set_action_state(bool busy, const char *message)
{
    s.busy = busy;
    if (!s.root) return;

    if (busy) {
        lv_obj_add_state(s.scan, LV_STATE_DISABLED);
        lv_obj_add_state(s.reconnect, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s.scan, LV_STATE_DISABLED);
        lv_obj_remove_state(s.reconnect, LV_STATE_DISABLED);
        set_scan_indicator(false);
    }
    update_save_state();
    if (message) pvdg_ui_label_set_if_changed(s.msg, message);
}

void pvdg_ui_wifi_set_write_authorized(bool authorized)
{
    const bool changed = s.write_authorized != authorized;
    s.write_authorized = authorized;
    if (!s.root) return;
    update_save_state();

    /* Scan/reconnect progress messages take priority while an action is active;
     * otherwise an auth refresh would overwrite "Scanning..." every render. */
    if (!authorized && !s.busy) {
        pvdg_ui_label_set_if_changed(
            s.msg,
            "Engineering login required before Save & Connect. Tap the gear icon to sign in.");
    } else if (authorized && changed && s.config_available && !s.busy) {
        pvdg_ui_label_set_if_changed(
            s.msg,
            "Engineering authenticated. Select a network, enter its password, then tap Save & Connect.");
    }
}

void pvdg_ui_wifi_clear_password_input(void)
{
    if (s.password) lv_textarea_set_text(s.password, "");
    s.password_visible = false;
    password_visibility_apply();
    keyboard_hide();
}
