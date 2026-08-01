#include "inverter_telemetry_block.h"

#include <string.h>

/* Word offset from the block start, derived once from the manual's decimal
 * addresses so no arithmetic is repeated at each use site. */
#define OFF(address) ((uint16_t)((address) - INVERTER_HUAWEI_BLOCK_START))

static uint16_t u16_at(const uint16_t *words, uint16_t offset)
{
    return words[offset];
}

/* The manual's "I16". Written as an explicit reinterpretation because the
 * distinction is load-bearing: PV voltage, power factor and internal temperature
 * are all genuinely negative on real plant, and reading one of them unsigned
 * turns a small negative into roughly 65 thousand of its unit. */
static int16_t s16_at(const uint16_t *words, uint16_t offset)
{
    return (int16_t)words[offset];
}

static uint32_t u32_at(const uint16_t *words, uint16_t offset)
{
    return ((uint32_t)words[offset] << 16) | words[offset + 1u];
}

/* The manual's "I32". An inverter at night consumes, so active power is
 * genuinely negative; read unsigned it reports about 4.29 million kW of
 * production from a plant in the dark. */
static int32_t s32_at(const uint16_t *words, uint16_t offset)
{
    return (int32_t)u32_at(words, offset);
}

bool inverter_huawei_block_decode(const uint16_t *words, uint16_t count,
                                  inverter_measurements_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!words || count < INVERTER_HUAWEI_BLOCK_REGISTERS) return false;

    /*
     * Gains, from the manual's own "gain" column. THEY ARE DIVISORS: the manual
     * states value = raw / gain. Applied as a multiplier a 230 V string reads
     * 2300 V -- a number that gets believed because it is on a screen.
     */
    static const float G10 = 1.0f / 10.0f;
    static const float G100 = 1.0f / 100.0f;
    static const float G1000 = 1.0f / 1000.0f;

    /* --- DC. PV1 V/I at 32016/32017 and PV20 V/I at 32054/32055 are both
     * documented, which pins the two-register stride at both ends of the run.
     * Strings beyond INVERTER_PV_STRINGS are read as part of the block and
     * deliberately not interpreted. */
    for (int string = 0; string < INVERTER_PV_STRINGS; ++string) {
        const uint16_t voltage_address = (uint16_t)(32016u + 2u * (unsigned)string);
        out->pv_voltage_v[string] = (float)s16_at(words, OFF(voltage_address)) * G10;
        out->pv_current_a[string] = (float)s16_at(words, OFF(voltage_address + 1u)) * G100;
    }
    out->dc_power_kw = (float)s32_at(words, OFF(32064)) * G1000;   /* I32, kW, 1000 */

    /* --- AC. Line to line, then phase to neutral. U16, V, gain 10. */
    out->line_voltage_v[0] = (float)u16_at(words, OFF(32066)) * G10;
    out->line_voltage_v[1] = (float)u16_at(words, OFF(32067)) * G10;
    out->line_voltage_v[2] = (float)u16_at(words, OFF(32068)) * G10;
    out->phase_voltage_v[0] = (float)u16_at(words, OFF(32069)) * G10;
    out->phase_voltage_v[1] = (float)u16_at(words, OFF(32070)) * G10;
    out->phase_voltage_v[2] = (float)u16_at(words, OFF(32071)) * G10;

    /* Phase currents are I32 and TWO registers apart each, not one: 32072,
     * 32074, 32076. A one-register stride here would read the low half of one
     * current together with the high half of the next and produce a number of
     * entirely plausible magnitude. */
    out->phase_current_a[0] = (float)s32_at(words, OFF(32072)) * G1000;
    out->phase_current_a[1] = (float)s32_at(words, OFF(32074)) * G1000;
    out->phase_current_a[2] = (float)s32_at(words, OFF(32076)) * G1000;

    out->peak_active_power_today_kw = (float)s32_at(words, OFF(32078)) * G1000;
    out->active_power_kw = (float)s32_at(words, OFF(32080)) * G1000;
    out->reactive_power_kvar = (float)s32_at(words, OFF(32082)) * G1000;
    out->power_factor = (float)s16_at(words, OFF(32084)) * G1000;
    out->frequency_hz = (float)u16_at(words, OFF(32085)) * G100;
    out->efficiency_percent = (float)u16_at(words, OFF(32086)) * G100;
    out->internal_temperature_c = (float)s16_at(words, OFF(32087)) * G10;
    out->insulation_resistance_mohm = (float)u16_at(words, OFF(32088)) * G1000;

    /* Raw. Device Status is an enumeration and Fault Code indexes a table this
     * firmware has not transcribed; a label invented for either would be a guess
     * wearing the clothes of a diagnosis. */
    out->device_status_raw = u16_at(words, OFF(32089));
    out->fault_code_raw = u16_at(words, OFF(32090));

    /* --- Energy. U32, kWh, gain 100. Unsigned in the manual, and rightly:
     * these are counters, and one that ran backwards would be a device fault
     * rather than a direction. */
    out->total_yield_kwh = (float)u32_at(words, OFF(32106)) * G100;
    out->total_dc_input_kwh = (float)u32_at(words, OFF(32108)) * G100;
    out->daily_yield_kwh = (float)u32_at(words, OFF(32114)) * G100;
    out->month_yield_kwh = (float)u32_at(words, OFF(32116)) * G100;

    out->valid = true;
    return true;
}
