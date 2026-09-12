#include "local_network_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_manager.h"
#include "engineering_auth.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "network_manager.h"

static const char *TAG = "local_network_backend";

/* Reuses the same Engineering-credential gate as
 * local_source_commissioning_backend.c, with its own private unlock session
 * -- Wi-Fi provisioning and source-evidence commissioning are independent
 * Engineering actions and unlocking one must not silently unlock the other. */
#define LOCAL_NETWORK_ENGINEERING_SESSION_MS (30ULL * 60ULL * 1000ULL)

static uint64_t s_unlocked_until_ms;
static bool s_setup_required;

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
    s_unlocked_until_ms = current + LOCAL_NETWORK_ENGINEERING_SESSION_MS;
    return true;
}

static void result_set(network_screen_action_result_t *result,
                       bool ok, bool restart_required, const char *message)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->ok = ok;
    result->restart_required = restart_required;
    snprintf(result->message, sizeof(result->message), "%s", message ? message : "");
}

static bool require_unlocked(network_screen_action_result_t *result)
{
    if (unlocked()) return true;
    result_set(result, false, false, "Engineering unlock expired. Unlock network settings again.");
    return false;
}

static network_screen_auth_result_t local_unlock(void *context,
                                                  const char *credential,
                                                  uint32_t *retry_after_ms,
                                                  bool *setup_required)
{
    (void)context;
    (void)credential;
    /* BENCH CREDENTIAL BYPASS -- REMOVE BEFORE PRODUCTION RELEASE
     *
     * See the "Waveshare bench conveniences" Kconfig menu for the full
     * rationale. Any credential (even empty) unlocks Wi-Fi provisioning
     * while this is enabled; the check below never reaches
     * engineering_auth_verify_local_credential(). It logs on every call so
     * a build shipped with this on cannot pass unnoticed in its own logs. */
#if defined(CONFIG_WAVESHARE_BENCH_NETWORK_AUTH_BYPASS) && CONFIG_WAVESHARE_BENCH_NETWORK_AUTH_BYPASS
    ESP_LOGW(TAG, "BENCH BUILD: Network page Engineering unlock is BYPASSED "
                  "(CONFIG_WAVESHARE_BENCH_NETWORK_AUTH_BYPASS=y). This must not ship.");
    if (setup_required) *setup_required = false;
    s_unlocked_until_ms = now_ms() + LOCAL_NETWORK_ENGINEERING_SESSION_MS;
    return NETWORK_SCREEN_AUTH_OK;
#else
    const engineering_local_auth_result_t auth =
        engineering_auth_verify_local_credential(credential, retry_after_ms, &s_setup_required);
    if (setup_required) *setup_required = s_setup_required;
    if (auth == ENGINEERING_LOCAL_AUTH_OK) {
        s_unlocked_until_ms = now_ms() + LOCAL_NETWORK_ENGINEERING_SESSION_MS;
        return NETWORK_SCREEN_AUTH_OK;
    }
    s_unlocked_until_ms = 0U;
    if (auth == ENGINEERING_LOCAL_AUTH_LOCKED) return NETWORK_SCREEN_AUTH_LOCKED;
    if (auth == ENGINEERING_LOCAL_AUTH_DENIED) return NETWORK_SCREEN_AUTH_DENIED;
    return NETWORK_SCREEN_AUTH_ERROR;
#endif
}

static void local_lock(void *context)
{
    (void)context;
    s_unlocked_until_ms = 0U;
}

static bool local_read_status(void *context, network_screen_status_t *out)
{
    (void)context;
    if (!out || !unlocked()) return false;
    network_status_t status = {0};
    network_manager_get_status(&status);
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->network_online = status.network_ready;
    out->fallback_ap_active = status.fallback_ap_active;
    strlcpy(out->ssid, status.ssid, sizeof(out->ssid));
    strlcpy(out->ip, status.ip, sizeof(out->ip));
    out->rssi = status.rssi;
    return true;
}

static bool local_request_scan(void *context)
{
    (void)context;
    if (!unlocked()) return false;
    return network_manager_request_scan() == ESP_OK;
}

