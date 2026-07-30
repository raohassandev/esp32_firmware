#include "safety_manager.h"
#include <stdlib.h>
#include "esp_check.h"
#include "config_manager.h"

static uint32_t s_alarm_flags;
static control_config_t s_control;

esp_err_t safety_manager_init(void)
{
    app_config_t *cfg = malloc(sizeof(*cfg));
    if (!cfg) return ESP_ERR_NO_MEM;
    esp_err_t err = config_manager_get_snapshot(cfg);
    if (err == ESP_OK) {
        s_control = cfg->control;
        s_alarm_flags = 0;
    }
    free(cfg);
    return err;
}

float safety_manager_limit_target_kw(float requested_kw, const meter_data_t *meter, uint32_t now_ms)
{
    s_alarm_flags = 0;
    if (!meter || !meter->online) s_alarm_flags |= SAFETY_ALARM_METER_OFFLINE;
    if (!meter || !meter->last_update_ms || now_ms - meter->last_update_ms > s_control.meter_stale_timeout_ms) {
        s_alarm_flags |= SAFETY_ALARM_METER_STALE;
    }
    return s_alarm_flags ? 0.0f : requested_kw;
}

/* Floor used before init, and if a configuration ever carries zero. Zero would
 * mean "every sample is stale", which would inhibit control permanently. */
#define SAFETY_MIN_STALE_TIMEOUT_MS 100U

uint32_t safety_manager_meter_stale_timeout_ms(void)
{
    const uint32_t configured = s_control.meter_stale_timeout_ms;
    return configured >= SAFETY_MIN_STALE_TIMEOUT_MS ? configured
                                                     : SAFETY_MIN_STALE_TIMEOUT_MS;
}

uint32_t safety_manager_get_alarm_flags(void)
{
    return s_alarm_flags;
}
