#pragma once

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *grid_screen_create(lv_obj_t *parent);
void grid_screen_apply(const screen_meters_snapshot_t *snapshot);
void grid_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
