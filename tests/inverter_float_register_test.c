/* Host-compiled unit test for IEEE-754 Float32 register support.
 *
 * This EXECUTES the real encoder and the real decoder. It does not grep source,
 * because the property at stake is arithmetic and byte layout, which source text
 * cannot demonstrate.
 *
 * The failure being prevented is specific and is the worst outcome this module
 * can produce. Before a float type existed, a percentage destined for a Float32
 * dispatch register was encoded as a plain integer: 50 % became 0x00000032, which
 * as a float is 7e-44 -- effectively an order to stop generating. The readback
 * path would then decode those same two registers the same wrong way, get 50
 * back, agree with the request and report the command CONFIRMED. Every layer
 * above, including the operator, would be told a 100 kW plant was limited to 50 %
 * while it had in fact been commanded to zero. Nothing downstream could notice.
 *
 * Two properties therefore have to hold together, and both are executed here:
 *   1. A percentage encoded as Float32 must produce the IEEE-754 bit pattern of
 *      that percentage, not its integer value.
 *   2. Encode and decode must be exact inverses IN BOTH WORD ORDERS. If they were
 *      not, a wrong value on the wire would read back as the value that was asked
 *      for -- a self-confirming error, which is the same trap by another door.
 *
 * And one refusal:
 *   3. A non-finite value is never a sample and never a command. Decoding NaN or
 *      an infinity out of a register must fail rather than return, so the caller
 *      treats it as "no reading yet" -- the project's existing convention, shared
 *      with modbus_decode_scaled() and with the usable-sample rule in
 *      inverter_write_confirmation_evaluate(). A NaN readback must not be able to
 *      declare a MISMATCH on a healthy machine, and must never reach the control
 *      loop as a number.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inverter_profile_decode.h"

/* Representative percentages: the endpoints, the SolarEdge worked-example value,
 * a value with no exact binary representation, and the small values a generator
 * support scheme actually spends its time at. */
static const float PERCENTS[] = {
    0.0f, 0.1f, 1.0f, 7.3f, 12.5f, 33.333333f, 47.3f, 50.0f, 66.7f, 99.9f, 100.0f
};
static const size_t PERCENT_COUNT = sizeof(PERCENTS) / sizeof(PERCENTS[0]);

static const inverter_word_order_t ORDERS[] = {
    INVERTER_WORD_ORDER_AB, INVERTER_WORD_ORDER_BA
};

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* THE headline property: a float register receives the float, and specifically not
 * the integer that a scale-only encoder would have produced. 50.0f is
 * 0x42480000; the old integer path produced 0x00000032. */
static void test_float_encoding_is_ieee754_not_an_integer(void)
{
    uint16_t words[2] = {0, 0};
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, words) == ESP_OK);

    /* Little-endian word order: least significant word at the lower address. */
    const uint32_t assembled = ((uint32_t)words[1] << 16) | words[0];
    assert(assembled == 0x42480000u);
    assert(assembled == float_bits(50.0f));

    /* 100 % must be the documented full-scale float, not the integer 100. */
    assert(inverter_profile_encode_value(100.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, words) == ESP_OK);
    assert((((uint32_t)words[1] << 16) | words[0]) == 0x42C80000u);
    assert((((uint32_t)words[1] << 16) | words[0]) != 0x00000064u);

    /* Zero percent is the one value where the integer path happened to agree, and
     * it must still be exactly +0.0f rather than a denormal. */
    assert(inverter_profile_encode_value(0.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, words) == ESP_OK);
    assert(words[0] == 0u && words[1] == 0u);

    /* The value that would have gone on the wire without a float type, spelled out
     * so the regression is unmistakable. */
    assert(assembled != 0x00000032u);

    /* And what that integer would have MEANT in a float register: not "50", but a
     * denormal indistinguishable from zero. This is the number the plant would
     * have been commanded to. */
    float misread;
    uint32_t integer_fifty = 0x00000032u;
    memcpy(&misread, &integer_fifty, sizeof(misread));
    assert(misread > 0.0f);
    assert(misread < 1e-40f);
}

/* Big-endian word order must put the same bits the other way round, and the two
 * orders must genuinely differ -- otherwise the word-order argument is being
 * ignored and the profile field would be decorative. */
static void test_both_word_orders_place_the_same_bits(void)
{
    uint16_t big[2] = {0, 0};
    uint16_t little[2] = {0, 0};
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_AB, 2, big) == ESP_OK);
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, little) == ESP_OK);
    assert(big[0] == little[1]);
    assert(big[1] == little[0]);
    assert(big[0] != big[1]); /* 0x4248 vs 0x0000: the orders are distinguishable */
}

/* Round-trip in both orders, at unit scale (which is what SolarEdge's Float32
 * percentage register uses: the float carries the percentage itself). */
