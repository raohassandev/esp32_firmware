#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOURCE_COMMISSIONING_MAX_METERS 4U
#define SOURCE_COMMISSIONING_MESSAGE_MAX 192U

typedef enum {
    SOURCE_COMMISSION_AUTH_OK = 0,
    SOURCE_COMMISSION_AUTH_DENIED,
    SOURCE_COMMISSION_AUTH_LOCKED,
    SOURCE_COMMISSION_AUTH_ERROR,
} source_commission_auth_result_t;

typedef struct {
    bool ok;
    bool restart_required;
    char message[SOURCE_COMMISSIONING_MESSAGE_MAX];
} source_commission_action_result_t;

typedef struct {
    uint8_t meter_index;
    uint8_t function_code;
    uint16_t address;
    uint16_t mask;
    uint16_t active_value;
} source_commission_signal_t;

typedef struct {
    bool valid;
    bool unlocked;
    bool setup_required;
    bool restart_required;
    bool evidence_enabled;
    source_commission_signal_t grid_available;
    source_commission_signal_t grid_breaker_closed;
    uint32_t evidence_poll_interval_ms;
    uint32_t evidence_stale_timeout_ms;
    uint32_t grid_loss_trip_ms;
    uint32_t grid_recovery_stable_ms;
} source_commission_config_t;

typedef struct {
    void *context;
    source_commission_auth_result_t (*unlock)(void *context,
                                               const char *credential,
                                               uint32_t *retry_after_ms,
                                               bool *setup_required);
    void (*lock)(void *context);
    bool (*read_config)(void *context, source_commission_config_t *out);
    bool (*save_config)(void *context,
                        const source_commission_config_t *config,
                        source_commission_action_result_t *result);
    bool (*restart_controller)(void *context,
                               source_commission_action_result_t *result);
} source_commission_backend_t;

lv_obj_t *source_commissioning_screen_create(lv_obj_t *parent);
void source_commissioning_screen_set_backend(const source_commission_backend_t *backend);
void source_commissioning_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
