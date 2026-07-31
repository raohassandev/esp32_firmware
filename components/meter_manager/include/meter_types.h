#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float active_power_kw;
    /* Per-phase active power, kW, import-positive.
     *
     * Read only when the commissioned model has a transcribed map with phase
     * addresses AND the site asked for per-phase control. phase_valid is per
     * phase and NOT a single flag: phase_selection_evaluate() needs all three to
     * identify the worst conductor, and knowing WHICH one is missing is what
     * lets it fall back to the total honestly rather than pick the worst of the
     * two that answered.
     *
     * HMI-EVIDENCE: the three phase readings, side by side with the total, are
     * how an operator sees an unbalanced site at a glance -- and how an engineer
     * explains why a limit is being enforced that the total does not appear to
     * justify. Computed here, not yet on a screen. */
    float phase_power_kw[3];
    bool phase_valid[3];
    bool online;
    bool degraded;
    bool connection_initialized;
    uint32_t last_update_ms;
    uint32_t last_attempt_ms;
    uint32_t last_response_time_ms;
    uint32_t success_count;
    uint32_t response_errors;
    uint32_t consecutive_failures;
    uint32_t current_poll_delay_ms;
    uint8_t recent_sample_count;
    uint8_t recent_success_percent;
    int32_t last_error;
    bool last_modbus_exception_valid;
    uint8_t last_modbus_exception_function;
    uint8_t last_modbus_exception_code;
    uint32_t last_modbus_exception_ms;
    uint32_t modbus_exception_count;
} meter_data_t;
