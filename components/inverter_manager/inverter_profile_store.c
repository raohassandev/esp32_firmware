#include "inverter_profile_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "config_manager.h"
#include "esp_check.h"
#include "esp_log.h"
#include "inverter_profiles.h"
#include "nvs.h"

#define PROFILE_NAMESPACE "pvdg"
#define PROFILE_KEY "inv_profiles"
#define PROFILE_STORE_VERSION 2u
#define DEFAULT_PROFILE_ID "custom-advanced-modbus"

/* Frozen version 1 layout, exactly as it was persisted before lab-target
 * declarations existed. It is a snapshot and must never be changed to track the
 * live struct, or the size test that identifies a version 1 blob stops working
 * and a commissioned unit's profile assignments are silently reset to defaults. */
typedef struct {
    uint16_t version;
    char profile_ids[APP_MAX_INVERTERS][INVERTER_PROFILE_ID_MAX];
} legacy_profile_store_blob_v1_t;

typedef struct {
    uint16_t version;
    char profile_ids[APP_MAX_INVERTERS][INVERTER_PROFILE_ID_MAX];
    /* Appended in version 2. Kept last so version 1 stays a byte-exact prefix.
     * Defaults to false on migration: an existing unit is never silently
     * declared a lab target by an upgrade. */
    bool lab_target[APP_MAX_INVERTERS];
} inverter_profile_store_blob_t;

_Static_assert(sizeof(inverter_profile_store_blob_t) > sizeof(legacy_profile_store_blob_v1_t),
               "version 2 must be larger than version 1 for the size test to distinguish them");
_Static_assert(offsetof(inverter_profile_store_blob_t, profile_ids) ==
                   offsetof(legacy_profile_store_blob_v1_t, profile_ids),
               "version 1 must remain a byte-exact prefix of version 2");

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
        /* Read into a buffer big enough for the current layout, then identify
         * which layout was actually stored by its size. Reading straight into
         * `loaded` and testing only the current size would treat every version 1
         * blob as corrupt and discard commissioned profile assignments. */
        uint8_t raw[sizeof(inverter_profile_store_blob_t)];
        size_t size = sizeof(raw);
        err = nvs_get_blob(handle, PROFILE_KEY, raw, &size);
        nvs_close(handle);

        bool recovered = false;
        if (err == ESP_OK && size == sizeof(inverter_profile_store_blob_t)) {
            inverter_profile_store_blob_t candidate;
            memcpy(&candidate, raw, sizeof(candidate));
            if (candidate.version == PROFILE_STORE_VERSION && blob_valid(&candidate)) {
                loaded = candidate;
                recovered = true;
            }
        } else if (err == ESP_OK && size == sizeof(legacy_profile_store_blob_v1_t)) {
            legacy_profile_store_blob_v1_t legacy;
            memcpy(&legacy, raw, sizeof(legacy));
            if (legacy.version == 1u) {
                /* Migrate: keep the commissioned profile assignments, declare no
                 * lab targets. load_defaults() already cleared lab_target. */
                for (size_t i = 0; i < APP_MAX_INVERTERS; ++i) {
                    strlcpy(loaded.profile_ids[i], legacy.profile_ids[i],
                            sizeof(loaded.profile_ids[i]));
                }
                loaded.version = PROFILE_STORE_VERSION;
                if (blob_valid(&loaded)) {
                    ESP_LOGI(TAG, "migrated inverter profile assignments to store version %u; "
                                  "no inverter is declared a lab target",
                             (unsigned)PROFILE_STORE_VERSION);
                    recovered = true;
                    err = persist(&loaded);
                }
            }
        }

        if (!recovered) {
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

/* Persists `next` and then disables automatic control if it is running, so that
 * neither a profile change nor a lab-target declaration can take effect beneath
 * a live control loop. The store is only updated after both succeed. */
static esp_err_t commit_and_disable_control(const inverter_profile_store_blob_t *next)
{
    ESP_RETURN_ON_ERROR(persist(next), TAG, "inverter profile store save failed");

    app_config_t config;
    ESP_RETURN_ON_ERROR(config_manager_get_snapshot(&config), TAG,
                        "configuration unavailable");
    if (config.control.enabled) {
        config.control.enabled = false;
        ESP_RETURN_ON_ERROR(config_manager_save(&config), TAG,
                            "failed to disable automatic control");
    }

    portENTER_CRITICAL(&s_lock);
    s_store = *next;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t inverter_profile_store_lab_target_get(uint8_t inverter_index, bool *out_declared)
{
    if (!out_declared || inverter_index >= APP_MAX_INVERTERS) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&s_lock);
    *out_declared = s_store.lab_target[inverter_index];
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t inverter_profile_store_lab_target_set(uint8_t inverter_index, bool declared)
{
    if (inverter_index >= APP_MAX_INVERTERS) return ESP_ERR_INVALID_ARG;

    inverter_profile_store_blob_t next;
    portENTER_CRITICAL(&s_lock);
    next = s_store;
    portEXIT_CRITICAL(&s_lock);
    if (next.lab_target[inverter_index] == declared) return ESP_OK;
    next.lab_target[inverter_index] = declared;

    return commit_and_disable_control(&next);
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

    return commit_and_disable_control(&next);
}
