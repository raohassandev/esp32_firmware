#pragma once

#include "cJSON.h"
#include "meter_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ONE SERIALIZER FOR THE METER MEASUREMENTS, USED BY EVERY VIEW.
 *
 * This product publishes meter data twice: the engineering view, and the
 * operator view that an unauthenticated browser gets. They differ in what they
 * are allowed to reveal -- hosts, unit ids and register addresses belong to the
 * engineer -- but they must NOT differ in what a measurement means. Two hand-
 * written copies of "volts, per phase, null when absent" drift, and the first
 * symptom of the drift is an operator and an engineer standing at the same panel
 * reading different numbers off the same instrument.
 *
 * MEASUREMENTS ARE NOT ENGINEERING DETAIL. Voltage, current, power factor,
 * frequency and the energy counters are what the person who owns the plant looks
 * at to satisfy themselves the controller is working, and they are printed on the
 * meter's own front panel. Withholding them from the operator view would hide the
 * evidence from exactly the reader it exists for. What stays behind the gate is
 * how the firmware TALKS to the meter, not what the meter says.
 */

/* Age in milliseconds, or null. Shared because the operator view and the
 * engineering view must agree on what "unknown age" looks like. */
void meter_json_add_age(cJSON *parent, const char *name, bool available,
                        uint32_t current_ms, uint32_t event_ms);

/* Per-phase active power, kW, import-positive; null per phase where the phase
 * was not read. Per phase and not one flag: WHICH phase is missing is what
 * decides whether control can use the worst conductor or must fall back. */
void meter_json_add_phase_power(cJSON *parent, const meter_data_t *data, bool has_data);

/* The full instantaneous set, exactly as the instrument reported it, with its
 * own age -- it is polled on a slower cadence than the control measurement, so
 * one age for both would be a lie about at least one. */
void meter_json_add_measurements(cJSON *parent, const meter_data_t *data, uint32_t current_ms);

/* The cumulative energy counters, with their own age for the same reason. */
void meter_json_add_energy(cJSON *parent, const meter_data_t *data, uint32_t current_ms);

#ifdef __cplusplus
}
#endif
