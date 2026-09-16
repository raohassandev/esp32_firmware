#include "waveshare_display_profile.h"

#include <stddef.h>

static const int8_t RGB_DATA_GPIOS[16] = {
    14, 38, 18, 17, 10, 39, 0, 45,
    48, 47, 21, 1, 2, 42, 41, 40,
};

static const waveshare_display_profile_t PROFILE_800X480 = {
    .variant = WAVESHARE_DISPLAY_800X480,
    .width = 800,
    .height = 480,
    .pixel_clock_hz = 16000000U,
    .hsync_pulse_width = 4,
    .hsync_back_porch = 8,
    .hsync_front_porch = 8,
    .vsync_pulse_width = 4,
    .vsync_back_porch = 8,
    .vsync_front_porch = 8,
    .pclk_active_negative = true,
    .vsync_gpio = 3,
    .hsync_gpio = 46,
    .de_gpio = 5,
    .pclk_gpio = 7,
    .data_gpio = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
    .i2c_sda_gpio = 8,
    .i2c_scl_gpio = 9,
    .i2c_frequency_hz = 400000U,
    /* GPIO4 is driven during the GT911 reset/address-selection sequence in the
     * pinned vendor port; naming it by observed purpose avoids calling it the
     * GT911 reset pin when the reset itself is also controlled via CH422G. */
    .gt911_address_select_gpio = 4,
};

static const waveshare_display_profile_t PROFILE_1024X600 = {
    .variant = WAVESHARE_DISPLAY_1024X600,
    .width = 1024,
    .height = 600,
    .pixel_clock_hz = 21000000U,
    .hsync_pulse_width = 30,
    .hsync_back_porch = 145,
    .hsync_front_porch = 170,
    .vsync_pulse_width = 2,
    .vsync_back_porch = 23,
    .vsync_front_porch = 12,
    .pclk_active_negative = true,
    .vsync_gpio = 3,
    .hsync_gpio = 46,
    .de_gpio = 5,
    .pclk_gpio = 7,
    .data_gpio = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
    .i2c_sda_gpio = 8,
    .i2c_scl_gpio = 9,
    .i2c_frequency_hz = 400000U,
    .gt911_address_select_gpio = 4,
};

const waveshare_display_profile_t *waveshare_display_profile(waveshare_display_variant_t variant)
{
    switch (variant) {
    case WAVESHARE_DISPLAY_800X480:
        return &PROFILE_800X480;
    case WAVESHARE_DISPLAY_1024X600:
        return &PROFILE_1024X600;
    default:
        return NULL;
    }
}

bool waveshare_display_profile_valid(const waveshare_display_profile_t *profile)
{
    if (!profile || profile->width == 0U || profile->height == 0U ||
        profile->pixel_clock_hz == 0U || profile->i2c_frequency_hz == 0U) {
        return false;
    }
    if (profile->vsync_gpio < 0 || profile->hsync_gpio < 0 ||
        profile->de_gpio < 0 || profile->pclk_gpio < 0 ||
        profile->i2c_sda_gpio < 0 || profile->i2c_scl_gpio < 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(RGB_DATA_GPIOS) / sizeof(RGB_DATA_GPIOS[0]); ++i) {
        if (profile->data_gpio[i] != RGB_DATA_GPIOS[i]) return false;
    }
    return true;
}
