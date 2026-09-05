#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*alarms_screen_acknowledge_fn)(void *context,
                                              uint32_t code,
                                              char *message,
                                              size_t message_capacity);

lv_obj_t *alarms_screen_create(lv_obj_t *parent);
void alarms_screen_set_acknowledge_backend(alarms_screen_acknowledge_fn acknowledge,
                                           void *context);
void alarms_screen_apply_alarms(const screen_alarms_snapshot_t *snapshot);
void alarms_screen_apply_events(const screen_events_snapshot_t *snapshot);
void alarms_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
