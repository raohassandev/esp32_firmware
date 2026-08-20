#include "local_backend_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "screen_api.h"
#include "sdkconfig.h"

#ifndef CONFIG_LWIP_NETIF_LOOPBACK
#error "Waveshare product screen self-API provider requires CONFIG_LWIP_NETIF_LOOPBACK"
#endif

#define LOCAL_API_TIMEOUT_MS 1500
#define LOCAL_API_URL_MAX 96
#define LOCAL_API_HOST_MAX 16

typedef struct {
    const char *path;
    size_t capacity;
    char *json;
    bool valid;
    uint32_t consecutive_failures;
} local_api_slot_t;

/* Capacities are bounded deliberately. The screen parser itself is bounded, and
 * an unexpectedly huge response is treated as unavailable instead of consuming
 * unbounded controller memory. Buffers live in PSRAM on this N16R8 board. */
static local_api_slot_t s_slots[] = {
    {SCREEN_API_LIVE_PATH,       4096U,  NULL, false, 0U},
    {SCREEN_API_STATUS_PATH,    16384U,  NULL, false, 0U},
    {SCREEN_API_METERS_PATH,    32768U,  NULL, false, 0U},
    {SCREEN_API_INVERTERS_PATH, 49152U,  NULL, false, 0U},
    {SCREEN_API_TELEMETRY_PATH, 16384U,  NULL, false, 0U},
    {SCREEN_API_EVENTS_PATH,    49152U,  NULL, false, 0U},
    {SCREEN_API_ALARMS_PATH,    49152U,  NULL, false, 0U},
};

static const char *TAG = "screen_backend";
static char s_last_target[LOCAL_API_HOST_MAX];
static bool s_logged_first_success;
static bool s_warned_no_target;

static local_api_slot_t *slot_for(const char *path)
{
    if (!path) return NULL;
    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        if (strcmp(path, s_slots[i].path) == 0) return &s_slots[i];
    }
    return NULL;
}

static bool host_from_netif(const char *if_key, char *host, size_t capacity)
{
    if (!if_key || !host || capacity == 0U) return false;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(if_key);
    if (!netif || !esp_netif_is_netif_up(netif)) return false;

    esp_netif_ip_info_t info = {0};
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0U) return false;
    return esp_ip4addr_ntoa(&info.ip, host, (int)capacity) != NULL;
}

/* CONFIG_LWIP_NETIF_LOOPBACK is per-interface loopback: packets addressed to a
 * netif's OWN IPv4 address are delivered back through that netif. Use the
 * always-present recovery AP first because it gives the native screen a stable
 * self-address even while STA association changes. STA is a safe fallback if
 * AP is unavailable. */
static bool resolve_self_host(char *host, size_t capacity)
{
    if (host_from_netif("WIFI_AP_DEF", host, capacity)) return true;
    if (host_from_netif("WIFI_STA_DEF", host, capacity)) return true;
    if (host && capacity > 0U) host[0] = '\0';
    return false;
}

static void note_failure(local_api_slot_t *slot, const char *reason)
{
    if (!slot) return;
    slot->consecutive_failures++;
    if (slot->consecutive_failures == 1U || (slot->consecutive_failures % 20U) == 0U) {
        ESP_LOGW(TAG, "%s unavailable (%s), consecutive failures=%u",
                 slot->path,
                 reason ? reason : "unknown",
                 (unsigned)slot->consecutive_failures);
    }
}

static bool provider_acquire(void *context, const char *path, const char **json)
{
    (void)context;
    if (!json) return false;
    *json = NULL;
    local_api_slot_t *slot = slot_for(path);
    if (!slot || !slot->valid || !slot->json) return false;
    *json = slot->json;
    return true;
}

static void provider_release(void *context, const char *path, const char *json)
{
    (void)context;
    (void)path;
    (void)json;
    /* Slot storage is persistent and owned by this adapter. */
}

bool local_backend_provider_init(screen_api_provider_t *provider)
{
    if (!provider) return false;

    s_last_target[0] = '\0';
    s_logged_first_success = false;
    s_warned_no_target = false;

    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        local_api_slot_t *slot = &s_slots[i];
        slot->valid = false;
        slot->consecutive_failures = 0U;
        if (slot->json) continue;
        slot->json = heap_caps_malloc(slot->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!slot->json) slot->json = malloc(slot->capacity);
        if (!slot->json) {
            ESP_LOGE(TAG, "Unable to allocate %u bytes for %s",
                     (unsigned)slot->capacity, slot->path);
            local_backend_provider_deinit();
            return false;
        }
        slot->json[0] = '\0';
    }

    provider->context = NULL;
    provider->acquire = provider_acquire;
    provider->release = provider_release;
    ESP_LOGI(TAG, "Read-only self-API provider ready; lwIP per-interface loopback enabled");
    return true;
}

bool local_backend_provider_fetch(const char *path)
{
    local_api_slot_t *slot = slot_for(path);
    if (!slot || !slot->json) return false;
    slot->valid = false;
    slot->json[0] = '\0';

    char host[LOCAL_API_HOST_MAX];
    if (!resolve_self_host(host, sizeof(host))) {
        if (!s_warned_no_target) {
            ESP_LOGW(TAG, "No active AP/STA own-IP loopback target yet; screen backend remains unavailable");
            s_warned_no_target = true;
        }
        note_failure(slot, "no active self IPv4 target");
        return false;
    }
    s_warned_no_target = false;

    if (strcmp(s_last_target, host) != 0) {
        snprintf(s_last_target, sizeof(s_last_target), "%s", host);
        ESP_LOGI(TAG, "Using controller own-IP loopback target http://%s", s_last_target);
    }

    char url[LOCAL_API_URL_MAX];
    int written = snprintf(url, sizeof(url), "http://%s%s", host, path);
    if (written <= 0 || (size_t)written >= sizeof(url)) {
        note_failure(slot, "self URL too long");
        return false;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = LOCAL_API_TIMEOUT_MS,
        .keep_alive_enable = false,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        note_failure(slot, "HTTP client init failed");
        return false;
    }

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        char reason[64];
        snprintf(reason, sizeof(reason), "open failed: %s", esp_err_to_name(err));
        note_failure(slot, reason);
        goto done;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        char reason[48];
        snprintf(reason, sizeof(reason), "HTTP %d", status);
        note_failure(slot, reason);
        goto close_client;
    }

    size_t total = 0U;
    while (total < slot->capacity - 1U) {
        int count = esp_http_client_read(client,
                                         slot->json + total,
                                         (int)(slot->capacity - 1U - total));
        if (count < 0) {
            note_failure(slot, "read failed");
            goto close_client;
        }
        if (count == 0) break;
        total += (size_t)count;
    }
    slot->json[total] = '\0';

    if (!esp_http_client_is_complete_data_received(client)) {
        note_failure(slot, "response incomplete or over bounded capacity");
        goto close_client;
    }

    slot->valid = true;
    slot->consecutive_failures = 0U;
    ok = true;
    if (!s_logged_first_success) {
        ESP_LOGI(TAG, "Existing Core API reachable through per-interface loopback; screen data path online");
        s_logged_first_success = true;
    }

close_client:
    (void)esp_http_client_close(client);
done:
    esp_http_client_cleanup(client);
    return ok;
}

void local_backend_provider_deinit(void)
{
    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        free(s_slots[i].json);
        s_slots[i].json = NULL;
        s_slots[i].valid = false;
        s_slots[i].consecutive_failures = 0U;
    }
    s_last_target[0] = '\0';
    s_logged_first_success = false;
    s_warned_no_target = false;
}
