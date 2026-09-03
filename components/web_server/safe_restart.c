#include "safe_restart.h"

#include <math.h>
#include <stdint.h>

#include "control_engine.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAFE_RESTART_POLL_MS 100U
#define SAFE_RESTART_TIMEOUT_MS 30000U
#define SAFE_RESTART_SETTLE_MS 200U

static const char *TAG = "safe_restart";

void web_safe_restart(void)
{
    /* A restart must never leave the inverter fleet holding the last positive
     * command merely because the controller disappeared. The control task owns
     * the physical zero write/readback path; this helper only revokes authority
     * and waits for its confirmed applied target to reach zero. */
    control_engine_force_disable();

    const int64_t deadline_us =
        esp_timer_get_time() + (int64_t)SAFE_RESTART_TIMEOUT_MS * 1000LL;
    while (esp_timer_get_time() < deadline_us) {
        control_status_t status = {0};
        control_engine_get_status(&status);
        if (!status.enabled && isfinite(status.applied_pv_kw) &&
            status.applied_pv_kw <= 0.0f) {
            vTaskDelay(pdMS_TO_TICKS(SAFE_RESTART_SETTLE_MS));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(SAFE_RESTART_POLL_MS));
    }

    /* Fail closed: if safe zero cannot be confirmed, keep the running controller
     * disabled and abort the restart instead of rebooting while a non-zero fleet
     * command may still be active. */
    ESP_LOGE(TAG, "Restart aborted: safe-zero command was not confirmed");
    vTaskDelete(NULL);
}
