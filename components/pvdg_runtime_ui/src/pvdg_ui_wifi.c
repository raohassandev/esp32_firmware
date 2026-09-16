#include "pvdg_ui_wifi.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pvdg_ui_theme.h"

/* Optional product-host hooks. The reusable runtime UI keeps no direct
 * dependency on network_manager; a product may provide these symbols. */
extern void pvdg_ui_wifi_host_refresh(pvdg_ui_model_t *model,
                                      pvdg_ui_wifi_scan_t *scan)
    __attribute__((weak));
extern void pvdg_ui_wifi_host_request_scan(void *user) __attribute__((weak));
extern void pvdg_ui_wifi_host_request_reconnect(void *user) __attribute__((weak));

typedef struct {
    lv_obj_t *root;
    lv_obj_t *badge;
    lv_obj_t *ssid;
    lv_obj_t *ip;
    lv_obj_t *rssi;
    lv_obj_t *scan;
    lv_obj_t *reconnect;
    lv_obj_t *selected;
    lv_obj_t *password;
    lv_obj_t *save;
    lv_obj_t *msg;
    lv_obj_t *rows[PVDG_UI_WIFI_MAX_NETWORKS];
    lv_obj_t *row_name[PVDG_UI_WIFI_MAX_NETWORKS];
    lv_obj_t *row_meta[PVDG_UI_WIFI_MAX_NETWORKS];
    pvdg_ui_wifi_callbacks_t cb;
    pvdg_ui_wifi_config_t config;
    char selected_ssid[PVDG_UI_TEXT_MEDIUM];
    bool secure[PVDG_UI_WIFI_MAX_NETWORKS];
    bool supported[PVDG_UI_WIFI_MAX_NETWORKS];
    bool selected_secure;
    bool config_available;
    bool busy;
    bool auto_scan_requested;
} wifi_ui_t;

static wifi_ui_t s;

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

static void request_scan_from_host(void)
{
    if (s.cb.request_scan) {
        s.cb.request_scan(s.cb.user);
    } else if (pvdg_ui_wifi_host_request_scan) {
        pvdg_ui_wifi_host_request_scan(NULL);
    }
}

static void scan_event(lv_event_t *event)
{
    (void)event;
    if (s.busy) return;
    s.auto_scan_requested = true;
    request_scan_from_host();
}

static void reconnect_event(lv_event_t *event)
{
    (void)event;
    if (s.busy) return;
    if (s.cb.request_reconnect) {
        s.cb.request_reconnect(s.cb.user);
    } else if (pvdg_ui_wifi_host_request_reconnect) {
        pvdg_ui_wifi_host_request_reconnect(NULL);
    }
}

static void row_event(lv_event_t *event)
{
    intptr_t index = (intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= PVDG_UI_WIFI_MAX_NETWORKS || !s.supported[index]) return;

    const char *name = lv_label_get_text(s.row_name[index]);
    snprintf(s.selected_ssid, sizeof(s.selected_ssid), "%s", name ? name : "");
    s.selected_secure = s.secure[index];
    pvdg_ui_label_set_if_changed(s.selected, s.selected_ssid);
    lv_textarea_set_text(s.password, "");
}

