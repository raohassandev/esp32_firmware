#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "config_guard";
static bool s_init_guard_active;
static bool s_core_allocation_failed;

void config_manager_core_allocation_guard_begin(void)
{
    s_core_allocation_failed = false;
    s_init_guard_active = true;
}

bool config_manager_core_allocation_guard_end(void)
{
    bool failed = s_core_allocation_failed;
    s_init_guard_active = false;
    return failed;
}

/* config_manager.c is compiled with malloc renamed to this function. */
void *config_manager_core_malloc(size_t size)
{
    void *allocation = malloc(size);
    if (!allocation && s_init_guard_active) {
        s_core_allocation_failed = true;
        ESP_LOGE(TAG, "core configuration allocation failed during initialization");
    }
    return allocation;
}

/* config_manager.c is compiled with nvs_set_blob renamed to this function.
 * If a legacy migration allocation failed, the old core would otherwise load
 * defaults and attempt to persist them, destroying recoverable commissioned
 * state. Refuse that write; the public schema-6 wrapper will return NO_MEM and
 * app_core will stop initialization with NVS untouched.
 */
esp_err_t config_manager_guarded_nvs_set_blob(nvs_handle_t handle,
                                              const char *key,
                                              const void *value,
                                              size_t length)
{
    if (s_init_guard_active && s_core_allocation_failed) {
        ESP_LOGE(TAG, "refusing NVS replacement after configuration init OOM");
        return ESP_ERR_NO_MEM;
    }
    return nvs_set_blob(handle, key, value, length);
}
