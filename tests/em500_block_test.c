/*
 * The EM-500 instantaneous measurement block.
 *
 * Everything the meter measures about the present moment arrives in ONE
 * transaction, so the whole risk sits in the decode: an offset that is one word
 * out, or a field read unsigned that the manual marks signed, produces a number
 * that looks entirely plausible and is wrong. Those are the cases below.
 *
 * The fixture is built from the manual's own units, so the test states what each
 * register MEANS rather than echoing what the decoder does with it.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "em500_block.h"

#define OFFSET(address) ((uint16_t)((address) - EM500_BLOCK_START))

static void put_u32(uint16_t *words, uint16_t address, uint32_t value)
{
    words[OFFSET(address)] = (uint16_t)(value >> 16);
    words[OFFSET(address) + 1u] = (uint16_t)(value & 0xFFFFu);
}

static void put_s32(uint16_t *words, uint16_t address, int32_t value)
{
    put_u32(words, address, (uint32_t)value);
}

static int close_to(float value, float expected)
{
    return fabsf(value - expected) < 0.001f;
}

/* A plausible 400 V site pulling roughly 244 kW, with phase 2 exporting. */
static void fixture(uint16_t *words)
{
    memset(words, 0, EM500_BLOCK_REGISTERS * sizeof(uint16_t));

    put_u32(words, 0x0002, 23012);        /* 230.12 V   */
    put_u32(words, 0x0004, 22987);        /* 229.87 V   */
    put_u32(words, 0x0006, 23105);        /* 231.05 V   */

    put_u32(words, 0x0008, 3567890);      /* 356.789 A  */
    put_u32(words, 0x000A, 1200000);      /* 120.0 A    */
    put_u32(words, 0x000C, 2000000);      /* 200.0 A    */

    put_u32(words, 0x000E, 39876);        /* 398.76 V   */
    put_u32(words, 0x0010, 39912);        /* 399.12 V   */
    put_u32(words, 0x0012, 40001);        /* 400.01 V   */

    put_s32(words, 0x0014, 12266000);     /* 122.66 kW  */
    put_s32(words, 0x0016, -2000000);     /* -20.00 kW, exporting */
    put_s32(words, 0x0018, 12388000);     /* 123.88 kW  */

    put_s32(words, 0x001A, 500000);       /* 5.0 kvar   */
    put_s32(words, 0x001C, -250000);      /* -2.5 kvar  */
    put_s32(words, 0x001E, 750000);       /* 7.5 kvar   */

    put_u32(words, 0x0020, 12300000);     /* 123.0 kVA  */
    put_u32(words, 0x0022, 2100000);      /* 21.0 kVA   */
    put_u32(words, 0x0024, 12400000);     /* 124.0 kVA  */

    put_s32(words, 0x0026, 9970);         /* 0.9970 */
    put_s32(words, 0x0028, -9520);        /* -0.9520, leading */
    put_s32(words, 0x002A, 9990);         /* 0.9990 */

    put_u32(words, 0x0032, 49985);        /* 49.985 Hz */
    put_u32(words, 0x0034, 23034);        /* 230.34 V  */
    put_u32(words, 0x0036, 39930);        /* 399.30 V  */
    put_u32(words, 0x0038, 2255963);      /* 225.5963 A */
    put_s32(words, 0x003A, 22654000);     /* 226.54 kW */
    put_s32(words, 0x003C, 1000000);      /* 10.0 kvar */
    put_u32(words, 0x003E, 26800000);     /* 268.0 kVA */
    put_s32(words, 0x0040, 9850);         /* 0.9850 */

    put_u32(words, 0x0042, 120);          /* 1.20 % */
    put_u32(words, 0x0044, 95);           /* 0.95 % */
    put_u32(words, 0x0046, 4310);         /* 43.10 % */
    put_u32(words, 0x0048, 812345);       /* 81.2345 A */
}

