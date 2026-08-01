#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE EM-500 INSTANTANEOUS MEASUREMENT BLOCK.
 *
 * Everything the meter measures about the here-and-now lives in one contiguous
 * run of registers, so it is read in ONE Modbus transaction rather than twenty.
 * That is not a micro-optimisation: the control loop shares this bus, the poll
 * interval on a commissioned site is 300 ms, and twenty round trips per cycle
 * would not fit -- so the alternative to a block read is not "slower", it is
 * "these values are never acquired at all", which is the state this replaces.
 *
 * Source: "EM-500 Three-Phase Energy Meter", MEASURES SUPPLIED BY SERIAL
 * COMMUNICATION PROTOCOL, Table 1. The header states "To be used with functions
 * 03 and 04". Every address and unit below is transcribed from that table; the
 * gap at 0x002C..0x0031 is not described there and is read as part of the block
 * and ignored, never interpreted.
 *
 * WHAT "EQV." MEANS IS NOT UNIFORM, AND THE MANUAL DOES NOT SAY SO. Measured on
 * the installed meter 2026-08-01: the equivalent VOLTAGE is the average of the
 * three phases (249.7/249.3/248.8 -> 249.25) while the equivalent CURRENT is
 * their sum (307.4/309.4/312.6 -> 929.4). Anything that presents both under one
 * heading tells the reader that 249 V is a total of three 249 V phases.
 *
 * SIGNEDNESS IS NOT COSMETIC. The manual gives active and reactive power and
 * power factor as "Signed long" and voltages, currents and apparent power as
 * "Unsigned long". Reading active power unsigned turns a modest export into
 * roughly 42.9 million kW of import -- which is exactly the class of error this
 * product exists to avoid, so the signedness of each field is carried in the
 * table rather than assumed uniform.
 */

#define EM500_BLOCK_START 0x0002u
/* 0x0002 through 0x0049 inclusive: the last field, neutral current at 0x0048,
 * is two words. Well inside the 125-register limit of one FC03 request. */
#define EM500_BLOCK_REGISTERS 72u

typedef struct {
    /* Per phase. */
    float phase_voltage_v[3];
    float current_a[3];
    float active_power_kw[3];
    float reactive_power_kvar[3];
    float apparent_power_kva[3];
    float power_factor[3];
    /* Line to line: L1-L2, L2-L3, L3-L1. */
    float line_voltage_v[3];
    /* Whole installation. "Eqv." in the manual's language. */
    float frequency_hz;
    float equivalent_phase_voltage_v;
    float equivalent_line_voltage_v;
    float equivalent_current_a;
    float total_active_power_kw;
    float total_reactive_power_kvar;
    float total_apparent_power_kva;
    float total_power_factor;
    /* Percent. A three-phase load that is badly out of balance is the reason a
     * limit enforced on the total can be satisfied while one phase exports, so
     * the meter's own asymmetry figures are worth having next to that decision. */
    float voltage_asymmetry_line_percent;
    float voltage_asymmetry_phase_percent;
    float current_asymmetry_percent;
    /*
     * 0048H, WHICH THE MANUAL CALLS "NEUTRAL CURRENT" AND WHICH THIS INSTRUMENT
     * DOES NOT APPEAR TO USE THAT WAY.
     *
     * Measured on the installed EM-500 on 2026-08-01, against a real 216 kW
     * load: the three phases carried 307.4 / 309.4 / 312.6 A, disagreeing by
     * 0.4%, and this register reported 930.8 A. A load that balanced puts a few
     * amps in the neutral, not three times the phase current. 930.8 is 98.8% of
     * the arithmetic sum of the phases -- which is what 0038H ("Eqv. Current")
     * reports exactly, so this looks like a second aggregate rather than a
     * neutral measurement.
     *
     * It is decoded and published because an engineer chasing it needs the
     * number. It is NOT rendered on any page under that name: a plausible value
     * with a confident label is how somebody sizes a neutral conductor wrongly.
     * Give it a meaning only when the manufacturer or a clamp meter says what it
     * is.
     */
    float neutral_current_a;
    /* False when the block could not be decoded at all. Individual fields are
     * never marked invalid separately: they arrive in one transaction, so they
     * are all present or all absent, and pretending otherwise would invent a
     * per-field freshness this instrument does not provide. */
    bool valid;
} em500_measurements_t;

/*
 * Decodes a block read from EM500_BLOCK_START of EM500_BLOCK_REGISTERS words.
 *
 * `words` is raw big-endian Modbus register data as received. Returns false and
 * leaves `out` zeroed with valid = false when the input is the wrong size or
 * NULL -- never a partial fill, because a struct half-populated with real
 * numbers and half with zeros is indistinguishable from a plant that is half
 * idle.
 */
bool em500_block_decode(const uint16_t *words, uint16_t count, em500_measurements_t *out);

/*
 * THE ENERGY COUNTERS.
 *
 * Source: same manual, TABLE 3. Two things differ from Table 1 and both matter.
 *
 * FOUR WORDS, NOT TWO. Table 1 measures are "2" words; every energy counter is
 * "4". They are 64-bit. Decoding one as 32 bits reads the top half of the value
 * -- which on a new meter is zero, so the counter reads 0.00 kWh forever and
 * looks like a meter that is not counting rather than a decode that is wrong.
 *
 * THEY ARE COUNTERS. They only move forward, they change by the hour rather than
 * the second, and they are the numbers a factory owner checks against the utility
 * bill. Polled slowly, and a failure to read them never disturbs control.
 *
 * WHAT IS DELIBERATELY ABSENT. The manual's Table 3 also lists per-phase and
 * per-tariff energies, but the document gives 1B48H two different meanings in two
 * places -- "Imp. Active Energy Tariff 1" in one column and "L1 imp. Active
 * Energy" in another. When the source contradicts itself the address is not
 * known, so those counters are not read. Only 0x1B20..0x1B47, where the manual
 * says one thing, is decoded here.
 */
#define EM500_ENERGY_START 0x1B20u
/* 0x1B20..0x1B47: ten counters of four words each. */
#define EM500_ENERGY_REGISTERS 40u

typedef struct {
    /* kWh. double rather than float on purpose: a float carries about seven
     * significant digits, and a plant that has moved 40 GWh would start losing
     * whole kWh off the end of its own meter reading. */
    double total_import_active_kwh;
    double total_export_active_kwh;
    double total_import_reactive_kvarh;
    double total_export_reactive_kvarh;
    double total_apparent_kvah;
    /* "Partial" counters are the resettable ones -- the meter's own trip meters. */
    double partial_import_active_kwh;
    double partial_export_active_kwh;
    double partial_import_reactive_kvarh;
    double partial_export_reactive_kvarh;
    double partial_apparent_kvah;
    bool valid;
} em500_energy_t;

/*
 * Decodes a block read from EM500_ENERGY_START of EM500_ENERGY_REGISTERS words.
 * Same contract as em500_block_decode: false and a zeroed struct on bad input,
 * never a partial fill.
 */
bool em500_energy_decode(const uint16_t *words, uint16_t count, em500_energy_t *out);

#ifdef __cplusplus
}
#endif
