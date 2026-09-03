#include "config_manager.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "modbus_tcp.h"
#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_NS "pvdg"
#define CONFIG_KEY "config"

static const char *TAG = "config_v6";

/* config_manager.c is compiled with source-local symbol renames. */
esp_err_t config_manager_init_core(void);
esp_err_t config_manager_save_core(const app_config_t *config);
esp_err_t config_manager_import_json_core(const char *json_text);
esp_err_t config_manager_export_json_core(char **out_json);
void config_manager_core_allocation_guard_begin(void);
bool config_manager_core_allocation_guard_end(void);

/* config_manager.c historically attempted a destructive NVS recovery.  AISH-OS
 * forbids that for commissioned controllers.  The source-local rename in
 * CMake routes that call here, where it fails closed and leaves NVS untouched. */
esp_err_t config_manager_forbidden_nvs_erase(void)
{
    ESP_LOGE(TAG, "Refusing destructive NVS erase; commissioned state left untouched");
    return ESP_ERR_NOT_SUPPORTED;
}

static bool mode_valid(uint8_t mode)
{
    return mode <= MODBUS_CONNECTION_RECONNECT_ON_ERROR;
}

static void normalize_modes_to_per_transaction(app_config_t *config)
{
    if (!config) return;
    for (uint8_t index = 0; index < APP_MAX_METERS; ++index) {
        config->meters[index].endpoint.connection_mode = MODBUS_CONNECTION_PER_TRANSACTION;
    }
    for (uint8_t index = 0; index < APP_MAX_INVERTERS; ++index) {
        config->inverters[index].endpoint.connection_mode = MODBUS_CONNECTION_PER_TRANSACTION;
    }
}

static bool configured_modes_valid(const app_config_t *config)
{
    if (!config || config->meter_count > APP_MAX_METERS ||
        config->inverter_count > APP_MAX_INVERTERS) {
        return false;
    }
    for (uint8_t index = 0; index < config->meter_count; ++index) {
        if (!mode_valid(config->meters[index].endpoint.connection_mode)) return false;
    }
    for (uint8_t index = 0; index < config->inverter_count; ++index) {
        if (!mode_valid(config->inverters[index].endpoint.connection_mode)) return false;
    }
    return true;
}

static esp_err_t read_stored_blob(void **out_blob, size_t *out_size)
{
    if (!out_blob || !out_size) return ESP_ERR_INVALID_ARG;
    *out_blob = NULL;
    *out_size = 0;

    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NS, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;

    size_t size = 0;
    error = nvs_get_blob(handle, CONFIG_KEY, NULL, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (error != ESP_OK) {
        nvs_close(handle);
        return error;
    }

    void *blob = malloc(size ? size : 1U);
    if (!blob) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }
    size_t actual = size;
    error = nvs_get_blob(handle, CONFIG_KEY, blob, &actual);
    nvs_close(handle);
    if (error != ESP_OK) {
        free(blob);
        return error;
    }

    *out_blob = blob;
    *out_size = actual;
    return ESP_OK;
}

static bool blob_header(const void *blob, size_t size, uint32_t *magic, uint16_t *version)
{
    if (!blob || size < sizeof(uint32_t) + sizeof(uint16_t) || !magic || !version) return false;
    const uint8_t *bytes = (const uint8_t *)blob;
    memcpy(magic, bytes, sizeof(*magic));
    memcpy(version, bytes + sizeof(*magic), sizeof(*version));
    return true;
}

static esp_err_t persist_blob_exact(const void *blob, size_t size)
{
    if (!blob || size == 0U) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NS, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;

    error = nvs_set_blob(handle, CONFIG_KEY, blob, size);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK) return error;

    void *verify = NULL;
    size_t verify_size = 0;
    error = read_stored_blob(&verify, &verify_size);
    bool matches = error == ESP_OK && verify && verify_size == size &&
                   memcmp(verify, blob, size) == 0;
    free(verify);
    if (error != ESP_OK) return error;
    return matches ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static esp_err_t migrate_same_size_schema5(void *blob, size_t size)
{
    if (!blob || size != sizeof(app_config_t)) return ESP_ERR_INVALID_SIZE;
    app_config_t migrated;
    memcpy(&migrated, blob, sizeof(migrated));
    if (migrated.magic != APP_CONFIG_MAGIC || migrated.version != 5U) {
        return ESP_ERR_INVALID_VERSION;
    }

    /* Schema 6 gives a meaning to a byte that was padding in schema 5.  Never
     * trust the old padding value: explicitly preserve all commissioned fields
     * while normalizing every endpoint to legacy per-transaction behaviour. */
    normalize_modes_to_per_transaction(&migrated);
    migrated.version = APP_CONFIG_VERSION;
    esp_err_t error = persist_blob_exact(&migrated, sizeof(migrated));
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Migrated schema 5 to %u; all Modbus endpoints remain per_transaction",
                 APP_CONFIG_VERSION);
    }
    return error;
}

