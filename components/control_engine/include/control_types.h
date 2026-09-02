#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_MODE_DISABLED = 0,
    APP_MODE_GRID,
    APP_MODE_GENERATOR,
    APP_MODE_MANUAL,
    APP_MODE_FAILSAFE,
    APP_MODE_EMERGENCY
} app_mode_t;

typedef struct {
    bool enabled;
    app_mode_t mode;
    float grid_power_kw;
    float raw_grid_power_kw;
    float grid_target_kw;
    float error_kw;
    float requested_pv_kw;
    float applied_pv_kw;
    uint8_t grid_policy;
    uint8_t source_mode;
    uint8_t grid_gate_state;
    bool grid_evidence_configured;
    bool grid_evidence_fresh;
    bool grid_available;
    bool grid_breaker_closed;
    /* Strong generator/transfer evidence is runtime-only status. These fields
     * are never persisted, so extending this struct has no NVS schema impact. */
    bool generator_evidence_configured;
    bool generator_running;
    bool generator_breaker_closed;
    bool transfer_active;
    bool grid_generator_synchronized;
    bool grid_recovery_stable;
    bool grid_loss_confirmed;
    uint32_t grid_evidence_age_ms;
    uint32_t grid_evidence_success_count;
    uint32_t grid_evidence_error_count;
    int32_t grid_evidence_last_error;
    uint16_t grid_available_raw;
    uint16_t grid_breaker_raw;
    uint16_t generator_running_raw;
    uint16_t generator_breaker_raw;
    uint16_t transfer_active_raw;
    uint16_t grid_generator_synchronized_raw;
    uint32_t alarm_flags;
    uint32_t last_cycle_ms;
    /* Control authority, so the interface can state one answer instead of
     * several scattered phrases. This struct is runtime only - it is never
     * persisted, so extending it carries no schema consequence.
     *
     * command_authority is the single question that matters: is the controller
     * permitted to write to the inverters right now. inhibit_reason carries the
     * firmware's own words for why not, so the interface never paraphrases a
     * safety decision it did not make. */
    bool command_authority;
    char inhibit_reason[128];
    uint32_t last_command_ms;       /* last accepted inverter write, 0 = never */
    uint32_t last_authority_change_ms;
} control_status_t;
