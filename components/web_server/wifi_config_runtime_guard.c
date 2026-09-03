#include "config_manager.h"
#include "control_engine.h"

#include <stdlib.h>

/* Changing the controller network can invalidate the transport used for meter
 * and inverter communication. Persist that change only after automatic command
 * authority is removed. The control task owns the physical safe-zero write; the
 * HTTP path only latches the disable and persists control.enabled=false. */
esp_err_t web_wifi_config_manager_save_guarded(const app_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    /* This wrapper is reached from the shared HTTP task. A whole app_config_t is
     * ~2.5 kB, so copy it on the heap rather than recreating the historical
     * task-stack overflow class. */
    app_config_t *guarded = malloc(sizeof(*guarded));
    if (!guarded) return ESP_ERR_NO_MEM;
    *guarded = *config;
    guarded->control.enabled = false;

    /* Preserve the existing safety order: live authority is revoked before the
     * transport-changing configuration is persisted. */
    control_engine_force_disable();
    esp_err_t error = config_manager_save(guarded);
    free(guarded);
    return error;
}
