#include "local_backend_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "screen_api.h"

#define LOCAL_API_TIMEOUT_MS 1500
#define LOCAL_API_URL_MAX 96

typedef struct {
    const char *path;
    size_t capacity;
    char *json;
    bool valid;
} local_api_slot_t;

/* Capacities are bounded deliberately. The screen parser itself is bounded, and
 * an unexpectedly huge response is treated as unavailable instead of consuming
 * unbounded controller memory. Buffers live in PSRAM on this N16R8 board. */
static local_api_slot_t s_slots[] = {
    {SCREEN_API_LIVE_PATH,       4096U,  NULL, false},
    {SCREEN_API_STATUS_PATH,    16384U,  NULL, false},
    {SCREEN_API_METERS_PATH,    32768U,  NULL, false},
    {SCREEN_API_INVERTERS_PATH, 49152U,  NULL, false},
    {SCREEN_API_TELEMETRY_PATH, 16384U,  NULL, false},
    {SCREEN_API_EVENTS_PATH,    49152U,  NULL, false},
    {SCREEN_API_ALARMS_PATH,    49152U,  NULL, false},
};

static const char *TAG = "screen_backend";

static local_api_slot_t *slot_for(const char *path)
{
    if (!path) return NULL;
    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        if (strcmp(path, s_slots[i].path) == 0) return &s_slots[i];
    }
    return NULL;
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

    for (size_t i = 0; i < sizeof(s_slots) / sizeof(s_slots[0]); ++i) {
        local_api_slot_t *slot = &s_slots[i];
        slot->valid = false;
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
    ESP_LOGI(TAG, "Loopback read-only API provider ready");
    return true;
}

bool local_backend_provider_fetch(const char *path)
{
    local_api_slot_t *slot = slot_for(path);
    if (!slot || !slot->json) return false;
    slot->valid = false;
    slot->json[0] = '\0';

    char url[LOCAL_API_URL_MAX];
    int written = snprintf(url, sizeof(url), "http://127.0.0.1%s", path);
    if (written <= 0 || (size_t)written >= sizeof(url)) return false;

    const esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = LOCAL_API_TIMEOUT_MS,
        .keep_alive_enable = false,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s open failed: %s", path, esp_err_to_name(err));
        goto done;
    }

    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "%s returned HTTP %d", path, status);
        goto close_client;
    }

    size_t total = 0U;
    while (total < slot->capacity - 1U) {
        int count = esp_http_client_read(client,
                                         slot->json + total,
                                         (int)(slot->capacity - 1U - total));
        if (count < 0) {
            ESP_LOGW(TAG, "%s read failed", path);
            goto close_client;
        }
        if (count == 0) break;
        total += (size_t)count;
    }
    slot->json[total] = '\0';

    if (!esp_http_client_is_complete_data_received(client)) {
        ESP_LOGW(TAG, "%s response incomplete or exceeds %u bytes",
                 path, (unsigned)(slot->capacity - 1U));
        goto close_client;
    }

    slot->valid = true;
    ok = true;

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
    }
}