static bool local_read_scan(void *context, network_screen_scan_t *out)
{
    (void)context;
    if (!out || !unlocked()) return false;
    network_scan_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) return false;
    network_manager_get_scan_snapshot(snapshot);

    memset(out, 0, sizeof(*out));
    out->scanning = (snapshot->state == NETWORK_SCAN_RUNNING);
    uint16_t count = snapshot->count;
    if (count > NETWORK_SCREEN_MAX_RESULTS) count = NETWORK_SCREEN_MAX_RESULTS;
    out->count = count;
    for (uint16_t i = 0U; i < count; ++i) {
        const network_scan_ap_t *ap = &snapshot->results[i];
        strlcpy(out->items[i].ssid, ap->ssid, sizeof(out->items[i].ssid));
        out->items[i].rssi = ap->rssi;
        out->items[i].secured = ap->auth_mode != 0U; /* 0 == Open, see scan_auth_name() */
        out->items[i].configured = ap->configured_primary || ap->configured_fallback;
        out->items[i].connected = ap->connected;
    }
    free(snapshot);
    return true;
}

static bool local_connect(void *context, const char *ssid, const char *password,
                          network_screen_action_result_t *result)
{
    (void)context;
    if (!ssid || !ssid[0] || !require_unlocked(result)) return false;

    const size_t password_length = password ? strlen(password) : 0U;
    if (password_length > 0U && password_length < 8U) {
        result_set(result, false, false,
                  "Wi-Fi password must contain 8-64 characters. Leave it blank only for an "
                  "open (unsecured) network.");
        return false;
    }
    if (password_length > 64U) {
        result_set(result, false, false, "Wi-Fi password must be 64 characters or fewer.");
        return false;
    }

    app_config_t *config = malloc(sizeof(*config));
    if (!config) {
        result_set(result, false, false, "Out of memory.");
        return false;
    }
    if (config_manager_get_snapshot(config) != ESP_OK) {
        free(config);
        result_set(result, false, false, "Current configuration is unavailable.");
        return false;
    }

    strlcpy(config->wifi.primary.ssid, ssid, sizeof(config->wifi.primary.ssid));
    strlcpy(config->wifi.primary.password, password ? password : "",
            sizeof(config->wifi.primary.password));
    config->wifi.primary.enabled = true;

    const esp_err_t err = config_manager_save(config);
    free(config);
    if (err != ESP_OK) {
        result_set(result, false, false, "Wi-Fi configuration could not be persisted.");
        return false;
    }

    result_set(result, true, true,
              "Wi-Fi saved. Tap Restart to apply and join the new network.");
    return true;
}

static bool local_restart_controller(void *context, network_screen_action_result_t *result)
{
    (void)context;
    if (!require_unlocked(result)) return false;
    result_set(result, true, false, "Restarting...");
    esp_restart();
    return true;
}

bool local_network_backend_init(network_screen_backend_t *backend)
{
    if (!backend) return false;
    memset(backend, 0, sizeof(*backend));
    s_unlocked_until_ms = 0U;
    s_setup_required = false;
#if defined(CONFIG_WAVESHARE_BENCH_NETWORK_AUTH_BYPASS) && CONFIG_WAVESHARE_BENCH_NETWORK_AUTH_BYPASS
    ESP_LOGW(TAG, "BENCH BUILD: this image was built with "
                  "CONFIG_WAVESHARE_BENCH_NETWORK_AUTH_BYPASS=y -- the Network page's "
                  "Engineering unlock accepts ANY credential and is prefilled. This must not ship.");
    /* Any placeholder works -- local_unlock() ignores the value entirely
     * under this Kconfig option. It only needs to be non-empty so the
     * field is not blank and render_locked() knows to show the bench
     * banner. */
    snprintf(backend->bench_default_credential, sizeof(backend->bench_default_credential),
            "bench-bypass");
#endif
    backend->unlock = local_unlock;
    backend->lock = local_lock;
    backend->read_status = local_read_status;
    backend->request_scan = local_request_scan;
    backend->read_scan = local_read_scan;
    backend->connect = local_connect;
    backend->restart_controller = local_restart_controller;
    return true;
}
