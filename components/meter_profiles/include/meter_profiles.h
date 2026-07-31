#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * COMMISSIONED METER FAMILIES, WITH THEIR REGISTER MAPS.
 *
 * WHY THIS EXISTS. Meters were commissioned as anonymous Modbus endpoints: a
 * host, a unit id, a register address, a data type and a scale, all typed in by
 * hand on every site. Two things went wrong with that, and both happened on real
 * plant.
 *
 * The register map was retyped from memory, so a wizard default (register 0,
 * scale 0.001) overwrote a working configuration and a meter reading 372 kW read
 * 25 kW instead -- a 15x error with every field on screen looking plausible.
 *
 * And the instrument had no name, so the only thing the firmware could key the
 * EM500's 0x2100 bitmask semantics on was the commissioned grid value, which is
 * a property of the site's WIRING rather than of the meter. That is how the
 * bitmask rule leaked onto instruments it was never measured against.
 *
 * A profile is therefore a NAMED family with a CITED map. The citation is not
 * decoration: it is what separates a register this firmware knows from one
 * somebody remembered.
 *
 * WHAT IS DELIBERATELY NOT HERE. Only families whose map has been transcribed
 * from a manufacturer document appear with registers. Every other catalogue
 * entry is present with has_register_map false, so it commissions as a manual
 * endpoint and says so, rather than being absent (which reads as "unsupported")
 * or being given invented addresses (which is worse than either).
 */

typedef struct {
    /* Matches meter_model_t, so a profile and a commissioned model can never
     * disagree about which instrument is being described. */
    uint32_t model;
    const char *id;
    const char *manufacturer;
    const char *model_family;

    /* False for a family with no transcribed map: the engineer supplies the
     * registers and the UI must say the values are theirs, not the manual's. */
    bool has_register_map;

    /* The one measurement the control loop regulates against. PDU addresses, as
     * they go on the wire. */
    uint8_t function_code;             /* 3 holding, 4 input */
    uint16_t active_power_address;
    uint8_t active_power_type;         /* modbus_data_type_t */
    uint8_t active_power_order;        /* modbus_word_order_t */
    float active_power_scale;          /* raw * scale = kW */

    /* Per-phase active power, for sites whose export limit must hold on the
     * worst phase rather than on the total. Zero address means the family's map
     * does not state them and per-phase control cannot be offered. */
    bool has_phase_power;
    uint16_t phase_power_address[3];

    /* Whether this family's manual documents its full instantaneous measurement
     * set, and its cumulative energy counters, as contiguous runs that can each
     * be read in ONE transaction -- the layout in em500_block.h.
     *
     * These are not control inputs. They are what a person needs to see that the
     * meter is wired the way the drawing says and that the controller is acting
     * on real measurements. A family without the flag simply has no such page,
     * rather than a page of registers guessed from a family that does. */
    bool has_measurement_block;
    bool has_energy_block;

    /* The digital-input word this family uses for source detection, and whether
     * its bits are an OR of all inputs (a bitmask) or an enumeration. Zero
     * address means the family has no documented source indication, and source
     * detection must not be offered against it. */
    bool has_source_input;
    uint16_t source_input_address;
    bool source_input_is_bitmask;

    /* Where every address above came from, verbatim enough to check. */
    const char *manual_reference;
} meter_profile_t;

/* The catalogue. Count is the number of entries. */
const meter_profile_t *meter_profiles_all(uint8_t *out_count);

/* The profile for a commissioned meter_model_t, or NULL when the model has no
 * profile -- which includes UNDECLARED. NULL means "no map", never "use a
 * default map". */
const meter_profile_t *meter_profile_for_model(uint32_t model);

#ifdef __cplusplus
}
#endif