static void test_round_trip_percentages_in_both_word_orders(void)
{
    for (size_t o = 0; o < sizeof(ORDERS) / sizeof(ORDERS[0]); ++o) {
        for (size_t i = 0; i < PERCENT_COUNT; ++i) {
            uint16_t words[2] = {0, 0};
            assert(inverter_profile_encode_value((double)PERCENTS[i],
                                                 INVERTER_VALUE_FLOAT32,
                                                 ORDERS[o], 2, words) == ESP_OK);
            float decoded = -1.0f;
            assert(inverter_profile_decode_value(words, 2, INVERTER_VALUE_FLOAT32,
                                                 ORDERS[o], 1.0f, &decoded) == ESP_OK);
            /* Exact: a float32 that came from a float32 bit pattern loses nothing.
             * Anything less than exactness here would mean bits were mangled. */
            assert(decoded == PERCENTS[i]);
        }
    }
}

/* Decoding with the WRONG word order must not quietly produce something
 * plausible. This is the value the old encoder would have put on a SolarEdge
 * inverter, and the point is that it is not a small error. */
static void test_the_wrong_word_order_is_catastrophically_wrong(void)
{
    uint16_t words[2] = {0, 0};
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, words) == ESP_OK);
    float wrong = -1.0f;
    esp_err_t err = inverter_profile_decode_value(words, 2, INVERTER_VALUE_FLOAT32,
                                                  INVERTER_WORD_ORDER_AB, 1.0f, &wrong);
    /* Either it is refused as non-finite, or it decodes to a number nowhere near
     * 50. Both are acceptable; silently landing near 50 would not be. */
    if (err == ESP_OK) {
        assert(fabsf(wrong - 50.0f) > 1.0f);
    } else {
        assert(err == ESP_ERR_INVALID_RESPONSE);
    }
}

/* A scale still applies to a float register, for a device documenting a float in
 * device units rather than percent. */
static void test_scale_applies_to_a_float_register(void)
{
    uint16_t words[2] = {0, 0};
    /* 100000 W encoded as a float, read back scaled to kW. */
    assert(inverter_profile_encode_value(100000.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, words) == ESP_OK);
    float kw = 0.0f;
    assert(inverter_profile_decode_value(words, 2, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 0.001f, &kw) == ESP_OK);
    assert(fabsf(kw - 100.0f) < 0.001f);
}

/* NON-FINITE REJECTION, read path. A float register can legally hold NaN -- some
 * manufacturers use it as "not available" -- and NaN compares unequal to
 * everything including itself, so returning it would poison the confirmation
 * comparison. It must be refused, and the caller's output must be left alone so a
 * previous good reading is not overwritten with garbage. */
static void test_non_finite_readings_are_refused(void)
{
    const uint32_t patterns[] = {
        0x7FC00000u, /* quiet NaN            */
        0x7FA00000u, /* signalling NaN       */
        0xFFC00000u, /* negative NaN         */
        0x7F800000u, /* +infinity            */
        0xFF800000u, /* -infinity            */
    };

    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        for (size_t o = 0; o < sizeof(ORDERS) / sizeof(ORDERS[0]); ++o) {
            uint16_t high = (uint16_t)(patterns[p] >> 16);
            uint16_t low = (uint16_t)patterns[p];
            uint16_t words[2];
            if (ORDERS[o] == INVERTER_WORD_ORDER_AB) {
                words[0] = high;
                words[1] = low;
            } else {
                words[0] = low;
                words[1] = high;
            }

            float sentinel = 42.0f;
            esp_err_t err = inverter_profile_decode_value(words, 2, INVERTER_VALUE_FLOAT32,
                                                          ORDERS[o], 1.0f, &sentinel);
            assert(err == ESP_ERR_INVALID_RESPONSE);
            /* Untouched: an unusable sample must not overwrite the caller's value. */
            assert(sentinel == 42.0f);
        }
    }
}

/* The project's convention, executed rather than described: a non-finite reading
 * is refused with ESP_ERR_INVALID_RESPONSE -- the same code modbus_decode_scaled()
 * uses -- and NOT with an argument error. The distinction matters because the
 * acquisition path treats a decode failure as "no sample yet" and the write
 * confirmation then holds at PENDING until its deadline. "Keep waiting", not
 * "fault", and never "confirmed". */
