#include "screen_app.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarms_screen.h"
#include "commissioning_screen.h"
#include "grid_screen.h"
#include "network_screen.h"
#include "overview_screen.h"
#include "readiness_screen.h"
#include "screen_widgets.h"
#include "solar_screen.h"
#include "source_commissioning_screen.h"

#define SCREEN_MIN_TOUCH_TARGET_PX 44

typedef struct {
    lv_obj_t *root;
    lv_obj_t *content;
    lv_obj_t *pages[SCREEN_PAGE_COUNT];
    lv_obj_t *nav_buttons[SCREEN_PAGE_COUNT];
    lv_obj_t *nav_signal;
    screen_page_t active;
    screen_status_snapshot_t status;
    screen_telemetry_snapshot_t telemetry;
} screen_app_state_t;

static screen_app_state_t s_app;
/* Backends may be installed before their heavy Engineering pages exist. Keep
 * the authoritative function tables outside the page objects so lazy creation
 * can bind them immediately after the page allocates its own static state. */
static screen_commissioning_backend_t s_commissioning_backend;
static bool s_commissioning_backend_set;
static source_commission_backend_t s_source_backend;
static bool s_source_backend_set;
static network_screen_backend_t s_network_backend;
static bool s_network_backend_set;

/* The local HMI uses fixed, kiosk-style pages. LVGL objects created with
 * lv_obj_create() are scrollable by default; on a touch panel that can turn a
 * small finger movement during a navigation tap into a visible viewport shift.
 * Keep navigation/page surfaces fixed and switch pages only through HIDDEN. */
static void make_fixed_surface(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

/* Industrial UI v1 requires >=44 px touch targets on touch layouts. Individual
 * pages historically used several compact 34/38/40 px controls. Enforce the
 * minimum once, immediately after each lazy page tree is created, so every
 * button, checkbox, dropdown and text input is physically tappable without
 * duplicating a size rule in every page implementation. min_height preserves a
 * larger page-specific control while lifting only undersized targets. */
static bool is_touch_target(const lv_obj_t *obj)
{
    return obj &&
           (lv_obj_has_class(obj, &lv_button_class) ||
            lv_obj_has_class(obj, &lv_checkbox_class) ||
            lv_obj_has_class(obj, &lv_dropdown_class) ||
            lv_obj_has_class(obj, &lv_textarea_class));
}

static void enforce_min_touch_targets(lv_obj_t *obj)
{
    if (!obj) return;
    if (is_touch_target(obj)) {
        lv_obj_set_style_min_height(obj, SCREEN_MIN_TOUCH_TARGET_PX, LV_PART_MAIN);
    }
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0U; i < child_count; ++i) {
        enforce_min_touch_targets(lv_obj_get_child(obj, (int32_t)i));
    }
}

static bool active_is(screen_page_t page)
{
    return s_app.root && s_app.active == page;
}

/* Physical candidate ec4fb846 created all seven complete page trees before the
 * first frame became visible. On the exact board that happened after Product
 * Core had already driven free internal DMA to 1695 bytes, and the LCD never
 * rendered. Hidden pages have no reason to consume boot-time memory. Build the
 * Overview only at startup and construct each other page on first navigation.
 * Page-local retained-row/flicker behavior is unchanged after creation. */
static lv_obj_t *ensure_page(screen_page_t page)
{
    if (!s_app.content || (unsigned)page >= (unsigned)SCREEN_PAGE_COUNT) return NULL;
    if (s_app.pages[page]) return s_app.pages[page];

    lv_obj_t *created = NULL;
    switch (page) {
    case SCREEN_PAGE_OVERVIEW:
        created = overview_screen_create(s_app.content);
        break;
    case SCREEN_PAGE_GRID:
        created = grid_screen_create(s_app.content);
        break;
    case SCREEN_PAGE_SOLAR:
        created = solar_screen_create(s_app.content);
        break;
    case SCREEN_PAGE_ALARMS:
        created = alarms_screen_create(s_app.content);
        break;
    case SCREEN_PAGE_READINESS:
        created = readiness_screen_create(s_app.content);
        break;
    case SCREEN_PAGE_COMMISSIONING:
        created = commissioning_screen_create(s_app.content);
        if (created && s_commissioning_backend_set) {
            commissioning_screen_set_backend(&s_commissioning_backend);
        }
        break;
    case SCREEN_PAGE_SOURCE:
        created = source_commissioning_screen_create(s_app.content);
        if (created && s_source_backend_set) {
            source_commissioning_screen_set_backend(&s_source_backend);
        }
        break;
    case SCREEN_PAGE_NETWORK:
        created = network_screen_create(s_app.content);
        if (created && s_network_backend_set) {
            network_screen_set_backend(&s_network_backend);
        }
        break;
    default:
        return NULL;
    }

    if (!created) return NULL;
    make_fixed_surface(created);
    enforce_min_touch_targets(created);
    s_app.pages[page] = created;
    return created;
}

