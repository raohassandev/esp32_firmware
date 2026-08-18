#include "screen_app.h"

#include <stdint.h>
#include <string.h>

#include "alarms_screen.h"
#include "grid_screen.h"
#include "overview_screen.h"
#include "readiness_screen.h"
#include "solar_screen.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *pages[SCREEN_PAGE_COUNT];
    screen_page_t active;
    screen_status_snapshot_t status;
    screen_telemetry_snapshot_t telemetry;
} screen_app_state_t;

static screen_app_state_t s_app;

static void nav_clicked(lv_event_t *event)
{
    const uintptr_t raw = (uintptr_t)lv_event_get_user_data(event);
    if (raw >= (uintptr_t)SCREEN_PAGE_COUNT) return;
    screen_app_show_page((screen_page_t)raw);
}

static lv_obj_t *nav_button(lv_obj_t *parent, const char *text, screen_page_t page)
{
    lv_obj_t *button = lv_button_create(parent);
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

    lv_obj_t *content = lv_obj_create(s_app.root);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);

    s_app.pages[SCREEN_PAGE_OVERVIEW] = overview_screen_create(content);
    s_app.pages[SCREEN_PAGE_GRID] = grid_screen_create(content);
    s_app.pages[SCREEN_PAGE_SOLAR] = solar_screen_create(content);
    s_app.pages[SCREEN_PAGE_ALARMS] = alarms_screen_create(content);
    s_app.pages[SCREEN_PAGE_READINESS] = readiness_screen_create(content);

    for (int i = 1; i < (int)SCREEN_PAGE_COUNT; ++i) {
        lv_obj_add_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    return s_app.root;
}

void screen_app_show_page(screen_page_t page)
{
    if (!s_app.root || (unsigned)page >= (unsigned)SCREEN_PAGE_COUNT) return;
    for (int i = 0; i < (int)SCREEN_PAGE_COUNT; ++i) {
        if (!s_app.pages[i]) continue;
        if (i == (int)page) lv_obj_remove_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_app.active = page;
}

void screen_app_apply_live(const screen_live_snapshot_t *snapshot)
{
    overview_screen_apply_live(snapshot);
}

void screen_app_apply_status(const screen_status_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.status = *snapshot;
    overview_screen_apply_status(snapshot);
    readiness_screen_apply(s_app.telemetry.valid ? &s_app.telemetry : NULL,
                           s_app.status.valid ? &s_app.status : NULL);
}

void screen_app_apply_meters(const screen_meters_snapshot_t *snapshot)
{
    grid_screen_apply(snapshot);
}

void screen_app_apply_inverters(const screen_inverters_snapshot_t *snapshot)
{
    solar_screen_apply(snapshot);
}

void screen_app_apply_telemetry(const screen_telemetry_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.telemetry = *snapshot;
    readiness_screen_apply(s_app.telemetry.valid ? &s_app.telemetry : NULL,
                           s_app.status.valid ? &s_app.status : NULL);
}

void screen_app_apply_events(const screen_events_snapshot_t *snapshot)
{
    alarms_screen_apply_events(snapshot);
}

void screen_app_apply_alarms(const screen_alarms_snapshot_t *snapshot)
{
    alarms_screen_apply_alarms(snapshot);
}

void screen_app_show_live_unavailable(void)
{
    overview_screen_show_backend_unavailable();
}

void screen_app_show_meters_unavailable(void)
{
    grid_screen_show_unavailable();
}

void screen_app_show_inverters_unavailable(void)
{
    solar_screen_show_unavailable();
}

void screen_app_show_operations_unavailable(void)
{
    alarms_screen_show_unavailable();
}

void screen_app_show_readiness_unavailable(void)
{
    memset(&s_app.status, 0, sizeof(s_app.status));
    memset(&s_app.telemetry, 0, sizeof(s_app.telemetry));
    readiness_screen_show_unavailable();
}

void screen_app_show_backend_unavailable(void)
{
    screen_app_show_live_unavailable();
    screen_app_show_meters_unavailable();
    screen_app_show_inverters_unavailable();
    screen_app_show_operations_unavailable();
    screen_app_show_readiness_unavailable();
}
