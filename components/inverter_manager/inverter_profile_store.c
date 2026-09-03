#include "inverter_profile_store.h"

#include <stdbool.h>
#include <string.h>
#include "config_manager.h"
#include "esp_check.h"
#include "esp_log.h"
#include "inverter_profiles.h"
#include "nvs.h"

#define PROFILE_NAMESPACE "pvdg"
#define PROFILE_KEY "inv_profiles"
#define PROFILE_STORE_VERSION 1u
#define DEFAULT_PROFILE_ID "custom-advanced-modbus"

typedef struct {
    uint16_t version;
    char profile_ids[APP_MAX_INVERTERS][INVERTER_PROFILE_ID_MAX];
} inverter_profile_store_blob_t;

static const char *TAG = "inv_profile_store";
static inverter_profile_store_blob_t s_store;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void load_defaults(inverter_profile_store_blob_t *store)
{
    memset(store, 0, sizeof(*store));
    store->version = PROFILE_STORE_VERSION;
    for (size_t i = 0; i < APP_MAX_INVERTERS; ++i) {
        strlcpy(store->profile_ids[i], DEFAULT_PROFILE_ID,
                sizeof(store->profile_ids[i]));
    }
}

static bool blob_valid(const inverter_profile_store_blob_t *store)
{
    if (!store || store->version != PROFILE_STORE_VERSION) return false;
    for (size_t i = 0; i < APP_MAX_INVERTERS; ++i) {
        if (!store->profile_ids[i][0] ||
            !inverter_profiles_find(store->profile_ids[i])) return false;
    }
    return true;
}

static esp_err_t persist(const inverter_profile_store_blob_t *store)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(PROFILE_NAMESPACE, NVS_READWRITE, &handle),
                        TAG, "NVS open failed");
    esp_err_t err = nvs_set_blob(handle, PROFILE_KEY, store, sizeof(*store));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t inverter_profile_store_init(void)
{
    inverter_profile_store_blob_t loaded;
    load_defaults(&loaded);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROFILE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t size = sizeof(loaded);
        err = nvs_get_blob(handle, PROFILE_KEY, &loaded, &size);
        nvs_close(handle);
        if (err != ESP_OK || size != sizeof(loaded) || !blob_valid(&loaded)) {
            ESP_LOGW(TAG, "stored inverter profile assignments invalid; using safe defaults");
            load_defaults(&loaded);
            err = persist(&loaded);
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_NOT_INITIALIZED) {
        load_defaults(&loaded);
        err = persist(&loaded);
    }

    if (err != ESP_OK) return err;

    portENTER_CRITICAL(&s_lock);
    s_store = loaded;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t inverter_profile_store_get(uint8_t inverter_index, char *profile_id,
                                     size_t profile_id_size)
{
    if (!profile_id || profile_id_size == 0 || inverter_index >= APP_MAX_INVERTERS) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_lock);
    strlcpy(profile_id, s_store.profile_ids[inverter_index], profile_id_size);
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t inverter_profile_store_set(uint8_t inverter_index, const char *profile_id)
{
    if (inverter_index >= APP_MAX_INVERTERS || !profile_id || !profile_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!inverter_profiles_find(profile_id)) return ESP_ERR_NOT_FOUND;
    if (strlen(profile_id) >= INVERTER_PROFILE_ID_MAX) return ESP_ERR_INVALID_SIZE;

    inverter_profile_store_blob_t next;
    portENTER_CRITICAL(&s_lock);
    next = s_store;
    portEXIT_CRITICAL(&s_lock);
    strlcpy(next.profile_ids[inverter_index], profile_id,
            sizeof(next.profile_ids[inverter_index]));

    /* Disable persisted automatic control BEFORE changing the profile map.
     * If the control-config save fails, the profile assignment is untouched.
     * If the later profile write fails, staying disabled is the safe outcome.
     * This ordering prevents a reset/power loss between commits from booting
     * an enabled controller against a newly-selected register map or scale. */
    app_config_t config;
    ESP_RETURN_ON_ERROR(config_manager_get_snapshot(&config), TAG,
                        "configuration unavailable");
    if (config.control.enabled) {
        config.control.enabled = false;
        ESP_RETURN_ON_ERROR(config_manager_save(&config), TAG,
                            "failed to disable automatic control before profile change");
    }

    ESP_RETURN_ON_ERROR(persist(&next), TAG, "profile assignment save failed");

    portENTER_CRITICAL(&s_lock);
    s_store = next;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}
