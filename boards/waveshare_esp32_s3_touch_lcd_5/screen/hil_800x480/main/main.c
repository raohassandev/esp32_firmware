#include <assert.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "screen_app.h"
#include "waveshare_display_port.h"
#include "waveshare_display_profile.h"

static const char *TAG = "waveshare_hil";
static waveshare_display_port_handles_t s_display;

void app_main(void)
{
    const waveshare_display_profile_t *profile =
        waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    assert(profile != NULL);
    assert(profile->width == 800U && profile->height == 480U);

    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode =
        ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    const waveshare_display_port_config_t display_config = {
        .profile = profile,
        .i2c_bus = NULL,
        .tear_mode = tear_mode,
        .rotation = rotation,
        .enable_touch = true,
    };

    ESP_LOGI(TAG, "HIL target: Waveshare ESP32-S3-Touch-LCD-5 800x480");
    ESP_ERROR_CHECK(waveshare_display_port_init(&display_config, &s_display));
    ESP_ERROR_CHECK(waveshare_display_port_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t lv_display_config =
        ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
            s_display.panel,
            NULL,
            profile->width,
            profile->height,
            rotation);
    lv_display_config.profile.use_psram = true;

    lv_display_t *display = esp_lv_adapter_register_display(&lv_display_config);
    assert(display != NULL);

    if (s_display.touch != NULL) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_display.touch);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    assert(screen_app_create(lv_screen_active()) != NULL);
    /* HIL must never fabricate plant measurements. Until a qualified provider is
     * bound to the existing backend authority, render every plant surface as
     * unavailable while still exercising the real LCD, LVGL and touch path. */
    screen_app_show_backend_unavailable();
    esp_lv_adapter_unlock();

    ESP_LOGI(TAG, "LCD/LVGL/touch bring-up started; backend values intentionally unavailable");
}
