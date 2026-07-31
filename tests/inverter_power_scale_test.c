/*
 * Power-limit scale-factor arithmetic, in BOTH directions, against the real
 * profile table.
 *
 * A scale error here is silent and dangerous. Commanding 45 % through a x10
 * register as the raw word 45 tells the inverter 4.5 %, and the readback decodes
 * the same wrong word with the same wrong scale, agrees with the request, and
 * reports the command CONFIRMED. Nothing downstream can detect it. The only
 * defence is asserting the exact word that goes on the wire.
 *
 * Owner-stated, and the two facts this file exists to pin:
 *   Huawei  register 40125, scale 10  -- 45 % is the word 450, 100 % is 1000
 *   Solis                   scale 100 -- 100 % is the word 10000
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "inverter_profile_decode.h"
#include "inverter_profiles.h"

/* Mirrors encode_command() in inverter_manager.c: percent x raw_units_per_percent,
 * then the profile's own word encoding. Kept to the same two steps so a change to
 * either one shows up here. */
static uint16_t encode_percent_word(const inverter_profile_t *profile, float percent)
{
    assert(profile);
    assert(profile->has_power_limit);
    assert(profile->power_limit_words == 1U);

    const double raw = (double)percent * (double)profile->raw_units_per_percent;
    uint16_t words[2] = {0U, 0U};
    const inverter_value_type_t type =
        profile->power_limit_type == INVERTER_VALUE_FLOAT32
            ? INVERTER_VALUE_FLOAT32
            : INVERTER_VALUE_U16;
    assert(inverter_profile_encode_value(raw, type, profile->power_limit_word_order,
                                         profile->power_limit_words, words) == ESP_OK);
    return words[0];
}

static float decode_percent_word(const inverter_profile_t *profile, uint16_t word)
{
    assert(profile);
    assert(profile->has_power_limit_readback);
    assert(profile->power_limit_readback_words == 1U);

    float percent = NAN;
    const uint16_t registers[1] = {word};
    assert(inverter_profile_decode_value(registers, 1U,
                                         profile->power_limit_readback_type,
                                         profile->power_limit_readback_word_order,
                                         profile->power_limit_readback_scale,
                                         &percent) == ESP_OK);
    return percent;
}

static bool close_enough(float a, float b)
{
    return isfinite(a) && isfinite(b) && fabsf(a - b) < 0.001f;
}

/* The command scale and the readback scale must be exact reciprocals, otherwise
 * a write and its confirmation are speaking different units. This is a PROPERTY
 * of every commandable profile, not a fact about one brand. */
static void test_every_profile_command_and_readback_scales_are_reciprocal(void)
{
    size_t checked = 0U;
    for (size_t i = 0U; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile);
        if (!profile->has_power_limit || !profile->has_power_limit_readback) continue;
        /* SmartLogger's acceptance readback is a DIFFERENT register with its own
         * documented gain of 1 against a command gain of 10. It is explicitly not
         * a unit-for-unit confirmation, so it is not held to reciprocity. */
        if (profile->power_limit_address != profile->power_limit_readback_address) continue;

        assert(isfinite(profile->raw_units_per_percent));
        assert(profile->raw_units_per_percent > 0.0f);
        assert(isfinite(profile->power_limit_readback_scale));
        assert(profile->power_limit_readback_scale > 0.0f);
        assert(close_enough(profile->raw_units_per_percent *
                                profile->power_limit_readback_scale,
                            1.0f));
        checked++;
    }
    assert(checked > 0U);
}

/* Round-tripping any representable percent must return the same percent. Again a
 * property: it holds for every single-register integer profile in the table, so a
 * new brand added with mismatched scales fails here without anyone remembering to
 * add a case. */
static void test_round_trip_holds_for_every_single_register_profile(void)
{
    static const float percents[] = {0.0f, 1.0f, 12.5f, 45.0f, 99.0f, 100.0f};
    size_t checked = 0U;

    for (size_t i = 0U; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile);
        if (!profile->has_power_limit || !profile->has_power_limit_readback) continue;
        if (profile->power_limit_words != 1U) continue;
        if (profile->power_limit_readback_words != 1U) continue;
        if (profile->power_limit_address != profile->power_limit_readback_address) continue;
        if (profile->power_limit_type == INVERTER_VALUE_FLOAT32) continue;

        for (size_t p = 0U; p < sizeof(percents) / sizeof(percents[0]); ++p) {
            const float percent = percents[p];
            if (percent < profile->minimum_percent) continue;
            if (percent > profile->maximum_percent) continue;
            /* Only percents the register can actually represent: a x1 register
             * cannot carry 12.5 and is not expected to. */
            const double raw = (double)percent * (double)profile->raw_units_per_percent;
            if (fabs(raw - floor(raw + 0.5)) > 1e-6) continue;

            const uint16_t word = encode_percent_word(profile, percent);
            assert(close_enough(decode_percent_word(profile, word), percent));
            checked++;
        }
    }
    assert(checked > 0U);
}

