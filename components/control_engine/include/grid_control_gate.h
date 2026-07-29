#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "source_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GRID_GATE_UNCONFIGURED = 0,
    GRID_GATE_WAITING_EVIDENCE,
    GRID_GATE_RECOVERY_STABILIZING,
    GRID_GATE_READY,
    GRID_GATE_LOST,
    GRID_GATE_CONFLICT
} grid_gate_state_t;

typedef struct {
    bool recovery_tracking;
    bool loss_tracking;
    uint32_t recovery_since_ms;
    uint32_t loss_since_ms;
} grid_gate_memory_t;

typedef struct {
    bool configured;
    bool evidence_fresh;
    source_mode_t source_mode;
    bool source_control_allowed;
    uint32_t timestamp_ms;
    uint32_t loss_trip_ms;
    uint32_t recovery_stable_ms;
} grid_gate_input_t;

typedef struct {
    grid_gate_state_t state;
    bool control_allowed;
    bool recovery_stable;
    bool loss_confirmed;
} grid_gate_output_t;

grid_gate_output_t grid_control_gate_step(grid_gate_memory_t *memory,
                                          const grid_gate_input_t *input);
void grid_control_gate_reset(grid_gate_memory_t *memory);
const char *grid_control_gate_state_name(grid_gate_state_t state);

#ifdef __cplusplus
}
#endif
