#include "app_core.h"
#include "esp_check.h"
#include "esp_log.h"
#include "config_manager.h"
#include "solar_grid_config.h"
#include "source_detection.h"
#include "source_detection_config.h"
#include "ota_manager.h"
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
    ESP_RETURN_ON_ERROR(solar_grid_config_init(), TAG, "Solar-Grid configuration init failed");
    /* OTA state must be known before mandatory network initialization. A newly
     * booted candidate that fails a mandatory startup stage must remain eligible
     * for rollback rather than being accepted as a degraded recovery boot. */
    ESP_RETURN_ON_ERROR(ota_manager_init(), TAG, "OTA manager init failed");

    ESP_LOGI(TAG, "Initializing network");
    ESP_RETURN_ON_ERROR(network_manager_init(), TAG, "network init failed");

    bool all_ok = true;
    bool source_config_ok = init_optional(source_detection_config_init(),
                                          "source detection configuration");
    all_ok &= source_config_ok;
    all_ok &= init_optional(profile_manager_init(), "profile manager");
    bool meter_ok = init_optional(meter_manager_init(), "meter manager");
    all_ok &= meter_ok;
    all_ok &= init_optional(inverter_manager_init(), "inverter manager");
    all_ok &= init_optional(safety_manager_init(), "safety manager");
    all_ok &= init_optional(control_engine_init(), "control engine");
    if (source_config_ok && meter_ok) {
        all_ok &= init_optional(source_detection_init(), "EM500 source detection");
    } else {
        ESP_LOGW(TAG, "EM500 source detection remains unavailable because its configuration or meter manager did not initialize");
    }
    all_ok &= init_optional(web_server_start(), "web server");

    if (!all_ok) {
        if (ota_manager_running_pending_verify()) {
            ESP_LOGE(TAG, "A first OTA boot has degraded subsystems; rollback is required");
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "Startup completed with degraded subsystems; device remains reachable for recovery");
        return ESP_OK;
    }

    /* Keep a staged image rollback-eligible through a real stabilization window
     * after all runtime subsystems and the recovery web path are initialized. */
    ESP_RETURN_ON_ERROR(ota_manager_schedule_boot_validation(30000U),
                        TAG, "OTA first-boot validation scheduling failed");
    return ESP_OK;
}
