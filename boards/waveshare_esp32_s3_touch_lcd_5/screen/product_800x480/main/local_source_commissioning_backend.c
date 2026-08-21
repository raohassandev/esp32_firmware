#include "local_source_commissioning_backend.h"

#include <stdio.h>
#include <string.h>

#include "config_manager.h"
#include "control_engine.h"
#include "engineering_auth.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "solar_grid_config.h"

#define LOCAL_SOURCE_ENGINEERING_SESSION_MS (30ULL * 60ULL * 1000ULL)

static uint64_t s_unlocked_until_ms;
static bool s_setup_required;
static bool s_restart_required;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static bool unlocked(void)
{
    const uint64_t current = now_ms();
    if (s_unlocked_until_ms == 0U || current >= s_unlocked_until_ms) {
        s_unlocked_until_ms = 0U;
        return false;
    }
    return true;
}

static void result_set(source_commission_action_result_t *result,
                       bool ok, bool restart_required, const char *message)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->ok = ok;
    result->restart_required = restart_required;
    snprintf(result->message, sizeof(result->message), "%s", message ? message : "");
}

static bool require_unlocked(source_commission_action_result_t *result)
{
    if (unlocked()) {
        s_unlocked_until_ms = now_ms() + LOCAL_SOURCE_ENGINEERING_SESSION_MS;
        return true;
    }
    result_set(result, false, false, "Engineering unlock expired. Unlock source commissioning again.");
    return false;
}

static source_commission_auth_result_t local_unlock(void *context,
                                                    const char *credential,
                                                    uint32_t *retry_after_ms,
                                                    bool *setup_required)
{
    (void)context;
    const engineering_local_auth_result_t auth =
        engineering_auth_verify_local_credential(credential, retry_after_ms, &s_setup_required);
    if (setup_required) *setup_required = s_setup_required;
    if (auth == ENGINEERING_LOCAL_AUTH_OK) {
        s_unlocked_until_ms = now_ms() + LOCAL_SOURCE_ENGINEERING_SESSION_MS;
        return SOURCE_COMMISSION_AUTH_OK;
    }
    s_unlocked_until_ms = 0U;
    if (auth == ENGINEERING_LOCAL_AUTH_LOCKED) return SOURCE_COMMISSION_AUTH_LOCKED;
    if (auth == ENGINEERING_LOCAL_AUTH_DENIED) return SOURCE_COMMISSION_AUTH_DENIED;
    return SOURCE_COMMISSION_AUTH_ERROR;
}

static void local_lock(void *context)
{
    (void)context;
    s_unlocked_until_ms = 0U;
}

static void signal_to_screen(const solar_grid_signal_config_t *source,
                             source_commission_signal_t *target)
{
    if (!source || !target) return;
    target->meter_index = source->meter_index;
    target->function_code = source->function_code;
    target->address = source->address;
    target->mask = source->mask;
    target->active_value = source->active_value;
}

static void signal_to_core(const source_commission_signal_t *source,
                           bool enabled,
                           solar_grid_signal_config_t *target)
{
    if (!source || !target) return;
    target->enabled = enabled;
    target->meter_index = source->meter_index;
    target->function_code = source->function_code;
    target->address = source->address;
    target->mask = source->mask;
    target->active_value = source->active_value;
}

static bool local_read_config(void *context, source_commission_config_t *out)
{
    (void)context;
    if (!out || !unlocked()) return false;
    solar_grid_config_t solar = {0};
    if (solar_grid_config_get_snapshot(&solar) != ESP_OK) return false;

    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->unlocked = true;
    out->setup_required = s_setup_required;
    out->restart_required = s_restart_required;
    out->evidence_enabled = solar.grid_available.enabled && solar.grid_breaker_closed.enabled;
    signal_to_screen(&solar.grid_available, &out->grid_available);
    signal_to_screen(&solar.grid_breaker_closed, &out->grid_breaker_closed);
    out->evidence_poll_interval_ms = solar.evidence_poll_interval_ms;
    out->evidence_stale_timeout_ms = solar.evidence_stale_timeout_ms;
    out->grid_loss_trip_ms = solar.grid_loss_trip_ms;
    out->grid_recovery_stable_ms = solar.grid_recovery_stable_ms;
    s_unlocked_until_ms = now_ms() + LOCAL_SOURCE_ENGINEERING_SESSION_MS;
    return true;
}

static bool local_save_config(void *context,
                              const source_commission_config_t *source,
                              source_commission_action_result_t *result)
{
    (void)context;
    if (!source || !require_unlocked(result)) return false;

    solar_grid_config_t next = {0};
    app_config_t app = {0};
    if (solar_grid_config_get_snapshot(&next) != ESP_OK ||
        config_manager_get_snapshot(&app) != ESP_OK) {
        result_set(result, false, false, "Source or controller configuration is unavailable.");
        return false;
    }

    signal_to_core(&source->grid_available, source->evidence_enabled, &next.grid_available);
    signal_to_core(&source->grid_breaker_closed, source->evidence_enabled, &next.grid_breaker_closed);
    next.evidence_poll_interval_ms = source->evidence_poll_interval_ms;
    next.evidence_stale_timeout_ms = source->evidence_stale_timeout_ms;
    next.grid_loss_trip_ms = source->grid_loss_trip_ms;
    next.grid_recovery_stable_ms = source->grid_recovery_stable_ms;

    /* Shared Solar-Grid validation remains the authority. The local HMI does not
     * reinterpret signal completeness, timing or register bounds. */
    if (!solar_grid_config_valid(&next)) {
        result_set(result, false, false,
                   "Core rejected source evidence. Check meter slot, FC03/FC04, non-zero masks and timing bounds.");
        return false;
    }

    /* Commissioning changes may never land underneath live automatic control.
     * Disable the running loop before either persistent model is changed. */
    control_engine_force_disable();
    app.control.enabled = false;
    if (config_manager_save(&app) != ESP_OK) {
        result_set(result, false, false,
                   "Automatic control was forced disabled, but persistent disable failed. Source evidence was not saved.");
        return false;
    }
    if (solar_grid_config_save(&next) != ESP_OK) {
        s_restart_required = true;
        result_set(result, false, true,
                   "Control is disabled, but source evidence failed to persist. Review before restart.");
        return false;
    }

    s_restart_required = true;
    result_set(result, true, true,
               "Source evidence saved. Automatic control is disabled; restart before source qualification.");
    return true;
}

static bool local_restart_controller(void *context, source_commission_action_result_t *result)
{
    (void)context;
    if (!require_unlocked(result)) return false;
    result_set(result, true, false, "Controller restarting...");
    esp_restart();
    return true;
}

bool local_source_commissioning_backend_init(source_commission_backend_t *backend)
{
    if (!backend) return false;
    memset(backend, 0, sizeof(*backend));
    s_unlocked_until_ms = 0U;
    s_setup_required = false;
    s_restart_required = false;
    backend->unlock = local_unlock;
    backend->lock = local_lock;
    backend->read_config = local_read_config;
    backend->save_config = local_save_config;
    backend->restart_controller = local_restart_controller;
    return true;
}
