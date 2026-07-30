#pragma once
#include "commissioning_gate.h"
#include "esp_err.h"
#include "control_types.h"
#include "generator_fleet_limit.h"

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

/*
 * The generator fleet verdict the control loop last acted on: which engines it
 * believed were on the bus, the aggregate rating, the minimum-loading floor it
 * derived and the safe PV that followed.
 *
 * Returns false until the loop has evaluated once, so "no verdict yet" is
 * distinguishable from "a verdict of zero". Reads a snapshot under a spinlock and
 * performs no I/O, so it is safe from an HTTP handler.
 *
 * Publish THIS rather than recomputing a floor from configuration. A recomputation
 * over the commissioned set answers a different question -- what the floor would be
 * if every in-service engine were running -- and offering it as the runtime answer
 * would misreport why PV is being held down.
 */
bool control_engine_get_generator_fleet(generator_fleet_limit_t *out_limit);

/* Latches the running controller disabled. The control task applies a safe zero
 * on its next cycle; the caller never performs inverter I/O synchronously. */
void control_engine_force_disable(void);
