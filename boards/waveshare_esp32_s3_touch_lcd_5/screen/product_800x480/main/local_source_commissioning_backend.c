#include "local_source_commissioning_backend.h"

#include <stdio.h>
#include <string.h>

/*
 * Source commissioning shares the same security boundary as the main
 * touchscreen commissioning flow. The current core has no local credential
 * verifier API, so all local writes remain fail-closed rather than introducing
 * a development password or bypass.
 */

static void result_locked(source_commission_action_result_t *result)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->ok = false;
    result->restart_required = false;
    snprintf(result->message, sizeof(result->message),
             "Source commissioning is locked in this build. Use the authenticated Engineering interface.");
}

static source_commission_auth_result_t local_unlock(void *context,
                                                     const char *credential,
                                                     uint32_t *retry_after_ms,
                                                     bool *setup_required)
{
    (void)context;
    (void)credential;
    if (retry_after_ms) *retry_after_ms = 0U;
    if (setup_required) *setup_required = false;
    return SOURCE_COMMISSION_AUTH_ERROR;
}

static void local_lock(void *context)
{
    (void)context;
}

static bool local_read_config(void *context, source_commission_config_t *out)
{
    (void)context;
    if (out) memset(out, 0, sizeof(*out));
    return false;
}

static bool local_save_config(void *context,
                              const source_commission_config_t *source,
                              source_commission_action_result_t *result)
{
    (void)context;
    (void)source;
    result_locked(result);
    return false;
}

static bool local_restart_controller(void *context,
                                     source_commission_action_result_t *result)
{
    (void)context;
    result_locked(result);
    return false;
}

bool local_source_commissioning_backend_init(source_commission_backend_t *backend)
{
    if (!backend) return false;
    memset(backend, 0, sizeof(*backend));
    backend->unlock = local_unlock;
    backend->lock = local_lock;
    backend->read_config = local_read_config;
    backend->save_config = local_save_config;
    backend->restart_controller = local_restart_controller;
    return true;
}
