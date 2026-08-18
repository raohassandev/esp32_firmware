#pragma once

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *readiness_screen_create(lv_obj_t *parent);
void readiness_screen_apply(const screen_telemetry_snapshot_t *snapshot,
                            const screen_status_snapshot_t *status);
void readiness_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
