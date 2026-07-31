#include "em500_block.h"

#include <string.h>

/* Word offsets from EM500_BLOCK_START, derived once from the manual's addresses
 * so no arithmetic is repeated at each use site.
 *
 *   offset = (address - 0x0002)
 */
#define OFF(address) ((uint16_t)((address) - EM500_BLOCK_START))

/* Two 16-bit registers, high word first, as unsigned. */
static uint32_t u32_at(const uint16_t *words, uint16_t offset)
{
    return ((uint32_t)words[offset] << 16) | words[offset + 1u];
}

/* The same pair read as a two's-complement signed value.
 *
 * Written as an explicit reinterpretation rather than a cast chain because the
 * distinction is the whole point: the manual marks active power, reactive power
 * and power factor "Signed long", and reading one of them unsigned turns a small
 * export into roughly 42.9 million kW of import. */
static int32_t s32_at(const uint16_t *words, uint16_t offset)
{
    const uint32_t raw = u32_at(words, offset);
    return (int32_t)raw;
}

bool em500_block_decode(const uint16_t *words, uint16_t count, em500_measurements_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!words || count < EM500_BLOCK_REGISTERS) return false;

    /* Units, straight from Table 1:
     *   V/100      volts       raw / 100
     *   A/10000    amps        raw / 10000
     *   W/100      watts       raw / 100      -> kW is raw / 100000
     *   var/100, VA/100        same shape
     *   /10000     power factor
     *   Hz/1000    hertz
     *   %/100      percent
     *
     * kW rather than W throughout, because every other power figure this
     * firmware carries is kW and a single unit boundary is easier to keep
     * correct than a conversion at each reader. */
    static const float V = 1.0f / 100.0f;
    static const float A = 1.0f / 10000.0f;
    static const float KW = 1.0f / 100000.0f;
    static const float PF = 1.0f / 10000.0f;
    static const float HZ = 1.0f / 1000.0f;
    static const float PCT = 1.0f / 100.0f;

    out->phase_voltage_v[0] = (float)u32_at(words, OFF(0x0002)) * V;
    out->phase_voltage_v[1] = (float)u32_at(words, OFF(0x0004)) * V;
    out->phase_voltage_v[2] = (float)u32_at(words, OFF(0x0006)) * V;

    out->current_a[0] = (float)u32_at(words, OFF(0x0008)) * A;
    out->current_a[1] = (float)u32_at(words, OFF(0x000A)) * A;
    out->current_a[2] = (float)u32_at(words, OFF(0x000C)) * A;

    out->line_voltage_v[0] = (float)u32_at(words, OFF(0x000E)) * V;
    out->line_voltage_v[1] = (float)u32_at(words, OFF(0x0010)) * V;
    out->line_voltage_v[2] = (float)u32_at(words, OFF(0x0012)) * V;

    /* Signed: negative means this phase is exporting. */
    out->active_power_kw[0] = (float)s32_at(words, OFF(0x0014)) * KW;
    out->active_power_kw[1] = (float)s32_at(words, OFF(0x0016)) * KW;
    out->active_power_kw[2] = (float)s32_at(words, OFF(0x0018)) * KW;

    out->reactive_power_kvar[0] = (float)s32_at(words, OFF(0x001A)) * KW;
    out->reactive_power_kvar[1] = (float)s32_at(words, OFF(0x001C)) * KW;
    out->reactive_power_kvar[2] = (float)s32_at(words, OFF(0x001E)) * KW;

    /* Unsigned: apparent power has no direction. */
    out->apparent_power_kva[0] = (float)u32_at(words, OFF(0x0020)) * KW;
    out->apparent_power_kva[1] = (float)u32_at(words, OFF(0x0022)) * KW;
    out->apparent_power_kva[2] = (float)u32_at(words, OFF(0x0024)) * KW;

    /* Signed: the sign carries leading versus lagging. */
    out->power_factor[0] = (float)s32_at(words, OFF(0x0026)) * PF;
    out->power_factor[1] = (float)s32_at(words, OFF(0x0028)) * PF;
    out->power_factor[2] = (float)s32_at(words, OFF(0x002A)) * PF;

    /* 0x002C..0x0031 is not described in Table 1. It is inside the block and is
     * deliberately not interpreted: reading a register the manual does not name
     * and giving it a meaning is the exact failure this codebase refuses. */

    out->frequency_hz = (float)u32_at(words, OFF(0x0032)) * HZ;
    out->equivalent_phase_voltage_v = (float)u32_at(words, OFF(0x0034)) * V;
    out->equivalent_line_voltage_v = (float)u32_at(words, OFF(0x0036)) * V;
    out->equivalent_current_a = (float)u32_at(words, OFF(0x0038)) * A;
    out->total_active_power_kw = (float)s32_at(words, OFF(0x003A)) * KW;
    out->total_reactive_power_kvar = (float)s32_at(words, OFF(0x003C)) * KW;
    out->total_apparent_power_kva = (float)u32_at(words, OFF(0x003E)) * KW;
    out->total_power_factor = (float)s32_at(words, OFF(0x0040)) * PF;

    out->voltage_asymmetry_line_percent = (float)u32_at(words, OFF(0x0042)) * PCT;
    out->voltage_asymmetry_phase_percent = (float)u32_at(words, OFF(0x0044)) * PCT;
    out->current_asymmetry_percent = (float)u32_at(words, OFF(0x0046)) * PCT;
    out->neutral_current_a = (float)u32_at(words, OFF(0x0048)) * A;

    out->valid = true;
    return true;
}

/* ------------------------------------------------------------------- energy */

#define ENERGY_OFF(address) ((uint16_t)((address) - EM500_ENERGY_START))

/* Four registers, most significant word first -- the same byte order the rest of
 * this meter uses, extended to 64 bits because Table 3 says these counters are
 * four words wide. */
static double energy_at(const uint16_t *words, uint16_t offset)
{
    const uint64_t raw = ((uint64_t)words[offset] << 48) |
                         ((uint64_t)words[offset + 1u] << 32) |
                         ((uint64_t)words[offset + 2u] << 16) |
                         (uint64_t)words[offset + 3u];
    /* Every Table 3 counter is "/ 100" and "Unsigned long". None of them can be
     * negative: an energy counter that ran backwards would be a meter fault, not
     * a direction, because import and export have their own separate registers. */
    return (double)raw / 100.0;
}

bool em500_energy_decode(const uint16_t *words, uint16_t count, em500_energy_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!words || count < EM500_ENERGY_REGISTERS) return false;

    out->total_import_active_kwh = energy_at(words, ENERGY_OFF(0x1B20));
    out->total_export_active_kwh = energy_at(words, ENERGY_OFF(0x1B24));
    out->total_import_reactive_kvarh = energy_at(words, ENERGY_OFF(0x1B28));
    out->total_export_reactive_kvarh = energy_at(words, ENERGY_OFF(0x1B2C));
    out->total_apparent_kvah = energy_at(words, ENERGY_OFF(0x1B30));

    out->partial_import_active_kwh = energy_at(words, ENERGY_OFF(0x1B34));
    out->partial_export_active_kwh = energy_at(words, ENERGY_OFF(0x1B38));
    out->partial_import_reactive_kvarh = energy_at(words, ENERGY_OFF(0x1B3C));
    out->partial_export_reactive_kvarh = energy_at(words, ENERGY_OFF(0x1B40));
    out->partial_apparent_kvah = energy_at(words, ENERGY_OFF(0x1B44));

    out->valid = true;
    return true;
}
