/* The forced redirect header is injected before every source in this component.
 * Undo its three aliases in this implementation so the worker can call the real
 * shared-Core persistence APIs after it has moved execution to internal DRAM. */
#undef config_manager_save
#undef solar_grid_config_save
#undef inverter_profile_store_set

#include "local_persistence_redirect.h"

/* local_persistence_redirect.h is pragma-once and was already force-included, so
 * its aliases are not recreated by the include above. Keep the undef directives
 * here deliberately; they document the only file allowed to cross the redirect. */
#undef config_manager_save
#undef solar_grid_config_save
#undef inverter_profile_store_set

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* config_manager_save() performs a byte-for-byte read-back after NVS commit and
 * allocates a second app_config_t verification buffer. The previous 8 kB worker
 * stack, plus the ~2.6 kB internal payload copy, exhausted/fragmented the scarce
 * internal DRAM left after RGB DMA and Core startup, so that verification malloc
 * returned ESP_ERR_NO_MEM even though the flash write itself had completed.
 *
 * This worker has no large automatic objects: it dispatches one pointer into the
 * existing Core persistence APIs. 4 kB is the normal bounded ESP-IDF task budget
 * for this call path and returns 4 kB of internal DRAM to the Core verification
 * allocation without moving any flash-active data back into PSRAM. */
#define LOCAL_PERSISTENCE_STACK_BYTES 4096U
#define LOCAL_PERSISTENCE_TASK_PRIORITY 5U
#define LOCAL_PROFILE_ID_MAX 64U

typedef enum {
    LOCAL_PERSIST_APP = 0,
    LOCAL_PERSIST_SOLAR_GRID,
    LOCAL_PERSIST_PROFILE,
} local_persistence_operation_t;

typedef struct {
    uint8_t index;
    char profile_id[LOCAL_PROFILE_ID_MAX];
} local_profile_payload_t;

typedef struct {
    local_persistence_operation_t operation;
    void *payload;
    esp_err_t result;
    StaticSemaphore_t done_storage;
    SemaphoreHandle_t done;
} local_persistence_request_t;

static void persistence_worker(void *argument)
{
    local_persistence_request_t *request = (local_persistence_request_t *)argument;
    if (!request || !request->payload) {
        if (request) {
            request->result = ESP_ERR_INVALID_ARG;
            xSemaphoreGive(request->done);
        }
        vTaskDelete(NULL);
        return;
    }

    switch (request->operation) {
    case LOCAL_PERSIST_APP:
        request->result = config_manager_save((const app_config_t *)request->payload);
        break;
    case LOCAL_PERSIST_SOLAR_GRID:
        request->result = solar_grid_config_save((const solar_grid_config_t *)request->payload);
        break;
    case LOCAL_PERSIST_PROFILE: {
        const local_profile_payload_t *profile = (const local_profile_payload_t *)request->payload;
        request->result = inverter_profile_store_set(profile->index, profile->profile_id);
        break;
    }
    default:
        request->result = ESP_ERR_INVALID_ARG;
        break;
    }

    /* The requester is blocked while flash cache may be disabled. Its LVGL stack
     * therefore remains untouched in PSRAM until this internal-stack task has
     * completely returned from the NVS/flash API. */
    xSemaphoreGive(request->done);
    vTaskDelete(NULL);
}

static esp_err_t run_internal(local_persistence_operation_t operation,
                              const void *payload,
                              size_t payload_size)
{
    if (!payload || payload_size == 0U) return ESP_ERR_INVALID_ARG;

    local_persistence_request_t *request = heap_caps_calloc(
        1U, sizeof(*request), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!request) return ESP_ERR_NO_MEM;

    request->payload = heap_caps_malloc(payload_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!request->payload) {
        heap_caps_free(request);
        return ESP_ERR_NO_MEM;
    }

    /* Copy PSRAM-backed caller data while cache is fully available. The worker
     * subsequently hands only internal-DRAM buffers to Core persistence APIs. */
    memcpy(request->payload, payload, payload_size);
    request->operation = operation;
    request->result = ESP_FAIL;
    request->done = xSemaphoreCreateBinaryStatic(&request->done_storage);
    if (!request->done) {
        heap_caps_free(request->payload);
        heap_caps_free(request);
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreateWithCaps(
        persistence_worker,
        "hmi_persist",
        LOCAL_PERSISTENCE_STACK_BYTES,
        request,
        LOCAL_PERSISTENCE_TASK_PRIORITY,
        NULL,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        heap_caps_free(request->payload);
        heap_caps_free(request);
        return ESP_ERR_NO_MEM;
    }

    /* Persistence is an explicit engineering action. Keep the public backend API
     * synchronous, but block the PSRAM-stacked LVGL task rather than executing
     * flash writes on it. The worker always signals completion before deletion. */
    (void)xSemaphoreTake(request->done, portMAX_DELAY);
    const esp_err_t result = request->result;
    heap_caps_free(request->payload);
    heap_caps_free(request);
    return result;
}

esp_err_t local_persistence_save_app(const app_config_t *config)
{
    return run_internal(LOCAL_PERSIST_APP, config, sizeof(*config));
}

esp_err_t local_persistence_save_solar_grid(const solar_grid_config_t *config)
{
    return run_internal(LOCAL_PERSIST_SOLAR_GRID, config, sizeof(*config));
}

esp_err_t local_persistence_set_inverter_profile(uint8_t index, const char *profile_id)
{
    if (!profile_id || !profile_id[0] || strlen(profile_id) >= LOCAL_PROFILE_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    local_profile_payload_t payload = {
        .index = index,
    };
    strlcpy(payload.profile_id, profile_id, sizeof(payload.profile_id));
    return run_internal(LOCAL_PERSIST_PROFILE, &payload, sizeof(payload));
}