static void nav_clicked(lv_event_t *event)
{
    const uintptr_t raw = (uintptr_t)lv_event_get_user_data(event);
    if (raw >= (uintptr_t)SCREEN_PAGE_COUNT) return;
    screen_app_show_page((screen_page_t)raw);
}

/* The nav bar previously gave no indication of which page was open --
 * every button used the same default LVGL styling regardless of state, so
 * an operator glancing at the panel could not tell where they were. This
 * applies (or clears) the accent-filled "active tab" look on demand. */
static void nav_button_set_active(lv_obj_t *button, bool active)
{
    if (!button) return;
    lv_obj_set_style_bg_color(button,
                              lv_color_hex(active ? SCREEN_COLOR_ACCENT : SCREEN_COLOR_SURFACE_RAISED),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, active ? LV_OPA_COVER : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
        lv_obj_set_style_text_color(
            label, lv_color_hex(active ? SCREEN_COLOR_ACCENT_ON : SCREEN_COLOR_TEXT_SECONDARY),
            LV_PART_MAIN);
    }
}

static lv_obj_t *nav_button(lv_obj_t *parent, const char *icon, const char *text, screen_page_t page)
{
    lv_obj_t *button = lv_button_create(parent);
    make_fixed_surface(button);
    lv_obj_set_style_radius(button, SCREEN_RADIUS_SM, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_height(button, SCREEN_MIN_TOUCH_TARGET_PX);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_add_event_cb(button, nav_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)page);

    lv_obj_t *label = lv_label_create(button);
    if (icon && icon[0]) {
        char combined[48];
        snprintf(combined, sizeof(combined), "%s  %s", icon, text);
        lv_label_set_text(label, combined);
    } else {
        lv_label_set_text(label, text);
    }
    lv_obj_center(label);
    nav_button_set_active(button, false);

    if ((unsigned)page < (unsigned)SCREEN_PAGE_COUNT) s_app.nav_buttons[page] = button;
    return button;
}

