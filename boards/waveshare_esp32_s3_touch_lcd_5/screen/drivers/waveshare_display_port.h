#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"
#include "waveshare_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAVESHARE_RGB_DEFAULT_BOUNCE_LINES 10U

typedef struct {
    const waveshare_display_profile_t *profile;
    /* Optional board-owned shared I2C bus. When NULL, this port creates and owns
     * a bus on the profile's SDA/SCL pins. This keeps future RTC/board support
     * free to become the shared-bus owner without changing the screen API. */
    i2c_master_bus_handle_t i2c_bus;
    esp_lv_adapter_tear_avoid_mode_t tear_mode;
    esp_lv_adapter_rotation_t rotation;
    bool enable_touch;
    /* Non-zero enables ESP-IDF's two internal-DRAM RGB bounce buffers. The
     * product image may allow a one-time retry with zero lines when the shared
     * Core has already fragmented internal DMA-capable memory. Frame buffers
     * remain in PSRAM in both modes. */
    uint16_t bounce_buffer_lines;
    bool allow_no_bounce_fallback;
} waveshare_display_port_config_t;

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_touch_handle_t touch;
    i2c_master_bus_handle_t i2c_bus;
    bool owns_i2c_bus;
} waveshare_display_port_handles_t;

/* Board-local physical driver only. It initializes RGB panel and optional GT911
 * touch using the explicit 800x480 or 1024x600 profile. It does not create LVGL
 * pages, backend state, tasks, control callbacks or product logic. */
esp_err_t waveshare_display_port_init(const waveshare_display_port_config_t *config,
                                      waveshare_display_port_handles_t *out);

/* The pinned vendor baseline explicitly demonstrates only the ON command. Do
 * not infer an OFF bit pattern that has not been qualified against the exact
 * board/schematic. */
esp_err_t waveshare_display_port_backlight_on(void);

/* Releases only resources owned by this port. Panel/touch deletion is attempted
 * before a locally-created I2C bus is released. */
esp_err_t waveshare_display_port_deinit(waveshare_display_port_handles_t *handles);

#ifdef __cplusplus
}
#endif
