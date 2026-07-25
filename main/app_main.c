#include "esp_err.h"
#include "esp_log.h"
#include "app_core.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_ERROR_CHECK(app_core_init());
    ESP_LOGI(TAG, "PV-DG controller started");
}