static void test_every_field_decodes_to_its_documented_unit(void)
{
    uint16_t words[EM500_BLOCK_REGISTERS];
    em500_measurements_t m;
    fixture(words);
    assert(em500_block_decode(words, EM500_BLOCK_REGISTERS, &m));
    assert(m.valid);

    assert(close_to(m.phase_voltage_v[0], 230.12f));
    assert(close_to(m.phase_voltage_v[2], 231.05f));
    assert(close_to(m.current_a[0], 356.789f));
    assert(close_to(m.line_voltage_v[2], 400.01f));
    assert(close_to(m.active_power_kw[0], 122.66f));
    assert(close_to(m.reactive_power_kvar[0], 5.0f));
    assert(close_to(m.apparent_power_kva[2], 124.0f));
    assert(close_to(m.power_factor[0], 0.9970f));
    assert(close_to(m.frequency_hz, 49.985f));
    assert(close_to(m.equivalent_current_a, 225.5963f));
    assert(close_to(m.total_active_power_kw, 226.54f));
    assert(close_to(m.total_apparent_power_kva, 268.0f));
    assert(close_to(m.total_power_factor, 0.9850f));
    assert(close_to(m.current_asymmetry_percent, 43.10f));
    assert(close_to(m.neutral_current_a, 81.2345f));
}

/*
 * THE ERROR THAT LOOKS PLAUSIBLE. The manual marks active and reactive power and
 * power factor "Signed long". Read one of them unsigned and a 20 kW export
 * becomes about 42.9 million kW of import -- a number no reader would trust, but
 * one that a MIN() across phases or a control decision would act on before any
 * human saw it.
 */
static void test_signed_fields_stay_signed(void)
{
    uint16_t words[EM500_BLOCK_REGISTERS];
    em500_measurements_t m;
    fixture(words);
    assert(em500_block_decode(words, EM500_BLOCK_REGISTERS, &m));

    assert(m.active_power_kw[1] < 0.0f);
    assert(close_to(m.active_power_kw[1], -20.0f));
    assert(m.reactive_power_kvar[1] < 0.0f);
    /* Power factor sign carries leading versus lagging. */
    assert(m.power_factor[1] < 0.0f);
    assert(close_to(m.power_factor[1], -0.9520f));

    /* And the export survives a whole-installation read too. */
    put_s32(words, 0x003A, -5000000);
    assert(em500_block_decode(words, EM500_BLOCK_REGISTERS, &m));
    assert(close_to(m.total_active_power_kw, -50.0f));
}

/* Unsigned fields must NOT be sign-extended: a large current is a large
 * current, not a negative one. */
static void test_unsigned_fields_stay_unsigned(void)
{
    uint16_t words[EM500_BLOCK_REGISTERS];
    em500_measurements_t m;
    fixture(words);
    /* Above 2^31, which is where a wrongly signed read flips negative. */
    put_u32(words, 0x0008, 3000000000u);
    assert(em500_block_decode(words, EM500_BLOCK_REGISTERS, &m));
    assert(m.current_a[0] > 0.0f);
    assert(close_to(m.current_a[0], 300000.0f));
}

/*
 * OFFSETS. A field read one word out still produces a number, and on this meter
 * it produces a number of roughly the right magnitude, because neighbouring
 * registers hold related quantities. So each field is checked against a
 * DISTINCT value rather than a shared one -- if every register held 1000 this
 * whole suite would pass with the map shifted.
 */
static void test_a_shifted_map_would_be_caught(void)
{
    uint16_t words[EM500_BLOCK_REGISTERS];
    em500_measurements_t m;
    fixture(words);
    assert(em500_block_decode(words, EM500_BLOCK_REGISTERS, &m));

    /* Neighbours differ, so reading the wrong one changes the answer. */
    assert(!close_to(m.phase_voltage_v[0], m.phase_voltage_v[1]));
    assert(!close_to(m.active_power_kw[0], m.active_power_kw[2]));
    assert(!close_to(m.current_a[0], m.current_a[1]));
    assert(!close_to(m.line_voltage_v[0], m.line_voltage_v[1]));
}

