#include "local_commissioning_backend.h"

#include <stdio.h>
#include <string.h>

/*
 * The current Product Core exposes Engineering authentication through the
 * guarded HTTP/session boundary only. It does not yet expose a local credential
 * verification API for the touchscreen. Never invent a local password or bypass
 * that boundary: keep all touchscreen commissioning writes fail-closed until a
 * shared local verifier is added to the core.
 *
 * Runtime/operator pages are independent of this backend and remain available.
 */

static void result_locked(screen_commission_action_result_t *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->ok = false;
    result->restart_required = false;
    snprintf(result->message, sizeof(result->message),
             "Engineering commissioning is locked in this build. Use the authenticated Engineering interface.");
}

static screen_commission_auth_result_t local_unlock(void *context,
                                                     const char *credential,
                                                     uint32_t *retry_after_ms,
                                                     bool *setup_required)
{
    (void)context;
    (void)credential;
    if (retry_after_ms) *retry_after_ms = 0U;
    if (setup_required) *setup_required = false;
    return SCREEN_COMMISSION_AUTH_ERROR;
}

static void local_lock(void *context)
{
    (void)context;
}

static bool local_read_config(void *context, screen_commissioning_config_t *out)
{
    (void)context;
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

static bool local_save_site(void *context,
                            const char *device_name,
                            screen_commission_action_result_t *result)
{
    (void)context;
    (void)device_name;
    result_locked(result);
    return false;
}

static bool local_save_meter(void *context,
                             uint8_t index,
                             const screen_commission_meter_t *meter,
                             screen_commission_action_result_t *result)
{
    (void)context;
    (void)index;
    (void)meter;
    result_locked(result);
    return false;
}

static bool local_save_inverter(void *context,
                                uint8_t index,
                                const screen_commission_inverter_t *inverter,
                                screen_commission_action_result_t *result)
{
    (void)context;
    (void)index;
    (void)inverter;
    result_locked(result);
    return false;
}

static bool local_save_plant(void *context,
                             const screen_commission_plant_t *plant,
                             screen_commission_action_result_t *result)
{
    (void)context;
    (void)plant;
    result_locked(result);
    return false;
}

static bool local_set_control_enabled(void *context,
                                      bool enabled,
                                      screen_commission_action_result_t *result)
{
    (void)context;
    (void)enabled;
    result_locked(result);
    return false;
}

static bool local_restart_controller(void *context,
                                     screen_commission_action_result_t *result)
{
    (void)context;
    result_locked(result);
    return false;
}

bool local_commissioning_backend_init(screen_commissioning_backend_t *backend)
{
    if (!backend) return false;
    memset(backend, 0, sizeof(*backend));
    backend->unlock = local_unlock;
    backend->lock = local_lock;
    backend->read_config = local_read_config;
    backend->save_site = local_save_site;
    backend->save_meter = local_save_meter;
    backend->save_inverter = local_save_inverter;
    backend->save_plant = local_save_plant;
    backend->set_control_enabled = local_set_control_enabled;
    backend->restart_controller = local_restart_controller;
    return true;
}