static void test_the_refusal_code_is_the_project_convention(void)
{
    uint16_t nan_words[2] = {0x0000u, 0x7FC0u}; /* quiet NaN, LSW first */
    float value = 0.0f;
    assert(inverter_profile_decode_value(nan_words, 2, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 1.0f, &value)
           == ESP_ERR_INVALID_RESPONSE);

    /* A malformed request is a DIFFERENT failure and must not be conflated: too
     * few registers is a size error, a bad scale is an argument error. */
    assert(inverter_profile_decode_value(nan_words, 1, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 1.0f, &value)
           == ESP_ERR_INVALID_SIZE);
    assert(inverter_profile_decode_value(nan_words, 2, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, NAN, &value)
           == ESP_ERR_INVALID_ARG);

    /* And a non-finite readback can never satisfy the readback comparison, so it
     * can never confirm a command. */
    assert(!inverter_profile_readback_matches(50.0f, NAN, 0.2f));
    assert(!inverter_profile_readback_matches(50.0f, INFINITY, 0.2f));
    assert(!inverter_profile_readback_matches(NAN, 50.0f, 0.2f));
}

/* NON-FINITE REJECTION, write path. Nothing may be encoded from a value that is
 * not a number: refusing means no Modbus frame is issued at all, whereas emitting
 * 0x7F800000 would hand an infinity to a 100 kW inverter and only then discover
 * the readback could not be confirmed. */
static void test_non_finite_commands_are_refused(void)
{
    const double bad[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        for (size_t o = 0; o < sizeof(ORDERS) / sizeof(ORDERS[0]); ++o) {
            uint16_t words[2] = {0xAAAAu, 0x5555u};
            assert(inverter_profile_encode_value(bad[i], INVERTER_VALUE_FLOAT32,
                                                 ORDERS[o], 2, words)
                   == ESP_ERR_INVALID_ARG);
            /* Nothing written: a refused command must leave no bytes behind. */
            assert(words[0] == 0xAAAAu && words[1] == 0x5555u);
        }
    }

    /* A finite double that cannot survive narrowing to float32 is refused for the
     * same reason -- it would become an infinity on the wire. */
    uint16_t words[2] = {0, 0};
    assert(inverter_profile_encode_value(1e300, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 2, words)
           == ESP_ERR_INVALID_ARG);
}

/* Width must MATCH the type, not merely be sufficient. A two-register write aimed
 * at a one-register description would overwrite the neighbouring address, and in
 * these manuals the neighbours are reactive-power limits and mode selectors. */
static void test_register_width_must_match_the_type(void)
{
    uint16_t words[2] = {0, 0};
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_FLOAT32,
                                         INVERTER_WORD_ORDER_BA, 1, words)
           == ESP_ERR_INVALID_SIZE);
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_U16,
                                         INVERTER_WORD_ORDER_AB, 2, words)
           == ESP_ERR_INVALID_SIZE);
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_U32,
                                         INVERTER_WORD_ORDER_AB, 1, words)
           == ESP_ERR_INVALID_SIZE);

    assert(inverter_value_type_is_wide(INVERTER_VALUE_FLOAT32));
    assert(inverter_value_type_is_wide(INVERTER_VALUE_U32));
    assert(inverter_value_type_is_wide(INVERTER_VALUE_S32));
    assert(!inverter_value_type_is_wide(INVERTER_VALUE_U16));
    assert(!inverter_value_type_is_wide(INVERTER_VALUE_S16));
}

/* The integer paths must be unchanged by all of this. Every profile in the
 * catalogue that predates the command-side word order leaves it zero (= AB), which
 * is exactly what the encoder used to hardcode, so their bytes must be identical.
 * A regression here would silently change the value written to nine real brands. */
static void test_integer_encoding_matches_the_previous_behaviour(void)
{
    /* Huawei/FoxESS/GoodWe shape: percent x 10 in one U16 register. */
    uint16_t one[1] = {0};
    assert(inverter_profile_encode_value(47.3 * 10.0, INVERTER_VALUE_U16,
                                         INVERTER_WORD_ORDER_AB, 1, one) == ESP_OK);
    assert(one[0] == 473u);

    /* Solis/AISWEI shape: percent x 100. */
    assert(inverter_profile_encode_value(47.3 * 100.0, INVERTER_VALUE_U16,
                                         INVERTER_WORD_ORDER_AB, 1, one) == ESP_OK);
    assert(one[0] == 4730u);

    /* Rounding is to nearest, as llround() always did. */
    assert(inverter_profile_encode_value(4729.5, INVERTER_VALUE_U16,
                                         INVERTER_WORD_ORDER_AB, 1, one) == ESP_OK);
    assert(one[0] == 4730u);

    /* Two-register integer, AB: high word first, exactly as the old
     * words[0] = raw >> 16; words[1] = raw; did. */
    uint16_t two[2] = {0, 0};
    assert(inverter_profile_encode_value(0x12345678, INVERTER_VALUE_U32,
                                         INVERTER_WORD_ORDER_AB, 2, two) == ESP_OK);
    assert(two[0] == 0x1234u && two[1] == 0x5678u);
    assert(inverter_profile_encode_value(0x12345678, INVERTER_VALUE_U32,
                                         INVERTER_WORD_ORDER_BA, 2, two) == ESP_OK);
    assert(two[0] == 0x5678u && two[1] == 0x1234u);

    /* Out of range is refused rather than truncated: a wrapped setpoint is a
     * command nobody asked for. */
    assert(inverter_profile_encode_value(65536.0, INVERTER_VALUE_U16,
                                         INVERTER_WORD_ORDER_AB, 1, one)
           == ESP_ERR_INVALID_ARG);
    assert(inverter_profile_encode_value(-1.0, INVERTER_VALUE_U16,
                                         INVERTER_WORD_ORDER_AB, 1, one)
           == ESP_ERR_INVALID_ARG);
    assert(inverter_profile_encode_value(4294967296.0, INVERTER_VALUE_U32,
                                         INVERTER_WORD_ORDER_AB, 2, two)
           == ESP_ERR_INVALID_ARG);
}

