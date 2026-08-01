/*
 * The Huawei telemetry block.
 *
 * Source: "Solar Inverter Modbus Interface Definitions (V3.0)", Huawei, Issue 01
 * (2023-01-17), section 3. The fixture below is written in the manual's own raw
 * units, so each case states what the register MEANS rather than echoing what
 * the decoder does with it.
 *
 * Four ways this decode goes wrong, each with a case, each of which produces a
 * plausible number rather than an obvious one:
 *   - gain applied as a multiplier instead of a divisor
 *   - a signed field read unsigned
 *   - the phase-current stride taken as one register instead of two
 *   - a short read filled in partially instead of refused
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "inverter_telemetry_block.h"

#define OFFSET(address) ((uint16_t)((address) - INVERTER_HUAWEI_BLOCK_START))

static void put_u16(uint16_t *words, uint16_t address, uint16_t value)
{
    words[OFFSET(address)] = value;
}

static void put_s16(uint16_t *words, uint16_t address, int16_t value)
{
    words[OFFSET(address)] = (uint16_t)value;
}

static void put_u32(uint16_t *words, uint16_t address, uint32_t value)
{
    words[OFFSET(address)] = (uint16_t)(value >> 16);
    words[OFFSET(address) + 1u] = (uint16_t)(value & 0xFFFFu);
}

static void put_s32(uint16_t *words, uint16_t address, int32_t value)
{
    put_u32(words, address, (uint32_t)value);
}

/* Absolute for small quantities, relative for large ones.
 *
 * A float carries about seven significant digits, so at a six-figure energy
 * counter its own spacing is already about 0.016 kWh -- a fixed 0.005 band there
 * is tighter than the type can represent and would fail on a correct decode.
 * The relative term keeps the test about the DECODE rather than about float. */
static int close_to(float value, float expected)
{
    const float magnitude = fabsf(expected);
    const float tolerance = magnitude > 1000.0f ? magnitude * 1e-6f : 0.005f;
    return fabsf(value - expected) <= tolerance;
}

/* A 60 kW machine on a 400 V three-phase grid, producing on a clear afternoon.
 * Every field is a DISTINCT value: if the fixture reused numbers, a map shifted
 * by one register would satisfy the whole suite. */
static void fixture(uint16_t *words)
{
    memset(words, 0, INVERTER_HUAWEI_BLOCK_REGISTERS * sizeof(uint16_t));

    /* PV strings, V gain 10 and A gain 100. */
    put_s16(words, 32016, 6012);   /* 601.2 V */
    put_s16(words, 32017, 1234);   /*  12.34 A */
    put_s16(words, 32018, 5987);   /* 598.7 V */
    put_s16(words, 32019, 1198);   /*  11.98 A */
    put_s16(words, 32020, 6105);   /* 610.5 V */
    put_s16(words, 32021, 1301);   /*  13.01 A */
    put_s16(words, 32022, 5876);   /* 587.6 V */
    put_s16(words, 32023, 1150);   /*  11.50 A */

    put_s32(words, 32064, 58750);  /* 58.750 kW DC, gain 1000 */

    /* Line to line and phase to neutral, U16, gain 10. */
    put_u16(words, 32066, 3987);   /* 398.7 V A-B */
    put_u16(words, 32067, 3991);   /* 399.1 V B-C */
    put_u16(words, 32068, 4001);   /* 400.1 V C-A */
    put_u16(words, 32069, 2301);   /* 230.1 V A */
    put_u16(words, 32070, 2298);   /* 229.8 V B */
    put_u16(words, 32071, 2311);   /* 231.1 V C */

    /* Phase currents, I32, gain 1000, TWO registers apart. */
    put_s32(words, 32072, 84120);  /* 84.120 A */
    put_s32(words, 32074, 83550);  /* 83.550 A */
    put_s32(words, 32076, 85010);  /* 85.010 A */

    put_s32(words, 32078, 59900);  /* 59.900 kW peak today */
    put_s32(words, 32080, 57340);  /* 57.340 kW active */
    put_s32(words, 32082, -1250);  /* -1.250 kvar */
    put_s16(words, 32084, 998);    /* 0.998 power factor */
    put_u16(words, 32085, 4998);   /* 49.98 Hz, gain 100 */
    put_u16(words, 32086, 9862);   /* 98.62 %, gain 100 */
    put_s16(words, 32087, 412);    /* 41.2 degC, gain 10 */
    put_u16(words, 32088, 30000);  /* 30.000 Mohm, gain 1000 */
    put_u16(words, 32089, 512);    /* device status, raw */
    put_u16(words, 32090, 0);      /* fault code, raw */

    /* Energy, U32, gain 100. */
    put_u32(words, 32106, 12345678u);  /* 123456.78 kWh total */
    put_u32(words, 32108, 12999999u);  /* 129999.99 kWh DC input */
    put_u32(words, 32114, 24567u);     /*    245.67 kWh today */
    put_u32(words, 32116, 567890u);    /*   5678.90 kWh this month */
}