/* A short or absent block yields nothing rather than a partial fill: a struct
 * half-populated with real numbers and half with zeros is indistinguishable
 * from a plant that is half idle. */
static void test_short_or_missing_input_yields_nothing(void)
{
    uint16_t words[EM500_BLOCK_REGISTERS];
    em500_measurements_t m;
    fixture(words);

    assert(!em500_block_decode(words, EM500_BLOCK_REGISTERS - 1u, &m));
    assert(!m.valid);
    assert(m.total_active_power_kw == 0.0f);
    assert(m.phase_voltage_v[0] == 0.0f);

    assert(!em500_block_decode(NULL, EM500_BLOCK_REGISTERS, &m));
    assert(!m.valid);
    assert(!em500_block_decode(words, EM500_BLOCK_REGISTERS, NULL));
}

/* The block must stay inside one Modbus request. Beyond 125 registers the read
 * silently becomes two transactions or an exception, and the whole reason this
 * exists is that it is one. */
static void test_block_fits_a_single_request(void)
{
    assert(EM500_BLOCK_REGISTERS <= 125u);
    /* And it must still reach the last documented field, neutral current at
     * 0x0048, which occupies two words. */
    assert(EM500_BLOCK_START + EM500_BLOCK_REGISTERS >= 0x0048u + 2u);
}

/* ------------------------------------------------------------------- energy */

static void put_u64(uint16_t *words, uint16_t address, uint64_t value)
{
    const uint16_t o = (uint16_t)(address - EM500_ENERGY_START);
    words[o] = (uint16_t)(value >> 48);
    words[o + 1u] = (uint16_t)((value >> 32) & 0xFFFFu);
    words[o + 2u] = (uint16_t)((value >> 16) & 0xFFFFu);
    words[o + 3u] = (uint16_t)(value & 0xFFFFu);
}

static void energy_fixture(uint16_t *words)
{
    memset(words, 0, EM500_ENERGY_REGISTERS * sizeof(uint16_t));
    put_u64(words, 0x1B20, 123456789ULL);   /* 1,234,567.89 kWh   */
    put_u64(words, 0x1B24, 4567890ULL);     /*    45,678.90 kWh   */
    put_u64(words, 0x1B28, 98765ULL);       /*       987.65 kvarh */
    put_u64(words, 0x1B2C, 4321ULL);        /*        43.21 kvarh */
    put_u64(words, 0x1B30, 222222ULL);      /*     2,222.22 kVAh  */
    put_u64(words, 0x1B34, 5000ULL);        /*        50.00 kWh   */
    put_u64(words, 0x1B38, 2500ULL);        /*        25.00 kWh   */
    put_u64(words, 0x1B3C, 1000ULL);        /*        10.00 kvarh */
    put_u64(words, 0x1B40, 300ULL);         /*         3.00 kvarh */
    put_u64(words, 0x1B44, 7777ULL);        /*        77.77 kVAh  */
}

static int close_to_d(double value, double expected)
{
    return fabs(value - expected) < 0.001;
}

static void test_energy_counters_decode_in_kwh(void)
{
    uint16_t words[EM500_ENERGY_REGISTERS];
    em500_energy_t e;
    energy_fixture(words);
    assert(em500_energy_decode(words, EM500_ENERGY_REGISTERS, &e));
    assert(e.valid);

    assert(close_to_d(e.total_import_active_kwh, 1234567.89));
    assert(close_to_d(e.total_export_active_kwh, 45678.90));
    assert(close_to_d(e.total_import_reactive_kvarh, 987.65));
    assert(close_to_d(e.total_export_reactive_kvarh, 43.21));
    assert(close_to_d(e.total_apparent_kvah, 2222.22));
    assert(close_to_d(e.partial_import_active_kwh, 50.00));
    assert(close_to_d(e.partial_export_active_kwh, 25.00));
    assert(close_to_d(e.partial_import_reactive_kvarh, 10.00));
    assert(close_to_d(e.partial_export_reactive_kvarh, 3.00));
    assert(close_to_d(e.partial_apparent_kvah, 77.77));
}

