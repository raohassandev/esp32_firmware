#include "grid_control_gate.h"

#include <string.h>

void grid_control_gate_reset(grid_gate_memory_t *memory)
{
    if (memory) memset(memory, 0, sizeof(*memory));
}

static uint32_t elapsed_ms(uint32_t now, uint32_t since)
{
    return now - since;
}

grid_gate_output_t grid_control_gate_step(grid_gate_memory_t *memory,
                                          const grid_gate_input_t *input)
{
    grid_gate_output_t output = {
        .state = GRID_GATE_UNCONFIGURED,
        .control_allowed = false,
        .recovery_stable = false,
        .loss_confirmed = false,
    };
    if (!memory || !input || !input->configured) {
        grid_control_gate_reset(memory);
        return output;
    }

    const bool healthy = input->evidence_fresh &&
                         input->source_mode == SOURCE_MODE_GRID_ONLY &&
                         input->source_control_allowed;
    const bool conflict = input->evidence_fresh &&
                          input->source_mode == SOURCE_MODE_CONFLICT;

    if (healthy) {
        memory->loss_tracking = false;
        memory->loss_since_ms = 0U;
        if (!memory->recovery_tracking) {
            memory->recovery_tracking = true;
            memory->recovery_since_ms = input->timestamp_ms;
        }
        output.recovery_stable = input->recovery_stable_ms == 0U ||
                                 elapsed_ms(input->timestamp_ms,
                                            memory->recovery_since_ms) >=
                                     input->recovery_stable_ms;
        output.control_allowed = output.recovery_stable;
        output.state = output.recovery_stable ? GRID_GATE_READY
                                              : GRID_GATE_RECOVERY_STABILIZING;
        return output;
    }

    /* Any uncertain, stale, open-breaker or contradictory evidence blocks PV
     * immediately. loss_trip_ms only classifies a persistent outage; it never
     * delays the fail-closed command path. */
    memory->recovery_tracking = false;
    memory->recovery_since_ms = 0U;
    if (!memory->loss_tracking) {
        memory->loss_tracking = true;
        memory->loss_since_ms = input->timestamp_ms;
    }
    output.loss_confirmed = input->loss_trip_ms == 0U ||
                            elapsed_ms(input->timestamp_ms,
                                       memory->loss_since_ms) >=
                                input->loss_trip_ms;

    if (conflict) {
        output.state = GRID_GATE_CONFLICT;
    } else if (output.loss_confirmed) {
        output.state = GRID_GATE_LOST;
    } else {
        output.state = GRID_GATE_WAITING_EVIDENCE;
    }
    return output;
}

const char *grid_control_gate_state_name(grid_gate_state_t state)
{
    switch (state) {
    case GRID_GATE_UNCONFIGURED: return "unconfigured";
    case GRID_GATE_WAITING_EVIDENCE: return "waiting_evidence";
    case GRID_GATE_RECOVERY_STABILIZING: return "recovery_stabilizing";
    case GRID_GATE_READY: return "ready";
    case GRID_GATE_LOST: return "grid_lost";
    case GRID_GATE_CONFLICT: return "conflict";
    default: return "invalid";
    }
}
