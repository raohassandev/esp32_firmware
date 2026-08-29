#include "screen_app.h"

#include <stdint.h>
#include <string.h>

#include "alarms_screen.h"
#include "commissioning_screen.h"
#include "grid_screen.h"
#include "overview_screen.h"
#include "readiness_screen.h"
#include "solar_screen.h"
#include "source_commissioning_screen.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *content;
    lv_obj_t *pages[SCREEN_PAGE_COUNT];
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
    default:
        return NULL;
    }

    if (!created) return NULL;
    make_fixed_surface(created);
    s_app.pages[page] = created;
    return created;
}

static void nav_clicked(lv_event_t *event)
{
    const uintptr_t raw = (uintptr_t)lv_event_get_user_data(event);
    if (raw >= (uintptr_t)SCREEN_PAGE_COUNT) return;
    screen_app_show_page((screen_page_t)raw);
}

static lv_obj_t *nav_button(lv_obj_t *parent, const char *text, screen_page_t page)
{
    lv_obj_t *button = lv_button_create(parent);
    make_fixed_surface(button);
    lv_obj_set_height(button, 40);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_add_event_cb(button, nav_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)page);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

lv_obj_t *screen_app_create(lv_obj_t *parent)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.active = SCREEN_PAGE_OVERVIEW;

    s_app.root = lv_obj_create(parent ? parent : lv_screen_active());
    make_fixed_surface(s_app.root);
    lv_obj_set_size(s_app.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_app.root, lv_color_hex(0x0B1017), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_app.root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_app.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_app.root, 8, LV_PART_MAIN);
    lv_obj_set_layout(s_app.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_app.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_app.root, 7, LV_PART_MAIN);

    lv_obj_t *nav = lv_obj_create(s_app.root);
    lv_obj_remove_style_all(nav);
    make_fixed_surface(nav);
    lv_obj_set_width(nav, LV_PCT(100));
    lv_obj_set_height(nav, 44);
    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, 5, LV_PART_MAIN);

    nav_button(nav, "Overview", SCREEN_PAGE_OVERVIEW);
    nav_button(nav, "Grid", SCREEN_PAGE_GRID);
    nav_button(nav, "Solar", SCREEN_PAGE_SOLAR);
    nav_button(nav, "Alarms", SCREEN_PAGE_ALARMS);
    nav_button(nav, "Ready", SCREEN_PAGE_READINESS);
    nav_button(nav, "Commission", SCREEN_PAGE_COMMISSIONING);
    nav_button(nav, "Source", SCREEN_PAGE_SOURCE);

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
        if (!s_app.pages[i]) continue;
        if (i == (int)page) lv_obj_remove_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
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
