#pragma once

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *solar_screen_create(lv_obj_t *parent);
void solar_screen_apply(const screen_inverters_snapshot_t *snapshot);
void solar_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
