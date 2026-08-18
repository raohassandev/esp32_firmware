#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime bridge for the native screen.
 *
 * The provider supplies JSON for the EXISTING backend API path requested. It may
 * use an in-process backend adapter or another qualified transport, but it must
 * not reimplement business/control logic. The returned JSON remains provider-
 * owned and valid until release() is called (or until acquire() returns again
 * when release is NULL).
 *
 * These refresh functions perform no scheduling and create no task. The board
 * integration owns cadence and must call them while it is safe to update LVGL.
 */
typedef bool (*screen_json_acquire_fn)(void *context, const char *path, const char **json);
typedef void (*screen_json_release_fn)(void *context, const char *path, const char *json);

typedef struct {
    void *context;
    screen_json_acquire_fn acquire;
    screen_json_release_fn release;
} screen_api_provider_t;

bool screen_runtime_init(const screen_api_provider_t *provider);

/* Existing /api/live information cadence. */
bool screen_runtime_refresh_fast(void);

/* Existing status/readiness contracts. */
bool screen_runtime_refresh_status(void);

/* Existing operator meter/inverter contracts. */
bool screen_runtime_refresh_devices(void);

/* Existing alarms/events contracts. */
bool screen_runtime_refresh_operations(void);

/* Convenience for commissioning/bench refresh; no internal loop is created. */
bool screen_runtime_refresh_all(void);

#ifdef __cplusplus
}
#endif
