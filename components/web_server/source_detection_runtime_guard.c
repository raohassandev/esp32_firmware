#include "config_manager.h"
#include "control_engine.h"
#include "source_detection_config.h"

#include <stdlib.h>

/* Source-detection settings can change the fallback source verdict consumed by
 * the control engine when strong commissioned contact evidence is unavailable.
 * Remove both persisted and live command authority before that model can change.
 * The control task, not this HTTP-side bridge, owns the physical safe-zero I/O. */
esp_err_t web_source_detection_config_save_guarded(
    const source_detection_config_t *source_config)
{
    if (!source_config) return ESP_ERR_INVALID_ARG;

    /* app_config_t is ~2.5 kB and this bridge is reached from the shared HTTP
     * server task. Keep the full snapshot off that task stack. */
    app_config_t *application = malloc(sizeof(*application));
    if (!application) return ESP_ERR_NO_MEM;

    esp_err_t error = config_manager_get_snapshot(application);
    if (error != ESP_OK) {
        free(application);
        return error;
    }

    application->control.enabled = false;
    error = config_manager_save(application);
    free(application);
    if (error != ESP_OK) return error;

    control_engine_force_disable();
    return source_detection_config_save(source_config);
}