static void test_every_field_decodes_to_its_documented_unit(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));
    assert(m.valid);

    assert(close_to(m.pv_voltage_v[0], 601.2f));
    assert(close_to(m.pv_current_a[0], 12.34f));
    assert(close_to(m.pv_voltage_v[3], 587.6f));
    assert(close_to(m.pv_current_a[3], 11.50f));
    assert(close_to(m.dc_power_kw, 58.750f));

    assert(close_to(m.line_voltage_v[0], 398.7f));
    assert(close_to(m.line_voltage_v[2], 400.1f));
    assert(close_to(m.phase_voltage_v[0], 230.1f));
    assert(close_to(m.phase_voltage_v[2], 231.1f));

    assert(close_to(m.phase_current_a[0], 84.120f));
    assert(close_to(m.phase_current_a[1], 83.550f));
    assert(close_to(m.phase_current_a[2], 85.010f));

    assert(close_to(m.peak_active_power_today_kw, 59.900f));
    assert(close_to(m.active_power_kw, 57.340f));
    assert(close_to(m.reactive_power_kvar, -1.250f));
    assert(close_to(m.power_factor, 0.998f));
    assert(close_to(m.frequency_hz, 49.98f));
    assert(close_to(m.efficiency_percent, 98.62f));
    assert(close_to(m.internal_temperature_c, 41.2f));
    assert(close_to(m.insulation_resistance_mohm, 30.0f));

    assert(m.device_status_raw == 512);
    assert(m.fault_code_raw == 0);

    assert(close_to(m.total_yield_kwh, 123456.78f));
    assert(close_to(m.daily_yield_kwh, 245.67f));
    assert(close_to(m.month_yield_kwh, 5678.90f));
}

/*
 * GAIN IS A DIVISOR. The manual's gain column means value = raw / gain. Applied
 * as a multiplier a 601 V string reads 6012 V and a 50 Hz grid reads 499,800 Hz
 * -- and the voltage in particular is a number an engineer might accept from a
 * high-voltage string without blinking.
 */
static void test_gain_divides_and_does_not_multiply(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));

    /* Each is strictly smaller than its raw word, which a multiplier cannot
     * satisfy for any of them. */
    assert(m.pv_voltage_v[0] < 6012.0f && m.pv_voltage_v[0] > 600.0f);
    assert(m.frequency_hz < 100.0f);
    assert(m.active_power_kw < 1000.0f);
    assert(m.total_yield_kwh < 12345678.0f);
}

/*
 * SIGNEDNESS. An inverter at night consumes: active power goes negative, and the
 * decoder reading it unsigned reports about 4.29 million kW of production from a
 * plant in the dark. Power factor and internal temperature are signed too -- a
 * machine below freezing is ordinary in a Pakistani winter morning.
 */
static void test_signed_fields_stay_signed(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);

    put_s32(words, 32080, -450);    /* -0.450 kW: consuming overnight */
    put_s16(words, 32084, -950);    /* -0.950: leading */
    put_s16(words, 32087, -35);     /* -3.5 degC */
    put_s32(words, 32064, -120);    /* -0.120 kW DC */
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));

    assert(m.active_power_kw < 0.0f && close_to(m.active_power_kw, -0.450f));
    assert(m.power_factor < 0.0f && close_to(m.power_factor, -0.950f));
    assert(m.internal_temperature_c < 0.0f && close_to(m.internal_temperature_c, -3.5f));
    assert(m.dc_power_kw < 0.0f);

    /* The already-negative reactive power from the fixture survives too. */
    assert(m.reactive_power_kvar < 0.0f);
}

/* And the unsigned fields must NOT be sign-extended: a large energy counter is
 * a large counter, not a negative one. */
