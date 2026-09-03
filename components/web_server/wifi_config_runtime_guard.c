#include "config_manager.h"
#include "control_engine.h"

/* Changing the controller network can invalidate the transport used for meter
 * and inverter communication. Persist that change only after automatic command
 * authority is removed. The control task owns the physical safe-zero write; the
 * HTTP path only latches the disable and persists control.enabled=false. */
esp_err_t web_wifi_config_manager_save_guarded(const app_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    app_config_t guarded = *config;
    guarded.control.enabled = false;
    control_engine_force_disable();
    return config_manager_save(&guarded);
}
