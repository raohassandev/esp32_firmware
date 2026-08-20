#pragma once

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_PAGE_OVERVIEW = 0,
    SCREEN_PAGE_GRID,
    SCREEN_PAGE_SOLAR,
    SCREEN_PAGE_ALARMS,
    SCREEN_PAGE_READINESS,
    SCREEN_PAGE_COUNT
} screen_page_t;

lv_obj_t *screen_app_create(lv_obj_t *parent);
void screen_app_show_page(screen_page_t page);

void screen_app_apply_live(const screen_live_snapshot_t *snapshot);
void screen_app_apply_status(const screen_status_snapshot_t *snapshot);
void screen_app_apply_meters(const screen_meters_snapshot_t *snapshot);
void screen_app_apply_inverters(const screen_inverters_snapshot_t *snapshot);
void screen_app_apply_telemetry(const screen_telemetry_snapshot_t *snapshot);
void screen_app_apply_commissioning(const screen_commissioning_snapshot_t *snapshot);
void screen_app_apply_events(const screen_events_snapshot_t *snapshot);
void screen_app_apply_alarms(const screen_alarms_snapshot_t *snapshot);

/* Per-contract unavailable states let one failed endpoint degrade only the
 * surface it owns instead of erasing healthy data from unrelated pages. */
void screen_app_show_live_unavailable(void);
void screen_app_show_meters_unavailable(void);
void screen_app_show_inverters_unavailable(void);
void screen_app_show_operations_unavailable(void);
void screen_app_show_readiness_unavailable(void);
void screen_app_show_commissioning_unavailable(void);
void screen_app_show_backend_unavailable(void);

#ifdef __cplusplus
}
#endif
