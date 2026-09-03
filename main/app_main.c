#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_core.h"
#include "ota_manager.h"

static const char *TAG = "app_main";

/* ESP-IDF sizes task stacks in bytes (StackType_t is uint8_t). */
#define BOOTSTRAP_STACK_BYTES 12288

static void app_bootstrap_task(void *argument)
{
    (void)argument;
    esp_err_t err = app_core_init();

    /* Report the worst-case headroom of this task: initialization performs the
     * deepest calls in the system (NVS, Wi-Fi, TLS-free HTTP server), so this
     * number is the evidence that the stack is correctly sized. */
    ESP_LOGI(TAG, "Bootstrap stack headroom: %u bytes of %u; free heap %u bytes (min %u)",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)BOOTSTRAP_STACK_BYTES,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s (0x%x)", esp_err_to_name(err), err);
        if (ota_manager_running_pending_verify()) {
            esp_err_t rollback = ota_manager_rollback_pending_and_reboot();
            ESP_LOGE(TAG, "OTA rollback could not be started: %s (0x%x)",
                     esp_err_to_name(rollback), rollback);
        }
        ESP_LOGE(TAG, "Controller halted safely instead of entering a reboot loop");
        while (true) vTaskDelay(pdMS_TO_TICKS(10000));
    }

    ESP_LOGI(TAG, "PV-DG controller started");
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* All initialization runs on this dedicated task rather than the 3584-byte
     * main task, which is far too small for the NVS + Wi-Fi + HTTP init path. */
    BaseType_t created = xTaskCreate(
        app_bootstrap_task,
        "app_bootstrap",
        BOOTSTRAP_STACK_BYTES,
        NULL,
        10,
        NULL
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create application bootstrap task");
    }
}
