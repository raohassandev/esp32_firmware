#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_core.h"

static const char *TAG = "app_main";

static void app_bootstrap_task(void *argument)
{
    (void)argument;
    esp_err_t err = app_core_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Application initialization failed: %s (0x%x)", esp_err_to_name(err), err);
        ESP_LOGE(TAG, "Controller halted safely instead of entering a reboot loop");
        while (true) vTaskDelay(pdMS_TO_TICKS(10000));
    }

    ESP_LOGI(TAG, "PV-DG controller started");
    vTaskDelete(NULL);
}

void app_main(void)
{
    BaseType_t created = xTaskCreate(
        app_bootstrap_task,
        "app_bootstrap",
        12288,
        NULL,
        10,
        NULL
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create application bootstrap task");
    }
}