/*
 * THE 64-BIT TRAP. Table 3 says four words. Decode a counter as two and the read
 * lands on the HIGH half, which on any meter that has not yet passed 42.9 GWh is
 * zero -- so the counter reads 0.00 kWh forever and looks like a meter that is
 * not counting rather than a decoder that is wrong. This is the case that would
 * survive a bench test on a new meter and fail on a customer's old one.
 */
static void test_a_32_bit_decode_would_read_zero(void)
{
    uint16_t words[EM500_ENERGY_REGISTERS];
    em500_energy_t e;
    energy_fixture(words);
    assert(em500_energy_decode(words, EM500_ENERGY_REGISTERS, &e));

    /* The value's significant bits live in the LOW pair, which a 32-bit read
     * would skip entirely. */
    const uint16_t o = 0x1B20u - EM500_ENERGY_START;
    assert(words[o] == 0 && words[o + 1u] == 0);
    assert(words[o + 2u] != 0 || words[o + 3u] != 0);
    assert(e.total_import_active_kwh > 0.0);
}

/* And a counter genuinely above 32 bits must survive, which is the whole reason
 * the meter uses four words: 42.9 GWh is a year for a mid-sized factory, not a
 * theoretical limit. */
static void test_counters_beyond_32_bits_survive(void)
{
    uint16_t words[EM500_ENERGY_REGISTERS];
    em500_energy_t e;
    energy_fixture(words);
    put_u64(words, 0x1B20, 500000000123ULL);   /* 5,000,000,001.23 kWh */
    assert(em500_energy_decode(words, EM500_ENERGY_REGISTERS, &e));
    /* Held to the hundredth of a kWh at five billion. A float cannot do this at
     * all -- its spacing up there is over 500 kWh -- so this assertion is what
     * makes the field a double rather than a preference. */
    assert(close_to_d(e.total_import_active_kwh, 5000000001.23));
}

static void test_energy_rejects_short_input(void)
{
    uint16_t words[EM500_ENERGY_REGISTERS];
    em500_energy_t e;
    energy_fixture(words);
    assert(!em500_energy_decode(words, EM500_ENERGY_REGISTERS - 1u, &e));
    assert(!e.valid);
    assert(e.total_import_active_kwh == 0.0);
    assert(!em500_energy_decode(NULL, EM500_ENERGY_REGISTERS, &e));
    assert(!em500_energy_decode(words, EM500_ENERGY_REGISTERS, NULL));
}

static void test_energy_block_fits_a_single_request(void)
{
    assert(EM500_ENERGY_REGISTERS <= 125u);
    /* Reaches the last unambiguous counter, partial apparent at 0x1B44, which is
     * four words. Beyond it the manual contradicts itself and nothing is read. */
    assert(EM500_ENERGY_START + EM500_ENERGY_REGISTERS >= 0x1B44u + 4u);
    assert(EM500_ENERGY_START + EM500_ENERGY_REGISTERS <= 0x1B48u);
}

int main(void)
{
    test_every_field_decodes_to_its_documented_unit();
    test_signed_fields_stay_signed();
    test_unsigned_fields_stay_unsigned();
    test_a_shifted_map_would_be_caught();
    test_short_or_missing_input_yields_nothing();
    test_block_fits_a_single_request();
    test_energy_counters_decode_in_kwh();
    test_a_32_bit_decode_would_read_zero();
    test_counters_beyond_32_bits_survive();
    test_energy_rejects_short_input();
    test_energy_block_fits_a_single_request();
    printf("EM500 measurement block tests passed\n");
    return 0;
}
