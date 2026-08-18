#include <assert.h>

#include "waveshare_display_profile.h"

static void test_800x480(void)
{
    const waveshare_display_profile_t *p = waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    assert(p != 0);
    assert(waveshare_display_profile_valid(p));
    assert(p->width == 800U);
    assert(p->height == 480U);
    assert(p->pixel_clock_hz == 16000000U);
    assert(p->hsync_pulse_width == 4U);
    assert(p->hsync_back_porch == 8U);
    assert(p->hsync_front_porch == 8U);
    assert(p->vsync_pulse_width == 4U);
    assert(p->vsync_back_porch == 8U);
    assert(p->vsync_front_porch == 8U);
}

static void test_1024x600(void)
{
    const waveshare_display_profile_t *p = waveshare_display_profile(WAVESHARE_DISPLAY_1024X600);
    assert(p != 0);
    assert(waveshare_display_profile_valid(p));
    assert(p->width == 1024U);
    assert(p->height == 600U);
    assert(p->pixel_clock_hz == 21000000U);
    assert(p->hsync_pulse_width == 30U);
    assert(p->hsync_back_porch == 145U);
    assert(p->hsync_front_porch == 170U);
    assert(p->vsync_pulse_width == 2U);
    assert(p->vsync_back_porch == 23U);
    assert(p->vsync_front_porch == 12U);
}

static void test_shared_bus_and_rgb_pins(void)
{
    static const int expected[16] = {
        14, 38, 18, 17, 10, 39, 0, 45,
        48, 47, 21, 1, 2, 42, 41, 40,
    };
    const waveshare_display_profile_t *a = waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    const waveshare_display_profile_t *b = waveshare_display_profile(WAVESHARE_DISPLAY_1024X600);
    assert(a && b);
    assert(a->vsync_gpio == 3 && a->hsync_gpio == 46 && a->de_gpio == 5 && a->pclk_gpio == 7);
    assert(a->i2c_sda_gpio == 8 && a->i2c_scl_gpio == 9 && a->i2c_frequency_hz == 400000U);
    assert(a->gt911_address_select_gpio == 4);
    for (int i = 0; i < 16; ++i) {
        assert(a->data_gpio[i] == expected[i]);
        assert(b->data_gpio[i] == expected[i]);
    }
}

static void test_invalid_variant(void)
{
    assert(waveshare_display_profile((waveshare_display_variant_t)99) == 0);
    assert(!waveshare_display_profile_valid(0));
}

int main(void)
{
    test_800x480();
    test_1024x600();
    test_shared_bus_and_rgb_pins();
    test_invalid_variant();
    return 0;
}
