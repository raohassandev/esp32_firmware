#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "em500_block.h"

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
    /* Everything else the instrument measures: voltages, currents, power factor,
     * frequency, reactive and apparent power, asymmetry, neutral current.
     *
     * NOT part of the control path. The control loop needs active power and
     * nothing more, and it needs it at the poll rate. This block is what a person
     * needs -- to see that the meter is wired correctly, that the CTs are on the
     * right phases, that a limit is being enforced for a reason the totals do not
     * show. It is therefore polled on its own slower cadence and its failure never
     * fails a control cycle.
     *
     * measurements.valid is false until the first block read succeeds, so a page
     * can say "not yet read" instead of drawing a plant at 0 V. */
    em500_measurements_t measurements;
    uint32_t measurements_updated_ms;
    /* Cumulative energy. Counters, not rates: they only ever move forward, they
     * change slowly, and they are what a factory owner actually checks -- so they
     * are polled slower still. */
    em500_energy_t energy;
    uint32_t energy_updated_ms;
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
