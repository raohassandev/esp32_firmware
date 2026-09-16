#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "screen_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_COMMISSIONING_MAX_METERS 4U
#define SCREEN_COMMISSIONING_MAX_INVERTERS 12U
#define SCREEN_COMMISSIONING_MAX_GENERATORS 3U
#define SCREEN_COMMISSIONING_MAX_PROFILES 12U
#define SCREEN_COMMISSIONING_HOST_MAX 64U
#define SCREEN_COMMISSIONING_PROFILE_ID_MAX 40U
#define SCREEN_COMMISSIONING_MESSAGE_MAX 192U

typedef enum {
    SCREEN_COMMISSION_AUTH_OK = 0,
    SCREEN_COMMISSION_AUTH_DENIED,
    SCREEN_COMMISSION_AUTH_LOCKED,
    SCREEN_COMMISSION_AUTH_ERROR,
} screen_commission_auth_result_t;

typedef struct {
    bool ok;
    bool restart_required;
    char message[SCREEN_COMMISSIONING_MESSAGE_MAX];
} screen_commission_action_result_t;

typedef struct {
    bool enabled;
    char name[24];
    char host[SCREEN_COMMISSIONING_HOST_MAX];
    uint16_t port;
    uint8_t unit_id;
    uint32_t timeout_ms;
    uint8_t function_code;
    uint16_t active_power_address;
    uint8_t data_type;
    uint8_t word_order;
    float scale;
    uint32_t poll_ms;
    uint8_t role;
    uint8_t generator_index;
    uint32_t model;
    uint32_t phase_basis;
    bool runtime_online;
    bool runtime_has_data;
    bool runtime_stale;
} screen_commission_meter_t;

typedef struct {
    bool enabled;
    char name[24];
    char host[SCREEN_COMMISSIONING_HOST_MAX];
    uint16_t port;
    uint8_t unit_id;
    uint32_t timeout_ms;
    float rated_kw;
    uint32_t comms_failsafe_ms;
    char profile_id[SCREEN_COMMISSIONING_PROFILE_ID_MAX];
    bool runtime_online;
    bool telemetry_valid;
    bool identity_verified;
} screen_commission_inverter_t;

typedef struct {
    char id[SCREEN_COMMISSIONING_PROFILE_ID_MAX];
    char manufacturer[24];
    char model[48];
    bool read_allowed;
    bool write_allowed;
    bool deferred_this_phase;
} screen_commission_profile_t;

typedef struct {
    bool enabled;
    float rated_kw;
    float minimum_loading_percent;
    float reserve_kw;
    float reverse_power_margin_kw;
    uint8_t role;
    float base_load_kw;
} screen_commission_generator_t;

typedef struct {
    uint8_t policy;
    uint8_t meter_orientation;
    float export_limit_kw;
    float minimum_import_kw;
    uint8_t load_sharing_mode;
    float base_load_tolerance_kw;
    float base_load_tolerance_percent;
    screen_commission_generator_t generators[SCREEN_COMMISSIONING_MAX_GENERATORS];

    float grid_import_target_kw;
    float deadband_kw;
    float kp;
    float ki;
    uint32_t control_interval_ms;
    uint32_t meter_stale_timeout_ms;
    bool grid_ramp_enabled;
    float grid_ramp_up_percent_per_second;
    float grid_ramp_down_percent_per_second;
    bool generator_ramp_enabled;
    float generator_ramp_up_percent_per_second;
    float generator_ramp_down_percent_per_second;
    float urgent_loading_fraction;
    float urgent_ramp_multiplier;
} screen_commission_plant_t;

typedef struct {
    bool valid;
    bool unlocked;
    bool setup_required;
    bool restart_required;
    char device_name[32];
    uint8_t meter_count;
    screen_commission_meter_t meters[SCREEN_COMMISSIONING_MAX_METERS];
    uint8_t inverter_count;
    screen_commission_inverter_t inverters[SCREEN_COMMISSIONING_MAX_INVERTERS];
    uint8_t profile_count;
    screen_commission_profile_t profiles[SCREEN_COMMISSIONING_MAX_PROFILES];
    screen_commission_plant_t plant;
} screen_commissioning_config_t;

typedef struct {
    void *context;
    screen_commission_auth_result_t (*unlock)(void *context,
                                               const char *credential,
                                               uint32_t *retry_after_ms,
                                               bool *setup_required);
    void (*lock)(void *context);
    bool (*read_config)(void *context, screen_commissioning_config_t *out);
    bool (*save_site)(void *context,
                      const char *device_name,
                      screen_commission_action_result_t *result);
    bool (*save_meter)(void *context,
                       uint8_t index,
                       const screen_commission_meter_t *meter,
                       screen_commission_action_result_t *result);
    bool (*save_inverter)(void *context,
                          uint8_t index,
                          const screen_commission_inverter_t *inverter,
                          screen_commission_action_result_t *result);
    bool (*save_plant)(void *context,
                       const screen_commission_plant_t *plant,
                       screen_commission_action_result_t *result);
    bool (*set_control_enabled)(void *context,
                                bool enabled,
                                screen_commission_action_result_t *result);
    bool (*restart_controller)(void *context,
                               screen_commission_action_result_t *result);
} screen_commissioning_backend_t;

lv_obj_t *commissioning_screen_create(lv_obj_t *parent);
void commissioning_screen_set_backend(const screen_commissioning_backend_t *backend);
void commissioning_screen_apply_gate(const screen_commissioning_snapshot_t *snapshot);
void commissioning_screen_apply_status(const screen_status_snapshot_t *snapshot);
void commissioning_screen_apply_meters(const screen_meters_snapshot_t *snapshot);
void commissioning_screen_apply_inverters(const screen_inverters_snapshot_t *snapshot);
void commissioning_screen_apply_telemetry(const screen_telemetry_snapshot_t *snapshot);
void commissioning_screen_show_unavailable(void);

#ifdef __cplusplus
}
#endif
