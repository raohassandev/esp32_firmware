#include "inverter_manager.h"

#include <math.h>

/* inverter_manager.c is compiled with this public symbol renamed source-locally. */
esp_err_t inverter_manager_set_total_power_kw_core(float target_kw);

esp_err_t inverter_manager_set_total_power_kw(float target_kw)
{
    if (!isfinite(target_kw) || target_kw < 0.0f) return ESP_ERR_INVALID_ARG;

    /* A zero target is the fail-safe direction and must remain available when
     * identity/status evidence has gone stale or a source transition is in
     * progress. Positive production output is different: every enabled inverter
     * must have a fresh, mapped ON_GRID status sample before any command plan is
     * allowed to reach the core write/readback/rollback transaction.
     */
    if (target_kw > 0.0f && !inverter_manager_fleet_synchronised()) {
        return ESP_ERR_INVALID_STATE;
    }

    return inverter_manager_set_total_power_kw_core(target_kw);
}
