#include "safety_manager.h"
#include <stdlib.h>
#include "esp_check.h"
#include "config_manager.h"
#include "freertos/FreeRTOS.h"

static uint32_t s_alarm_flags;
static control_config_t s_control;
static portMUX_TYPE s_alarm_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t safety_manager_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err == ESP_OK) {
        s_control = cfg->control;
        portENTER_CRITICAL(&s_alarm_lock);
        s_alarm_flags = 0;
        portEXIT_CRITICAL(&s_alarm_lock);
    }
    free(cfg);
    return err;
}

float safety_manager_limit_target_kw(float requested_kw, const meter_data_t *meter, uint32_t now_ms)
{
    /* Build one complete immutable alarm snapshot locally, then publish it in a
     * single critical section. The previous implementation cleared the shared
     * flags before repopulating them, so a concurrent HTTP/status reader could
     * briefly observe "no alarm" during an active fail-safe condition. */
    uint32_t alarm_flags = 0;
    if (!meter || !meter->online) alarm_flags |= SAFETY_ALARM_METER_OFFLINE;
    if (!meter || !meter->last_update_ms || now_ms - meter->last_update_ms > s_control.meter_stale_timeout_ms) {
        alarm_flags |= SAFETY_ALARM_METER_STALE;
    }

    portENTER_CRITICAL(&s_alarm_lock);
    s_alarm_flags = alarm_flags;
    portEXIT_CRITICAL(&s_alarm_lock);
    return alarm_flags ? 0.0f : requested_kw;
}

uint32_t safety_manager_get_alarm_flags(void)
{
    portENTER_CRITICAL(&s_alarm_lock);
    uint32_t alarm_flags = s_alarm_flags;
    portEXIT_CRITICAL(&s_alarm_lock);
    return alarm_flags;
}
