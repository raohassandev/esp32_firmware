#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAVESHARE_DISPLAY_800X480 = 0,
    WAVESHARE_DISPLAY_1024X600 = 1,
} waveshare_display_variant_t;

typedef struct {
    waveshare_display_variant_t variant;
    uint16_t width;
    uint16_t height;
    uint32_t pixel_clock_hz;

    uint16_t hsync_pulse_width;
    uint16_t hsync_back_porch;
    uint16_t hsync_front_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_back_porch;
    uint16_t vsync_front_porch;
    bool pclk_active_negative;

    int8_t vsync_gpio;
    int8_t hsync_gpio;
    int8_t de_gpio;
    int8_t pclk_gpio;
    int8_t data_gpio[16];

    int8_t i2c_sda_gpio;
    int8_t i2c_scl_gpio;
    uint32_t i2c_frequency_hz;
    int8_t gt911_address_select_gpio;
} waveshare_display_profile_t;

/* Values are transcribed from the pinned official Waveshare ESP-IDF LVGL v9
 * baseline at a7b179dbfccea8121c88770d8a3c53e5a84b1024. Selecting a profile is
 * explicit; there is deliberately no guessed/default physical SKU here. */
const waveshare_display_profile_t *waveshare_display_profile(waveshare_display_variant_t variant);
bool waveshare_display_profile_valid(const waveshare_display_profile_t *profile);

#ifdef __cplusplus
}
#endif
