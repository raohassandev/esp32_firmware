#pragma once

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only operator overview for the Waveshare local display.
 *
 * This page has no control/write callbacks. It only presents values already
 * exposed by the existing backend contracts.
 */
lv_obj_t *overview_screen_create(lv_obj_t *parent);
void overview_screen_apply_live(const screen_live_snapshot_t *snapshot);
void overview_screen_apply_status(const screen_status_snapshot_t *snapshot);
void overview_screen_show_backend_unavailable(void);

#ifdef __cplusplus
}
#endif
