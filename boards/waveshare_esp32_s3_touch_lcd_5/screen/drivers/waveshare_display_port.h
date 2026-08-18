#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"
#include "waveshare_display_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const waveshare_display_profile_t *profile;
    /* Optional board-owned shared I2C bus. When NULL, this port creates and owns
     * a bus on the profile's SDA/SCL pins. This keeps future RTC/board support
     * free to become the shared-bus owner without changing the screen API. */
    i2c_master_bus_handle_t i2c_bus;
    esp_lv_adapter_tear_avoid_mode_t tear_mode;
    esp_lv_adapter_rotation_t rotation;
    bool enable_touch;
} waveshare_display_port_config_t;

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    i2c_master_bus_handle_t i2c_bus;
    bool owns_i2c_bus;
} waveshare_display_port_handles_t;

/* Board-local physical driver only. It initializes RGB panel and optional GT911
 * touch using the explicit 800x480 or 1024x600 profile. It does not create LVGL
 * pages, backend state, tasks, control callbacks or product logic. */
esp_err_t waveshare_display_port_init(const waveshare_display_port_config_t *config,
                                      waveshare_display_port_handles_t *out);

/* CH422G-controlled display backlight. Requires a successful init. */
esp_err_t waveshare_display_port_backlight_set(bool on);

/* Releases only resources owned by this port. Panel/touch deletion is attempted
 * before a locally-created I2C bus is released. */
esp_err_t waveshare_display_port_deinit(waveshare_display_port_handles_t *handles);

#ifdef __cplusplus
}
#endif
