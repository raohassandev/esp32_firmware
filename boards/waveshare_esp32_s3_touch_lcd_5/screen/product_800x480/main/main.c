#include <stdbool.h>
#include <stdint.h>

#include "app_core.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_manager.h"
#include "waveshare_display_port.h"
#include "waveshare_display_profile.h"

#define BOOTSTRAP_STACK_BYTES 12288U
#define PRODUCT_RGB_BOUNCE_LINES 10U

static const char *TAG = "waveshare_gate_a";
static waveshare_display_port_handles_t s_display;
static bool s_display_reserved;

static void log_dma_headroom(const char *stage)
{
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI(TAG, "%s: internal DMA free=%u largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(caps),
             (unsigned)heap_caps_get_largest_free_block(caps));
}

static esp_err_t reserve_display_hardware(void)
{
    const waveshare_display_profile_t *profile =
        waveshare_display_profile(WAVESHARE_DISPLAY_800X480);
    if (!profile || profile->width != 800U || profile->height != 480U) {
        return ESP_ERR_INVALID_STATE;
    }

    const waveshare_display_port_config_t config = {
        .profile = profile,
        .i2c_bus = NULL,
        .tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .enable_touch = true,
        .bounce_buffer_lines = PRODUCT_RGB_BOUNCE_LINES,
        .allow_no_bounce_fallback = true,
    };

    log_dma_headroom("Before LCD/touch reservation");
    esp_err_t err = waveshare_display_port_init(&config, &s_display);
    if (err == ESP_OK) {
        s_display_reserved = true;
        log_dma_headroom("After LCD/touch reservation");
    }
    return err;
}

static void app_bootstrap_task(void *argument)
{
    (void)argument;
    esp_err_t err = app_core_init();

    ESP_LOGI(TAG, "Bootstrap stack headroom: %u bytes of %u; free heap %u bytes (min %u)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)BOOTSTRAP_STACK_BYTES,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s (0x%x)",
                 esp_err_to_name(err), err);
        if (ota_manager_running_pending_verify()) {
            esp_err_t rollback = ota_manager_rollback_pending_and_reboot();
            ESP_LOGE(TAG, "OTA rollback could not be started: %s (0x%x)",
                     esp_err_to_name(rollback), rollback);
        }
        ESP_LOGE(TAG, "Controller halted safely instead of entering a reboot loop");
        while (true) vTaskDelay(pdMS_TO_TICKS(10000));
    }

    ESP_LOGI(TAG,
             "Current dev Product Core started; display_reserved=%s. Gate A imports no legacy UI/backend logic.",
             s_display_reserved ? "true" : "false");
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t display_err = reserve_display_hardware();
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Waveshare LCD/touch reservation failed: %s; Product Core continues headless",
                 esp_err_to_name(display_err));
    }

    BaseType_t created = xTaskCreate(
        app_bootstrap_task,
        "app_bootstrap",
        BOOTSTRAP_STACK_BYTES,
        NULL,
        10,
        NULL);

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create application bootstrap task");
        if (s_display_reserved) {
            (void)waveshare_display_port_deinit(&s_display);
            s_display_reserved = false;
        }
    }
}