static void save_event(lv_event_t *event)
{
    (void)event;
    if (s.busy || !s.cb.submit_config || !s.config_available || !s.selected_ssid[0]) return;

    pvdg_ui_wifi_config_t next;
    bool changed = false;
    char error[160] = {0};
    const char *password = lv_textarea_get_text(s.password);
    pvdg_ui_wifi_config_result_t result = pvdg_ui_wifi_prepare_primary(
        &s.config,
        s.selected_ssid,
        password ? password : "",
        s.selected_secure,
        &next,
        &changed,
        error,
        sizeof(error));

    if (result != PVDG_UI_WIFI_CONFIG_OK) {
        pvdg_ui_label_set_if_changed(s.msg,
                                     error[0] ? error : "Wi-Fi validation failed.");
        return;
    }

    s.cb.submit_config(&next, changed, s.cb.user);
    lv_textarea_set_text(s.password, "");
    pvdg_ui_label_set_if_changed(s.msg,
                                 "Wi-Fi configuration submitted; restart may be required.");
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
    lv_obj_set_style_pad_all(s.root, PVDG_UI_GAP_MD, LV_PART_MAIN);

    pvdg_ui_make_title(s.root, "Wi-Fi Manager");
    s.badge = pvdg_ui_make_badge(s.root, "Offline", lv_color_hex(PVDG_UI_COLOR_DANGER));
    pvdg_ui_make_metric_row(s.root, "SSID", &s.ssid);
    pvdg_ui_make_metric_row(s.root, "IP", &s.ip);
    pvdg_ui_make_metric_row(s.root, "Signal", &s.rssi);

    lv_obj_t *actions = lv_obj_create(s.root);
    pvdg_ui_style_root(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, 52);
    lv_obj_set_layout(actions, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    s.scan = button(actions, "Scan", scan_event);
    s.reconnect = button(actions, "Reconnect", reconnect_event);

    s.selected = label(s.root, "Select a network", PVDG_UI_COLOR_TEXT);
    s.password = lv_textarea_create(s.root);
    lv_textarea_set_one_line(s.password, true);
    lv_textarea_set_password_mode(s.password, true);
    lv_textarea_set_placeholder_text(s.password, "Password (never prefilled)");
    lv_obj_set_width(s.password, LV_PCT(100));

    s.save = button(s.root, "Save Primary Wi-Fi", save_event);
    lv_obj_set_width(s.save, LV_PCT(100));
    s.msg = pvdg_ui_make_muted(s.root, "Tap Scan to find nearby Wi-Fi networks.");

    for (uint8_t i = 0; i < PVDG_UI_WIFI_MAX_NETWORKS; ++i) {
        s.rows[i] = lv_button_create(s.root);
        lv_obj_set_width(s.rows[i], LV_PCT(100));
        lv_obj_set_height(s.rows[i], 34);
        lv_obj_set_layout(s.rows[i], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(s.rows[i], LV_FLEX_FLOW_ROW);
        s.row_name[i] = label(s.rows[i], "--", PVDG_UI_COLOR_TEXT);
        lv_obj_set_flex_grow(s.row_name[i], 1);
        s.row_meta[i] = label(s.rows[i], "", PVDG_UI_COLOR_MUTED);
        lv_obj_add_event_cb(s.rows[i], row_event, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_add_flag(s.rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    return s.root;
}

void pvdg_ui_wifi_apply(const pvdg_ui_model_t *model,
                        const pvdg_ui_wifi_scan_t *scan)
{
    if (!s.root) return;

    pvdg_ui_model_t host_model;
    pvdg_ui_wifi_scan_t host_scan;
    if (!scan && pvdg_ui_wifi_host_refresh) {
        if (model) host_model = *model;
        else memset(&host_model, 0, sizeof(host_model));
        memset(&host_scan, 0, sizeof(host_scan));

        pvdg_ui_wifi_host_refresh(&host_model, &host_scan);
        model = &host_model;
        scan = &host_scan;

        if (!s.auto_scan_requested && !host_scan.scan_running && host_scan.count == 0U) {
            s.auto_scan_requested = true;
            request_scan_from_host();
            host_scan.scan_running = true;
        }
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

    for (uint8_t i = 0; i < PVDG_UI_WIFI_MAX_NETWORKS; ++i) {
        lv_obj_add_flag(s.rows[i], LV_OBJ_FLAG_HIDDEN);
        s.secure[i] = false;
        s.supported[i] = false;
    }

    if (!scan) return;

    uint8_t count = scan->count > PVDG_UI_WIFI_MAX_NETWORKS
                        ? PVDG_UI_WIFI_MAX_NETWORKS
                        : scan->count;
    for (uint8_t i = 0; i < count; ++i) {
        const pvdg_ui_wifi_network_t *network = &scan->networks[i];
        pvdg_ui_label_set_if_changed(s.row_name[i],
                                     network->ssid[0] ? network->ssid : "<hidden>");
        char meta[96];
        snprintf(meta, sizeof(meta), "%d dBm  Ch %u  %s",
                 network->rssi,
                 (unsigned)network->channel,
                 network->security[0] ? network->security : "Unknown");
        pvdg_ui_label_set_if_changed(s.row_meta[i], meta);
        s.secure[i] = network->secure;
        s.supported[i] = network->supported;
        if (network->supported) lv_obj_remove_state(s.rows[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s.rows[i], LV_STATE_DISABLED);
        lv_obj_remove_flag(s.rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (scan->scan_running) lv_obj_add_state(s.scan, LV_STATE_DISABLED);
    else if (!s.busy) lv_obj_remove_state(s.scan, LV_STATE_DISABLED);
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
        pvdg_ui_label_set_if_changed(s.msg, "Wi-Fi baseline loaded.");
    } else {
        memset(&s.config, 0, sizeof(s.config));
        pvdg_ui_label_set_if_changed(s.msg,
                                     error[0] ? error : "Wi-Fi baseline unavailable.");
    }
}

void pvdg_ui_wifi_set_action_state(bool busy, const char *message)
{
    s.busy = busy;
    if (!s.root) return;

    if (busy) {
        lv_obj_add_state(s.scan, LV_STATE_DISABLED);
        lv_obj_add_state(s.reconnect, LV_STATE_DISABLED);
        lv_obj_add_state(s.save, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s.scan, LV_STATE_DISABLED);
        lv_obj_remove_state(s.reconnect, LV_STATE_DISABLED);
        if (s.config_available && s.selected_ssid[0]) {
            lv_obj_remove_state(s.save, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s.save, LV_STATE_DISABLED);
        }
    }

    if (message) pvdg_ui_label_set_if_changed(s.msg, message);
}
