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

    /*
     * WHICH SOURCES THIS GATE WILL RELEASE CONTROL AGAINST.
     *
     * It used to release for SOURCE_MODE_GRID_ONLY alone, which meant automatic
     * control was impossible whenever a generator carried the plant: PV was
     * driven to zero rather than limited. On a PV-DG controller that is the
     * opposite of the product -- the generator case is the one the minimum
     * loading, reverse-power margin and generator ramp profile all exist for,
     * and every one of them was computed each cycle and then discarded.
     *
     * GENERATOR_ONLY and ISLAND are both "a generator is carrying the plant
     * alone", and the control engine derives a generator-safe PV limit for
     * both, so both are released.
     *
     * GRID_GENERATOR_SYNC is released now that the strategy exists. One PV
     * setpoint has to satisfy two objectives -- hold the generator above its
     * floor AND respect the grid export policy -- and it does so by taking the
     * MORE RESTRICTIVE of the two: the grid policy sets the target the loop
     * drives toward, and the generator floor caps the maximum it may reach. The
     * conservative direction is the correct one when two protections disagree.
     *
     * ITS LIMITATION IS STATED RATHER THAN HIDDEN. The floor is derived as it is
     * for a generator carrying alone: reduce PV and the generator picks up load.
     * On a plant where the generator is BASE-LOADED, its own controller holds it
     * at a fixed kW and PV changes flow to the grid instead, so the floor does
     * not bind the way this assumes and PV may be curtailed harder than the
     * machine requires. That errs toward a more loaded generator, which is the
     * safe direction, and it costs yield rather than protection. It has not been
     * exercised on a synchronised plant; see docs/RELEASE_READINESS.md.
     *
     * TRANSFER, NO_SOURCE and UNKNOWN stay closed for the reasons they always
     * did.
     */
    const bool source_carrying = input->source_mode == SOURCE_MODE_GRID_ONLY ||
                                 input->source_mode == SOURCE_MODE_GENERATOR_ONLY ||
                                 input->source_mode == SOURCE_MODE_ISLAND ||
                                 input->source_mode == SOURCE_MODE_GRID_GENERATOR_SYNC;
    const bool healthy = input->evidence_fresh && source_carrying &&
                         input->source_control_allowed;
    const bool conflict = input->evidence_fresh &&
                          input->source_mode == SOURCE_MODE_CONFLICT;

    if (healthy) {
        memory->loss_tracking = false;
        memory->loss_since_ms = 0U;
        /* A CHANGEOVER RESTARTS THE STABILISATION TIMER.
         *
         * Grid to generator and back are both healthy states, so without this
         * the timer would keep running straight through a transfer and PV would
         * be commanded against a bus that had just changed underneath it. The
         * timer already exists to hold PV until a source has proven steady;
         * that requirement is no weaker when the new source is a generator. */
        const bool source_changed = !memory->mode_known ||
                                    memory->last_mode != input->source_mode;
        memory->last_mode = input->source_mode;
        memory->mode_known = true;
        if (!memory->recovery_tracking || source_changed) {
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