lv_obj_t *screen_app_create(lv_obj_t *parent)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.active = SCREEN_PAGE_OVERVIEW;

    s_app.root = lv_obj_create(parent ? parent : lv_screen_active());
    make_fixed_surface(s_app.root);
    lv_obj_set_size(s_app.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_app.root, lv_color_hex(SCREEN_COLOR_BG_APP), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_app.root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_app.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_app.root, SCREEN_SPACE_MD, LV_PART_MAIN);
    lv_obj_set_layout(s_app.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_app.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_app.root, SCREEN_SPACE_MD, LV_PART_MAIN);

    lv_obj_t *nav = lv_obj_create(s_app.root);
    lv_obj_remove_style_all(nav);
    make_fixed_surface(nav);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, SCREEN_MIN_TOUCH_TARGET_PX);
    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, SCREEN_SPACE_XS, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(nav, SCREEN_SPACE_SM, LV_PART_MAIN);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(nav, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(nav, lv_color_hex(SCREEN_COLOR_BORDER), LV_PART_MAIN);

    nav_button(nav, LV_SYMBOL_HOME, "Overview", SCREEN_PAGE_OVERVIEW);
    nav_button(nav, LV_SYMBOL_CHARGE, "Grid", SCREEN_PAGE_GRID);
    nav_button(nav, NULL, "Solar", SCREEN_PAGE_SOLAR);
    nav_button(nav, LV_SYMBOL_BELL, "Alarms", SCREEN_PAGE_ALARMS);
    nav_button(nav, LV_SYMBOL_OK, "Ready", SCREEN_PAGE_READINESS);
    nav_button(nav, LV_SYMBOL_SETTINGS, "Commission", SCREEN_PAGE_COMMISSIONING);
    nav_button(nav, NULL, "Source", SCREEN_PAGE_SOURCE);
    nav_button(nav, LV_SYMBOL_WIFI, "Network", SCREEN_PAGE_NETWORK);
    nav_button_set_active(s_app.nav_buttons[SCREEN_PAGE_OVERVIEW], true);

    /* Persistent Wi-Fi signal indicator: lives in the nav row so it stays
     * visible on every page, not only the Network settings screen -- an
     * operator glancing at Overview or Alarms should not have to navigate
     * away to see whether the panel still has a link. Fixed width so it
     * does not reflow the nav row's flex-grow buttons as the text changes
     * length between "Wi-Fi ||||" and "Wi-Fi: none". */
    s_app.nav_signal = lv_label_create(nav);
    screen_ui_apply_wifi_indicator(s_app.nav_signal, false, 0);
    lv_obj_set_width(s_app.nav_signal, 60);
    lv_obj_set_style_text_align(s_app.nav_signal, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    s_app.content = lv_obj_create(s_app.root);
    lv_obj_remove_style_all(s_app.content);
    make_fixed_surface(s_app.content);
    lv_obj_set_width(s_app.content, LV_PCT(100));
    lv_obj_set_flex_grow(s_app.content, 1);

    /* Only the first visible page is allocated during boot. If even Overview
     * cannot be created, propagate failure so the product logs headless/OOM
     * instead of falsely declaring a usable black screen. */
    if (!ensure_page(SCREEN_PAGE_OVERVIEW)) return NULL;
    return s_app.root;
}

void screen_app_show_page(screen_page_t page)
{
    if (!s_app.root || (unsigned)page >= (unsigned)SCREEN_PAGE_COUNT) return;
    if (!ensure_page(page)) return;
    for (int i = 0; i < (int)SCREEN_PAGE_COUNT; ++i) {
        if (s_app.pages[i]) {
            if (i == (int)page) lv_obj_remove_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
        }
        nav_button_set_active(s_app.nav_buttons[i], i == (int)page);
    }
    s_app.active = page;
}

screen_page_t screen_app_get_active_page(void)
{
    return s_app.active;
}

void screen_app_set_commissioning_backend(const screen_commissioning_backend_t *backend)
{
    if (backend) {
        s_commissioning_backend = *backend;
        s_commissioning_backend_set = true;
    } else {
        memset(&s_commissioning_backend, 0, sizeof(s_commissioning_backend));
        s_commissioning_backend_set = false;
    }
    if (s_app.pages[SCREEN_PAGE_COMMISSIONING]) commissioning_screen_set_backend(backend);
}

void screen_app_set_source_commissioning_backend(const source_commission_backend_t *backend)
{
    if (backend) {
        s_source_backend = *backend;
        s_source_backend_set = true;
    } else {
        memset(&s_source_backend, 0, sizeof(s_source_backend));
        s_source_backend_set = false;
    }
    if (s_app.pages[SCREEN_PAGE_SOURCE]) source_commissioning_screen_set_backend(backend);
}

void screen_app_set_network_backend(const network_screen_backend_t *backend)
{
    if (backend) {
        s_network_backend = *backend;
        s_network_backend_set = true;
    } else {
        memset(&s_network_backend, 0, sizeof(s_network_backend));
        s_network_backend_set = false;
    }
    if (s_app.pages[SCREEN_PAGE_NETWORK]) network_screen_set_backend(backend);
}

/* Keep transport/model refresh independent from LVGL rendering. The Core may
 * continue refreshing every authoritative snapshot, but only the page the
 * operator can actually see is allowed to mutate its LVGL tree. On this RGB
 * panel every unnecessary hidden-page mutation competes with scanout bandwidth
 * and can become visible as movement when live plant data changes. */
void screen_app_apply_live(const screen_live_snapshot_t *snapshot)
{
    if (active_is(SCREEN_PAGE_OVERVIEW)) overview_screen_apply_live(snapshot);
}

void screen_app_apply_status(const screen_status_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.status = *snapshot;

    if (active_is(SCREEN_PAGE_OVERVIEW)) {
        overview_screen_apply_status(snapshot);
    } else if (active_is(SCREEN_PAGE_COMMISSIONING)) {
        commissioning_screen_apply_status(snapshot);
    } else if (active_is(SCREEN_PAGE_READINESS)) {
        readiness_screen_apply(s_app.telemetry.valid ? &s_app.telemetry : NULL,
                               s_app.status.valid ? &s_app.status : NULL);
    }
}

void screen_app_apply_meters(const screen_meters_snapshot_t *snapshot)
{
    if (active_is(SCREEN_PAGE_GRID)) grid_screen_apply(snapshot);
    else if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_apply_meters(snapshot);
}

void screen_app_apply_inverters(const screen_inverters_snapshot_t *snapshot)
{
    if (active_is(SCREEN_PAGE_SOLAR)) solar_screen_apply(snapshot);
    else if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_apply_inverters(snapshot);
}

void screen_app_apply_telemetry(const screen_telemetry_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.telemetry = *snapshot;

    /* Runs regardless of which page is active -- see the nav-bar comment in
     * screen_app_create() for why this indicator must not be page-scoped. */
    if (s_app.nav_signal && s_app.telemetry.valid) {
        screen_ui_apply_wifi_indicator(s_app.nav_signal, s_app.telemetry.network_online,
                                       s_app.telemetry.rssi);
    }

    if (active_is(SCREEN_PAGE_COMMISSIONING)) {
        commissioning_screen_apply_telemetry(snapshot);
    } else if (active_is(SCREEN_PAGE_READINESS)) {
        readiness_screen_apply(s_app.telemetry.valid ? &s_app.telemetry : NULL,
                               s_app.status.valid ? &s_app.status : NULL);
    }
}

void screen_app_apply_commissioning(const screen_commissioning_snapshot_t *snapshot)
{
    if (active_is(SCREEN_PAGE_READINESS)) readiness_screen_apply_commissioning(snapshot);
    else if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_apply_gate(snapshot);
}

void screen_app_apply_events(const screen_events_snapshot_t *snapshot)
{
    if (active_is(SCREEN_PAGE_ALARMS)) alarms_screen_apply_events(snapshot);
}

void screen_app_apply_alarms(const screen_alarms_snapshot_t *snapshot)
{
    if (active_is(SCREEN_PAGE_ALARMS)) alarms_screen_apply_alarms(snapshot);
}

void screen_app_show_live_unavailable(void)
{
    if (active_is(SCREEN_PAGE_OVERVIEW)) overview_screen_show_backend_unavailable();
}

void screen_app_show_meters_unavailable(void)
{
    if (active_is(SCREEN_PAGE_GRID)) grid_screen_show_unavailable();
    else if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_apply_meters(NULL);
}

void screen_app_show_inverters_unavailable(void)
{
    if (active_is(SCREEN_PAGE_SOLAR)) solar_screen_show_unavailable();
    else if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_apply_inverters(NULL);
}

void screen_app_show_operations_unavailable(void)
{
    if (active_is(SCREEN_PAGE_ALARMS)) alarms_screen_show_unavailable();
}

void screen_app_show_readiness_unavailable(void)
{
    memset(&s_app.status, 0, sizeof(s_app.status));
    memset(&s_app.telemetry, 0, sizeof(s_app.telemetry));
    if (active_is(SCREEN_PAGE_READINESS)) {
        readiness_screen_show_unavailable();
    } else if (active_is(SCREEN_PAGE_COMMISSIONING)) {
        commissioning_screen_apply_status(NULL);
        commissioning_screen_apply_telemetry(NULL);
    }
}

void screen_app_show_commissioning_unavailable(void)
{
    if (active_is(SCREEN_PAGE_READINESS)) readiness_screen_show_commissioning_unavailable();
    else if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_apply_gate(NULL);
}

void screen_app_show_backend_unavailable(void)
{
    screen_app_show_live_unavailable();
    screen_app_show_meters_unavailable();
    screen_app_show_inverters_unavailable();
    screen_app_show_operations_unavailable();
    screen_app_show_readiness_unavailable();
    screen_app_show_commissioning_unavailable();
    if (active_is(SCREEN_PAGE_COMMISSIONING)) commissioning_screen_show_unavailable();
    if (active_is(SCREEN_PAGE_SOURCE)) source_commissioning_screen_show_unavailable();
}