/* Signed integers round-trip too, in both orders, so the encoder is a genuine
 * inverse of the decoder across every type it supports rather than only for the
 * float that motivated it. */
static void test_signed_integers_round_trip(void)
{
    const double values[] = {-32768.0, -1.0, 0.0, 1.0, 32767.0};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        uint16_t one[1] = {0};
        assert(inverter_profile_encode_value(values[i], INVERTER_VALUE_S16,
                                            INVERTER_WORD_ORDER_AB, 1, one) == ESP_OK);
        float decoded = 0.0f;
        assert(inverter_profile_decode_value(one, 1, INVERTER_VALUE_S16,
                                            INVERTER_WORD_ORDER_AB, 1.0f, &decoded) == ESP_OK);
        assert((double)decoded == values[i]);
    }

    const double wide[] = {-2147483648.0, -70000.0, 0.0, 70000.0, 2147483647.0};
    for (size_t i = 0; i < sizeof(wide) / sizeof(wide[0]); ++i) {
        for (size_t o = 0; o < sizeof(ORDERS) / sizeof(ORDERS[0]); ++o) {
            uint16_t two[2] = {0, 0};
            assert(inverter_profile_encode_value(wide[i], INVERTER_VALUE_S32,
                                                ORDERS[o], 2, two) == ESP_OK);
            float decoded = 0.0f;
            assert(inverter_profile_decode_value(two, 2, INVERTER_VALUE_S32,
                                                ORDERS[o], 1.0f, &decoded) == ESP_OK);
            /* float32 cannot hold every int32 exactly, so compare at float
             * precision -- the bytes are what is under test here. */
            assert(fabs((double)decoded - wide[i]) <= fabs(wide[i]) * 1e-6);
        }
    }
}

/* Null and unknown-type arguments must fail closed on both paths. */
static void test_arguments_fail_closed(void)
{
    uint16_t words[2] = {0, 0};
    float value = 0.0f;
    assert(inverter_profile_encode_value(50.0, INVERTER_VALUE_FLOAT32,
                                        INVERTER_WORD_ORDER_BA, 2, NULL)
           == ESP_ERR_INVALID_ARG);
    assert(inverter_profile_decode_value(NULL, 2, INVERTER_VALUE_FLOAT32,
                                        INVERTER_WORD_ORDER_BA, 1.0f, &value)
           == ESP_ERR_INVALID_ARG);
    assert(inverter_profile_decode_value(words, 2, INVERTER_VALUE_FLOAT32,
                                        INVERTER_WORD_ORDER_BA, 1.0f, NULL)
           == ESP_ERR_INVALID_ARG);

    /* An out-of-range type is not a float and not an integer: it is unsupported,
     * not "close enough to U16". */
    assert(inverter_profile_encode_value(50.0, (inverter_value_type_t)99,
                                        INVERTER_WORD_ORDER_AB, 1, words)
           == ESP_ERR_NOT_SUPPORTED);
    assert(inverter_profile_decode_value(words, 2, (inverter_value_type_t)99,
                                        INVERTER_WORD_ORDER_AB, 1.0f, &value)
           == ESP_ERR_NOT_SUPPORTED);
}

int main(void)
{
    test_float_encoding_is_ieee754_not_an_integer();
    test_both_word_orders_place_the_same_bits();
    test_round_trip_percentages_in_both_word_orders();
    test_the_wrong_word_order_is_catastrophically_wrong();
    test_scale_applies_to_a_float_register();
    test_non_finite_readings_are_refused();
    test_the_refusal_code_is_the_project_convention();
    test_non_finite_commands_are_refused();
    test_register_width_must_match_the_type();
    test_integer_encoding_matches_the_previous_behaviour();
    test_signed_integers_round_trip();
    test_arguments_fail_closed();
    printf("inverter float register encode/decode unit tests passed\n");
    return 0;
}
