#pragma once

#include "cJSON.h"
#include "inverter_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ONE SERIALIZER FOR WHAT THE INVERTER MEASURES, USED BY EVERY VIEW.
 *
 * Same reasoning as meter_json.h: /api/inverters answers twice, and the two
 * views may differ in what they REVEAL about how the firmware reaches the
 * machine, never in what a measurement means.
 *
 * THE DISTINCTION THIS FILE EXISTS TO KEEP. An inverter page carries two kinds
 * of number that look alike and are not:
 *
 *   COMMANDED is this firmware's own belief -- a percentage it decided and
 *   wrote. It says nothing about the machine.
 *
 *   MEASURED is what the machine reported back. It is the only evidence that
 *   the command had any effect.
 *
 * Printing them in the same style, side by side, with the same weight, is how a
 * screen convinces someone that a plant is curtailed when nothing was curtailed.
 * They are serialized into separate objects here, and rendered differently, on
 * purpose.
 */

/* Everything the machine measures: DC strings, AC per phase, yield, temperature,
 * device status. Carries its own age -- it is polled on a slower cadence than
 * the control read, so a single age would misreport one of them. */
void inverter_json_add_measurements(cJSON *parent, const inverter_data_t *data,
                                    uint32_t current_ms);

#ifdef __cplusplus
}
#endif
