#pragma once

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *alarms_screen_create(lv_obj_t *parent);
void alarms_screen_apply_alarms(const screen_alarms_snapshot_t *snapshot);
void alarms_screen_apply_events(const screen_events_snapshot_t *snapshot);
void alarms_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