static void test_unsigned_fields_stay_unsigned(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);
    put_u32(words, 32106, 3000000000u);  /* above 2^31 */
    put_u16(words, 32069, 60000);        /* a U16 above 2^15 */
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));
    assert(m.total_yield_kwh > 0.0f);
    assert(m.phase_voltage_v[0] > 0.0f);
    assert(close_to(m.phase_voltage_v[0], 6000.0f));
}

/*
 * THE STRIDE. Phase currents are I32 at 32072, 32074, 32076 -- two registers
 * apart, not one. A one-register stride reads the low half of one current joined
 * to the high half of the next, and because neighbouring phases carry similar
 * magnitudes the result looks like a current rather than like garbage.
 *
 * The three fixture currents differ, so a decoder that mis-strides cannot land
 * on all three correct values by coincidence.
 */
static void test_phase_current_stride_is_two_registers(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));

    assert(!close_to(m.phase_current_a[0], m.phase_current_a[1]));
    assert(!close_to(m.phase_current_a[1], m.phase_current_a[2]));
    /* All three are plant-plausible, which a mis-stride would not be. */
    for (int phase = 0; phase < 3; ++phase) {
        assert(m.phase_current_a[phase] > 50.0f && m.phase_current_a[phase] < 100.0f);
    }
}

/* The PV strings interleave voltage, current, voltage, current. A decoder that
 * took them as four voltages then four currents would still produce numbers. */
static void test_pv_strings_interleave_voltage_and_current(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));

    for (int string = 0; string < INVERTER_PV_STRINGS; ++string) {
        /* Voltages are hundreds of volts, currents are tens of amps: no
         * interleaving mistake can satisfy both bands at once. */
        assert(m.pv_voltage_v[string] > 500.0f && m.pv_voltage_v[string] < 700.0f);
        assert(m.pv_current_a[string] > 5.0f && m.pv_current_a[string] < 20.0f);
    }
    assert(!close_to(m.pv_voltage_v[0], m.pv_voltage_v[1]));
}

/* Status and fault stay raw. Device Status is an enumeration and Fault Code
 * indexes a table this firmware has not transcribed; a label invented for
 * either would be a guess wearing the clothes of a diagnosis. */
static void test_status_and_fault_are_carried_raw(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);
    put_u16(words, 32089, 0xABCD);
    put_u16(words, 32090, 0x1234);
    assert(inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));
    assert(m.device_status_raw == 0xABCD);
    assert(m.fault_code_raw == 0x1234);
}

static void test_short_or_missing_input_yields_nothing(void)
{
    uint16_t words[INVERTER_HUAWEI_BLOCK_REGISTERS];
    inverter_measurements_t m;
    fixture(words);

    assert(!inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS - 1u, &m));
    assert(!m.valid);
    assert(m.active_power_kw == 0.0f);
    assert(m.total_yield_kwh == 0.0f);
    assert(m.device_status_raw == 0);

    assert(!inverter_huawei_block_decode(NULL, INVERTER_HUAWEI_BLOCK_REGISTERS, &m));
    assert(!m.valid);
    assert(!inverter_huawei_block_decode(words, INVERTER_HUAWEI_BLOCK_REGISTERS, NULL));
}

/* One transaction, and it must reach the last field it claims to decode. */
static void test_block_fits_a_single_request(void)
{
    assert(INVERTER_HUAWEI_BLOCK_REGISTERS <= 125u);
    /* 32116 "electricity generated in current month" is two registers. */
    assert(INVERTER_HUAWEI_BLOCK_START + INVERTER_HUAWEI_BLOCK_REGISTERS >= 32116u + 2u);
    /* And the last PV string decoded is inside it. */
    assert(INVERTER_HUAWEI_BLOCK_START <= 32016u);
}

int main(void)
{
    test_every_field_decodes_to_its_documented_unit();
    test_gain_divides_and_does_not_multiply();
    test_signed_fields_stay_signed();
    test_unsigned_fields_stay_unsigned();
    test_phase_current_stride_is_two_registers();
    test_pv_strings_interleave_voltage_and_current();
    test_status_and_fault_are_carried_raw();
    test_short_or_missing_input_yields_nothing();
    test_block_fits_a_single_request();
    printf("Huawei inverter telemetry block tests passed\n");
    return 0;
}