/* The owner's two stated facts, asserted as exact words on the wire. */
static void test_huawei_scale_is_ten(void)
{
    const inverter_profile_t *profile = inverter_profiles_find("huawei.sun2000.pending");
    assert(profile);
    assert(profile->has_power_limit);
    assert(profile->power_limit_address == 40125U);
    assert(profile->power_limit_readback_address == 40125U);
    assert(close_enough(profile->raw_units_per_percent, 10.0f));
    assert(close_enough(profile->power_limit_readback_scale, 0.1f));

    /* percent -> register */
    assert(encode_percent_word(profile, 0.0f) == 0U);
    assert(encode_percent_word(profile, 45.0f) == 450U);
    assert(encode_percent_word(profile, 100.0f) == 1000U);
    /* The bug this file exists to catch: 45 % must NOT go out as 45. */
    assert(encode_percent_word(profile, 45.0f) != 45U);

    /* register -> percent */
    assert(close_enough(decode_percent_word(profile, 0U), 0.0f));
    assert(close_enough(decode_percent_word(profile, 450U), 45.0f));
    assert(close_enough(decode_percent_word(profile, 1000U), 100.0f));
    /* A raw 45 means 4.5 %, which is the damage a missing scale would do. */
    assert(close_enough(decode_percent_word(profile, 45U), 4.5f));
}

static void test_solis_scale_is_one_hundred(void)
{
    const inverter_profile_t *profile = inverter_profiles_find("solis.commercial.pending");
    assert(profile);
    assert(profile->has_power_limit);
    assert(close_enough(profile->raw_units_per_percent, 100.0f));
    assert(close_enough(profile->power_limit_readback_scale, 0.01f));

    /* percent -> register */
    assert(encode_percent_word(profile, 0.0f) == 0U);
    assert(encode_percent_word(profile, 45.0f) == 4500U);
    assert(encode_percent_word(profile, 100.0f) == 10000U);
    assert(encode_percent_word(profile, 100.0f) != 1000U); /* not the Huawei scale */
    assert(encode_percent_word(profile, 100.0f) != 100U);  /* not unscaled */

    /* register -> percent */
    assert(close_enough(decode_percent_word(profile, 0U), 0.0f));
    assert(close_enough(decode_percent_word(profile, 4500U), 45.0f));
    assert(close_enough(decode_percent_word(profile, 10000U), 100.0f));
}

/* The two brands the owner stated must not share a scale. Asserted directly so
 * that copying one profile over the other is caught. */
static void test_huawei_and_solis_scales_differ(void)
{
    const inverter_profile_t *huawei = inverter_profiles_find("huawei.sun2000.pending");
    const inverter_profile_t *solis = inverter_profiles_find("solis.commercial.pending");
    assert(huawei && solis);
    assert(!close_enough(huawei->raw_units_per_percent, solis->raw_units_per_percent));
    assert(encode_percent_word(huawei, 100.0f) != encode_percent_word(solis, 100.0f));
}

/* 100 % must fit the register for every profile in the table. A scale large
 * enough to overflow 16 bits at full output would refuse to encode the one
 * command that means "stop curtailing". */
static void test_full_output_is_representable_everywhere(void)
{
    for (size_t i = 0U; i < inverter_profiles_count(); ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        assert(profile);
        if (!profile->has_power_limit) continue;
        if (profile->power_limit_words != 1U) continue;
        if (profile->power_limit_type == INVERTER_VALUE_FLOAT32) continue;
        const double raw = (double)profile->maximum_percent *
                           (double)profile->raw_units_per_percent;
        assert(raw >= 0.0);
        assert(raw <= 65535.0);
    }
}

int main(void)
{
    test_every_profile_command_and_readback_scales_are_reciprocal();
    test_round_trip_holds_for_every_single_register_profile();
    test_huawei_scale_is_ten();
    test_solis_scale_is_one_hundred();
    test_huawei_and_solis_scales_differ();
    test_full_output_is_representable_everywhere();
    puts("Inverter power-limit scale factor unit tests passed");
    return 0;
}
