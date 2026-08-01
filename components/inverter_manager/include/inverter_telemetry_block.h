#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * WHAT THE INVERTER MEASURES.
 *
 * Until now this firmware read one number off an inverter -- active power -- and
 * only where a profile declared it. No DC, no string voltages, no AC per phase,
 * no yield, no temperature, no device status. So the inverter page could show
 * what the controller had COMMANDED and nothing about what the machine was
 * actually doing, which is the wrong half: a commanded percentage is this
 * firmware's own belief, and the whole question a plant owner brings to the page
 * is whether the machine agrees.
 *
 * SOURCE, AND ONLY THIS SOURCE. "Solar Inverter Modbus Interface Definitions
 * (V3.0)", Huawei Technologies, Issue 01 (2023-01-17), section 3, Register
 * Definitions. Every address, type, unit and gain below is transcribed from that
 * document's own table. Nothing is inferred from another brand, another firmware,
 * or a value that looked right on a bench.
 *
 * ONE TRANSACTION. 32016 through 32117 is 102 contiguous registers, inside the
 * 125-register limit of a single FC03. Read field by field this is thirty round
 * trips against a device the control loop also writes to; the block makes the
 * difference between "acquired once a second" and "not acquired at all".
 *
 * READING IS NOT WRITING, AND THIS GRANTS NOTHING. Every register here is marked
 * RO in the manual. Acquiring them does not qualify a profile to be commanded,
 * does not promote it out of LAB_ONLY, and is not evidence about physical
 * equipment. The write gates are unchanged and unrelated: see
 * inverter_profiles.h. This exists so a person can SEE the machine, which is
 * exactly the thing a controller that may not command it should still offer.
 *
 * GAIN IS A DIVISOR. The manual's "gain" column means value = raw / gain. A gain
 * applied as a multiplier turns a 230 V string into 2300 V, which is the kind of
 * number that gets believed because it is on a screen.
 *
 * SIGNEDNESS IS PER FIELD, from the manual's own Type column. PV voltage and
 * current, active and reactive power, power factor and internal temperature are
 * signed (I16/I32); grid voltages, frequency, efficiency and every energy
 * counter are unsigned (U16/U32). An inverter at night genuinely reads negative
 * active power -- it consumes -- and reading that field unsigned reports roughly
 * 4.29 million kW of production from a plant in the dark.
 */

#define INVERTER_HUAWEI_BLOCK_START 32016u
/* 32016..32117 inclusive. The last field, "electricity generated in current
 * month" at 32116, is two registers. */
#define INVERTER_HUAWEI_BLOCK_REGISTERS 102u

/* Number of PV strings decoded.
 *
 * The manual documents PV1 voltage/current at 32016/32017 and PV20
 * voltage/current at 32054/32055, which pins a fixed two-register stride at both
 * ends of the run. Four strings covers the commercial machines this product is
 * commissioned against; the rest are read as part of the block and simply not
 * interpreted, the same way the meter's undocumented gap is. */
#define INVERTER_PV_STRINGS 4

typedef struct {
    /* DC side. */
    float pv_voltage_v[INVERTER_PV_STRINGS];
    float pv_current_a[INVERTER_PV_STRINGS];
    float dc_power_kw;

    /* AC side. Line voltages are A-B, B-C, C-A; phase voltages and currents are
     * A, B, C. The manual notes several of these are invalid on single-phase
     * output modes, where the device reports zero -- which this passes through
     * unaltered rather than deciding for the reader. */
    float line_voltage_v[3];
    float phase_voltage_v[3];
    float phase_current_a[3];

    float active_power_kw;        /* signed: negative is consumption */
    float reactive_power_kvar;    /* signed */
    float peak_active_power_today_kw;
    float power_factor;           /* signed: sign carries leading/lagging */
    float frequency_hz;
    float efficiency_percent;
    float internal_temperature_c; /* signed */
    float insulation_resistance_mohm;

    /* Raw, deliberately. Device Status is an enumeration and Fault Code is an
     * index into a table this firmware has not transcribed. Publishing the raw
     * value lets a person quote it to the manufacturer; inventing a label for it
     * would be a guess wearing the clothes of a diagnosis. */
    uint16_t device_status_raw;
    uint16_t fault_code_raw;

    /* Cumulative energy, kWh. */
    float total_yield_kwh;
    float total_dc_input_kwh;
    float daily_yield_kwh;
    float month_yield_kwh;

    /* False when the block could not be decoded at all. Not per field: they
     * arrive in one transaction, so they are all present or all absent, and a
     * per-field freshness would be a property this instrument does not offer. */
    bool valid;
} inverter_measurements_t;

/*
 * Decodes a Huawei block read from INVERTER_HUAWEI_BLOCK_START of
 * INVERTER_HUAWEI_BLOCK_REGISTERS words.
 *
 * `words` is raw big-endian Modbus register data as received. Returns false and
 * leaves `out` zeroed with valid = false on a short or NULL input -- never a
 * partial fill, because a struct half-populated with real numbers and half with
 * zeros is indistinguishable from an inverter that is half asleep.
 */
bool inverter_huawei_block_decode(const uint16_t *words, uint16_t count,
                                  inverter_measurements_t *out);

#ifdef __cplusplus
}
#endif
