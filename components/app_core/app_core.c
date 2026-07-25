#include "app_core.h"
#include "esp_check.h"
#include "esp_log.h"
#include "config_manager.h"
#include "network_manager.h"
#include "profile_manager.h"
#include "meter_manager.h"
#include "inverter_manager.h"
#include "safety_manager.h"
#include "control_engine.h"
#include "web_server.h"

static const char *TAG = "app_core";

esp_err_t app_core_init(void)
{
    ESP_LOGI(TAG, "Initializing configuration");
    ESP_RETURN_ON_ERROR(config_manager_init(), TAG, "configuration init failed");

    ESP_LOGI(TAG, "Initializing network");
    ESP_RETURN_ON_ERROR(network_manager_init(), TAG, "network init failed");

    ESP_RETURN_ON_ERROR(profile_manager_init(), TAG, "profile init failed");
    ESP_RETURN_ON_ERROR(meter_manager_init(), TAG, "meter init failed");
    ESP_RETURN_ON_ERROR(inverter_manager_init(), TAG, "inverter init failed");
    ESP_RETURN_ON_ERROR(safety_manager_init(), TAG, "safety init failed");
    ESP_RETURN_ON_ERROR(control_engine_init(), TAG, "control init failed");
    ESP_RETURN_ON_ERROR(web_server_start(), TAG, "web server init failed");

    return ESP_OK;
}
