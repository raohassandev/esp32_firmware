#pragma once
#include "commissioning_gate.h"
#include "esp_err.h"
#include "control_types.h"

esp_err_t control_engine_init(void);
void control_engine_get_status(control_status_t *out_status);

/*
 * The commissioning gate evaluated by the most recent control cycle (P0-6).
 *
 * Automatic control cannot engage while this reports !commissioned - the gate is
 * applied to the control task's own enable, not merely reported. Reads cached
 * state under the engine's lock; performs no I/O and is safe to call from an
 * HTTP handler.
 *
 * Before the first cycle has run, and if the engine failed to initialise, this
 * returns the fully fail-closed evaluation rather than an empty struct.
 */
void control_engine_get_commissioning(commissioning_status_t *out_status);

/* Latches the running controller disabled. The control task applies a safe zero
 * on its next cycle; the caller never performs inverter I/O synchronously. */
void control_engine_force_disable(void);
