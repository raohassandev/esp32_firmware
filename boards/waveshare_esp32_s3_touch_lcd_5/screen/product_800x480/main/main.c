#include <stdbool.h>
#include <stdint.h>

#include "app_core.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "local_backend_provider.h"
#include "lvgl.h"
#include "screen_api.h"
#include "screen_app.h"
#include "screen_runtime.h"
#include "waveshare_display_port.h"
#include "waveshare_display_profile.h"

#define BOOTSTRAP_STACK_BYTES 16384
#define SCREEN_REFRESH_STACK_BYTES 12288
#define SCREEN_FAST_MS 500U
#define SCREEN_STATUS_MS 5000U
#define SCREEN_DEVICES_MS 10000U
#define SCREEN_OPERATIONS_MS 5000U

static const char *TAG = "waveshare_product";
static waveshare_display_port_handles_t s_display;

static esp_err_t native_screen_init(void)
{
    const waveshare_display_profile_t *profile =
        waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    if (!profile || profile->width != 800U || profile->height != 480U) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode =
        ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;
    const waveshare_display_port_config_t display_config = {
        .profile = profile,
        .i2c_bus = NULL,
        .tear_mode = tear_mode,
        .rotation = rotation,
        .enable_touch = true,
        .bounce_buffer_lines = WAVESHARE_RGB_DEFAULT_BOUNCE_LINES,
        .allow_no_bounce_fallback = true,
    };

    ESP_LOGI(TAG, "Initializing native 800x480 Waveshare LCD/touch");
    esp_err_t err = waveshare_display_port_init(&display_config, &s_display);
    if (err != ESP_OK) return err;
    err = waveshare_display_port_backlight_on();
    if (err != ESP_OK) return err;

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    err = esp_lv_adapter_init(&adapter_config);
    if (err != ESP_OK) return err;

    esp_lv_adapter_display_config_t lv_display_config =
        ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
            s_display.panel, NULL, profile->width, profile->height, rotation);
    lv_display_config.profile.use_psram = true;

    lv_display_t *display = esp_lv_adapter_register_display(&lv_display_config);
    if (!display) return ESP_FAIL;

    if (s_display.touch) {
        esp_lv_adapter_touch_config_t touch_config =
            ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, s_display.touch);
        if (!esp_lv_adapter_register_touch(&touch_config)) return ESP_FAIL;
    }

    err = esp_lv_adapter_start();
    if (err != ESP_OK) return err;

    err = esp_lv_adapter_lock(-1);
    if (err != ESP_OK) return err;
    lv_obj_t *root = screen_app_create(lv_screen_active());
    if (root) screen_app_show_backend_unavailable();
    esp_lv_adapter_unlock();
    if (!root) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Native LCD/LVGL/touch ready");
    return ESP_OK;
}

static void refresh_fast(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_LIVE_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_fast();
        esp_lv_adapter_unlock();
    }
}

static void refresh_status(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_STATUS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_TELEMETRY_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_status();
        esp_lv_adapter_unlock();
    }
}

static void refresh_devices(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_METERS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_INVERTERS_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_devices();
        esp_lv_adapter_unlock();
    }
}

static void refresh_operations(void)
{
    (void)local_backend_provider_fetch(SCREEN_API_ALARMS_PATH);
    (void)local_backend_provider_fetch(SCREEN_API_EVENTS_PATH);
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        (void)screen_runtime_refresh_operations();
        esp_lv_adapter_unlock();
    }
}

static void screen_refresh_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    uint32_t elapsed_ms = 0U;

    /* First pass populates every page; subsequent passes keep the fast lane at
     * 500 ms while slower operator surfaces are deliberately less frequent. */
    refresh_status();
    refresh_devices();
    refresh_operations();

    for (;;) {
        refresh_fast();
        elapsed_ms += SCREEN_FAST_MS;

        if ((elapsed_ms % SCREEN_STATUS_MS) == 0U) refresh_status();
        if ((elapsed_ms % SCREEN_DEVICES_MS) == 0U) refresh_devices();
        if ((elapsed_ms % SCREEN_OPERATIONS_MS) == 0U) refresh_operations();

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SCREEN_FAST_MS));
    }
}

static void bootstrap_task(void *argument)
{
    (void)argument;

    esp_err_t core_err = app_core_init();
    if (core_err != ESP_OK) {
        ESP_LOGE(TAG, "Product Core initialization failed: %s; controller remains fail-safe",
                 esp_err_to_name(core_err));
    } else {
        ESP_LOGI(TAG, "Shared Product Core started");
    }

    esp_err_t screen_err = native_screen_init();
    if (screen_err != ESP_OK) {
        ESP_LOGE(TAG, "Native screen initialization failed: %s; Core continues headless",
                 esp_err_to_name(screen_err));
    } else if (core_err == ESP_OK) {
        screen_api_provider_t provider = {0};
        if (local_backend_provider_init(&provider) && screen_runtime_init(&provider)) {
            BaseType_t created = xTaskCreate(
                screen_refresh_task,
                "screen_refresh",
                SCREEN_REFRESH_STACK_BYTES,
                NULL,
                5,
                NULL);
            if (created == pdPASS) {
                ESP_LOGI(TAG, "Screen bound read-only to existing Core API over loopback");
            } else {
                ESP_LOGE(TAG, "Unable to create screen refresh task; UI stays unavailable");
            }
        } else {
            ESP_LOGE(TAG, "Screen backend provider initialization failed; UI stays unavailable");
        }
    }

    ESP_LOGI(TAG, "Bootstrap headroom %u bytes; free heap %u (minimum %u)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());

    if (core_err != ESP_OK) {
        for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    BaseType_t created = xTaskCreate(
        bootstrap_task,
        "waveshare_bootstrap",
        BOOTSTRAP_STACK_BYTES,
        NULL,
        10,
        NULL);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create Waveshare product bootstrap task");
    }
}
