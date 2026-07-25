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

/* Subsystems that may fail without making the device unreachable: log and
 * keep going so the web UI / recovery AP stays available for repair. */
static bool init_optional(esp_err_t err, const char *name)
{
    if (err == ESP_OK) return true;
    ESP_LOGE(TAG, "%s init failed: %s (0x%x); continuing without it", name, esp_err_to_name(err), err);
    return false;
}

esp_err_t app_core_init(void)
{
    ESP_LOGI(TAG, "Initializing configuration");
    ESP_RETURN_ON_ERROR(config_manager_init(), TAG, "configuration init failed");

    ESP_LOGI(TAG, "Initializing network");
    ESP_RETURN_ON_ERROR(network_manager_init(), TAG, "network init failed");

    bool all_ok = true;
    all_ok &= init_optional(profile_manager_init(), "profile manager");
    all_ok &= init_optional(meter_manager_init(), "meter manager");
    all_ok &= init_optional(inverter_manager_init(), "inverter manager");
    all_ok &= init_optional(safety_manager_init(), "safety manager");
    all_ok &= init_optional(control_engine_init(), "control engine");
    all_ok &= init_optional(web_server_start(), "web server");

    if (!all_ok) {
        ESP_LOGW(TAG, "Startup completed with degraded subsystems; device remains reachable for recovery");
    }
    return ESP_OK;
}