esp_err_t config_manager_init(void)
{
    /* Initialise only.  If NVS itself needs destructive recovery, return the
     * error and require an explicit recovery decision instead of erasing it. */
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed without erase: %s", esp_err_to_name(error));
        return error;
    }

    void *stored = NULL;
    size_t stored_size = 0;
    error = read_stored_blob(&stored, &stored_size);
    if (error != ESP_OK) return error;

    uint32_t magic = 0;
    uint16_t version = 0;
    bool have_header = blob_header(stored, stored_size, &magic, &version);
    bool legacy_schema = have_header && magic == APP_CONFIG_MAGIC &&
                         version >= 1U && version < APP_CONFIG_VERSION;

    if (legacy_schema && version == 5U) {
        error = migrate_same_size_schema5(stored, stored_size);
        if (error != ESP_OK) {
            free(stored);
            return error;
        }
    }
    free(stored);

    /* The legacy core historically treats a schema-1..4 migration malloc
     * failure as "no valid config", loads defaults and then tries to persist
     * them. Guard its allocation and NVS-write boundary so recoverable
     * commissioned state can never be replaced because heap was temporarily
     * exhausted. The core may set in-memory defaults before it returns, but the
     * guarded NVS write is refused and this wrapper returns NO_MEM, causing
     * app_core initialization to stop fail-closed with stored NVS untouched. */
    config_manager_core_allocation_guard_begin();
    error = config_manager_init_core();
    bool core_allocation_failed = config_manager_core_allocation_guard_end();
    if (core_allocation_failed) {
        ESP_LOGE(TAG, "Configuration core allocation failed; refusing default fallback startup");
        return ESP_ERR_NO_MEM;
    }
    if (error != ESP_OK) return error;

    /* Schemas 1..4 are migrated by the established core field-by-field logic.
     * Their endpoint padding also predates connection_mode, so normalize the
     * resulting live configuration before any manager can consume it. */
    if (legacy_schema) {
        app_config_t snapshot;
        error = config_manager_get_snapshot(&snapshot);
        if (error != ESP_OK) return error;
        normalize_modes_to_per_transaction(&snapshot);
        snapshot.version = APP_CONFIG_VERSION;
        return config_manager_save(&snapshot);
    }
    return ESP_OK;
}

esp_err_t config_manager_save(const app_config_t *config)
{
    if (!configured_modes_valid(config)) return ESP_ERR_INVALID_ARG;
    return config_manager_save_core(config);
}

static bool parse_mode_value(cJSON *object, bool *present, uint8_t *mode)
{
    if (!present || !mode) return false;
    *present = false;
    if (!cJSON_IsObject(object)) return true;

    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, "connection_mode");
    if (!value) value = cJSON_GetObjectItemCaseSensitive(object, "connection_mode_code");
    if (!value) return true;

    uint8_t parsed = 0;
    if (cJSON_IsString(value)) {
        if (strcmp(value->valuestring, "per_transaction") == 0) {
            parsed = MODBUS_CONNECTION_PER_TRANSACTION;
        } else if (strcmp(value->valuestring, "persistent") == 0) {
            parsed = MODBUS_CONNECTION_PERSISTENT;
        } else if (strcmp(value->valuestring, "reconnect_on_error") == 0) {
            parsed = MODBUS_CONNECTION_RECONNECT_ON_ERROR;
        } else {
            return false;
        }
    } else if (cJSON_IsNumber(value)) {
        if (!isfinite(value->valuedouble) || value->valuedouble < 0.0 ||
            value->valuedouble > MODBUS_CONNECTION_RECONNECT_ON_ERROR ||
            floor(value->valuedouble) != value->valuedouble) {
            return false;
        }
        parsed = (uint8_t)value->valueint;
    } else {
        return false;
    }

    if (!mode_valid(parsed)) return false;
    *present = true;
    *mode = parsed;
    return true;
}

typedef struct {
    bool meter_present[APP_MAX_METERS];
    uint8_t meter_mode[APP_MAX_METERS];
    bool inverter_present[APP_MAX_INVERTERS];
    uint8_t inverter_mode[APP_MAX_INVERTERS];
} imported_modes_t;

