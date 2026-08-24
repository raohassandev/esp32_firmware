#include "esp_log.h"
#include "esp_lv_adapter.h"

static const char *TAG = "screen_qual";

void app_main(void)
{
    /* Compile-time dependency/API qualification only. Hardware creation is
     * deliberately not attempted in this standalone project because no exact
     * physical SKU/revision is attached to CI. */
    esp_lv_adapter_config_t config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    (void)config;
    ESP_LOGI(TAG, "Waveshare screen software/dependency qualification image");
}