static bool parse_imported_modes(cJSON *root, const app_config_t *current,
                                 imported_modes_t *modes)
{
    if (!root || !current || !modes) return false;
    memset(modes, 0, sizeof(*modes));

    cJSON *meters = cJSON_GetObjectItemCaseSensitive(root, "meters");
    if (cJSON_IsArray(meters)) {
        int count = cJSON_GetArraySize(meters);
        if (count > APP_MAX_METERS) return false;
        for (int index = 0; index < count; ++index) {
            if (!parse_mode_value(cJSON_GetArrayItem(meters, index),
                                  &modes->meter_present[index],
                                  &modes->meter_mode[index])) return false;
            if (modes->meter_present[index] && index >= current->meter_count) return false;
        }
    }

    cJSON *inverters = cJSON_GetObjectItemCaseSensitive(root, "inverters");
    if (cJSON_IsArray(inverters)) {
        int count = cJSON_GetArraySize(inverters);
        if (count > APP_MAX_INVERTERS) return false;
        for (int index = 0; index < count; ++index) {
            if (!parse_mode_value(cJSON_GetArrayItem(inverters, index),
                                  &modes->inverter_present[index],
                                  &modes->inverter_mode[index])) return false;
            if (modes->inverter_present[index] && index >= current->inverter_count) return false;
        }
    }
    return true;
}

esp_err_t config_manager_import_json(const char *json_text)
{
    if (!json_text) return ESP_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(json_text);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    app_config_t before;
    esp_err_t error = config_manager_get_snapshot(&before);
    if (error != ESP_OK) {
        cJSON_Delete(root);
        return error;
    }

    imported_modes_t modes;
    if (!parse_imported_modes(root, &before, &modes)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(root);

    error = config_manager_import_json_core(json_text);
    if (error != ESP_OK) return error;

    bool any_mode = false;
    for (uint8_t index = 0; index < before.meter_count; ++index) {
        if (modes.meter_present[index]) any_mode = true;
    }
    for (uint8_t index = 0; index < before.inverter_count; ++index) {
        if (modes.inverter_present[index]) any_mode = true;
    }
    if (!any_mode) return ESP_OK;

    app_config_t updated;
    error = config_manager_get_snapshot(&updated);
    if (error != ESP_OK) return error;
    for (uint8_t index = 0; index < updated.meter_count; ++index) {
        if (modes.meter_present[index]) {
            updated.meters[index].endpoint.connection_mode = modes.meter_mode[index];
        }
    }
    for (uint8_t index = 0; index < updated.inverter_count; ++index) {
        if (modes.inverter_present[index]) {
            updated.inverters[index].endpoint.connection_mode = modes.inverter_mode[index];
        }
    }
    return config_manager_save(&updated);
}

static void add_mode_to_array(cJSON *array, uint8_t count, const uint8_t *modes)
{
    if (!cJSON_IsArray(array) || !modes) return;
    int json_count = cJSON_GetArraySize(array);
    int limit = json_count < count ? json_count : count;
    for (int index = 0; index < limit; ++index) {
        cJSON *object = cJSON_GetArrayItem(array, index);
        if (!cJSON_IsObject(object)) continue;
        uint8_t mode = modes[index];
        cJSON_ReplaceItemInObjectCaseSensitive(
            object, "connection_mode",
            cJSON_CreateString(modbus_tcp_connection_mode_name(mode)));
        if (!cJSON_GetObjectItemCaseSensitive(object, "connection_mode")) {
            cJSON_AddStringToObject(object, "connection_mode",
                                    modbus_tcp_connection_mode_name(mode));
        }
        cJSON_ReplaceItemInObjectCaseSensitive(
            object, "connection_mode_code", cJSON_CreateNumber(mode));
        if (!cJSON_GetObjectItemCaseSensitive(object, "connection_mode_code")) {
            cJSON_AddNumberToObject(object, "connection_mode_code", mode);
        }
    }
}

esp_err_t config_manager_export_json(char **out_json)
{
    if (!out_json) return ESP_ERR_INVALID_ARG;
    *out_json = NULL;

    char *core_json = NULL;
    esp_err_t error = config_manager_export_json_core(&core_json);
    if (error != ESP_OK) return error;

    app_config_t snapshot;
    error = config_manager_get_snapshot(&snapshot);
    if (error != ESP_OK) {
        free(core_json);
        return error;
    }
    if (!configured_modes_valid(&snapshot)) {
        free(core_json);
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_Parse(core_json);
    free(core_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t meter_modes[APP_MAX_METERS] = {0};
    uint8_t inverter_modes[APP_MAX_INVERTERS] = {0};
    for (uint8_t index = 0; index < snapshot.meter_count; ++index) {
        meter_modes[index] = snapshot.meters[index].endpoint.connection_mode;
    }
    for (uint8_t index = 0; index < snapshot.inverter_count; ++index) {
        inverter_modes[index] = snapshot.inverters[index].endpoint.connection_mode;
    }

    add_mode_to_array(cJSON_GetObjectItemCaseSensitive(root, "meters"),
                      snapshot.meter_count, meter_modes);
    add_mode_to_array(cJSON_GetObjectItemCaseSensitive(root, "inverters"),
                      snapshot.inverter_count, inverter_modes);

    *out_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return *out_json ? ESP_OK : ESP_ERR_NO_MEM;
}
