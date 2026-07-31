#include "source_detection_config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs.h"

#define SOURCE_DETECTION_NVS_NAMESPACE "src_detect"
#define SOURCE_DETECTION_NVS_KEY "config"

static const char *TAG = "source_config";
static source_detection_config_t s_config;
static uint32_t s_generation;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void source_detection_config_defaults(source_detection_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->magic = SOURCE_DETECTION_CONFIG_MAGIC;
    config->version = SOURCE_DETECTION_CONFIG_VERSION;
    config->mode = SOURCE_DETECTION_MODE_DISABLED;
    config->single.meter_index = SOURCE_DETECTION_METER_INDEX_NONE;
    config->single.function_code = 0U;
    config->single.address_base = SOURCE_DETECTION_ADDRESS_BASE_UNCONFIGURED;
    config->single.register_address = SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT;
    config->single.grid_value = 0U;
    config->single.generator_value = 1U;
    config->dual.grid_meter_index = SOURCE_DETECTION_METER_INDEX_NONE;
    config->dual.generator_meter_index = SOURCE_DETECTION_METER_INDEX_NONE;
    config->dual.grid_threshold_kw = SOURCE_DETECTION_POWER_THRESHOLD_DEFAULT_KW;
    config->dual.generator_threshold_kw = SOURCE_DETECTION_POWER_THRESHOLD_DEFAULT_KW;
    /* No site timing is assumed. Commissioning must supply both values before
     * either detection mode can become valid. */
    config->debounce_ms = 0U;
    config->stale_timeout_ms = 0U;
}

bool source_detection_config_valid(const source_detection_config_t *config)
{
    if (!config || config->magic != SOURCE_DETECTION_CONFIG_MAGIC ||
        config->version != SOURCE_DETECTION_CONFIG_VERSION ||
        config->mode > SOURCE_DETECTION_MODE_DUAL_METER) {
        return false;
    }
    if (config->mode == SOURCE_DETECTION_MODE_DISABLED) return true;
    if (config->debounce_ms == 0U || config->stale_timeout_ms == 0U) return false;

    if (config->mode == SOURCE_DETECTION_MODE_SINGLE_INPUT) {
        return config->single.meter_index != SOURCE_DETECTION_METER_INDEX_NONE &&
               (config->single.function_code == 3U ||
                config->single.function_code == 4U) &&
               config->single.address_base <= 1U &&
               config->single.register_address >= config->single.address_base &&
               config->single.grid_value != config->single.generator_value;
    }

    return config->dual.grid_meter_index != SOURCE_DETECTION_METER_INDEX_NONE &&
           config->dual.generator_meter_index != SOURCE_DETECTION_METER_INDEX_NONE &&
           config->dual.grid_meter_index != config->dual.generator_meter_index &&
           isfinite(config->dual.grid_threshold_kw) &&
           isfinite(config->dual.generator_threshold_kw) &&
           config->dual.grid_threshold_kw > 0.0f &&
           config->dual.generator_threshold_kw > 0.0f;
}

source_detection_policy_t source_detection_config_policy(
    const source_detection_config_t *config,
    bool single_meter_is_em500)
{
    source_detection_policy_t policy = {0};
    /* A NULL configuration returns the zeroed policy, which carries
     * single_bitmask_semantics = false. The bitmask reading is never reachable
     * without a configuration to license it. */
    if (!config) return policy;
    policy.mode = (source_detection_mode_t)config->mode;
    policy.debounce_ms = config->debounce_ms;
    policy.stale_timeout_ms = config->stale_timeout_ms;
    policy.single_grid_value = config->single.grid_value;
    policy.single_generator_value = config->single.generator_value;
    policy.single_bitmask_semantics = single_meter_is_em500;
    policy.grid_threshold_kw = config->dual.grid_threshold_kw;
    policy.generator_threshold_kw = config->dual.generator_threshold_kw;
    /* Copied straight through, like every other commissioned fact. A zeroed
     * policy therefore says "this plant cannot synchronise", which is the
     * fail-closed answer: two live sources is treated as a fault until an
     * engineer states otherwise. */
    policy.sync_capable = config->dual.sync_capable != 0U;
    return policy;
}

static void set_active(const source_detection_config_t *config)
{
    portENTER_CRITICAL(&s_lock);
    s_config = *config;
    s_generation++;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t source_detection_config_init(void)
{
    source_detection_config_t loaded;
    source_detection_config_defaults(&loaded);

    /* This phase deliberately uses an additive NVS namespace. The commissioned
     * pvdg/config blob, including Wi-Fi credentials, is never resized, rewritten
     * or reset by source detection. */
    nvs_handle_t handle;
    esp_err_t error = nvs_open(SOURCE_DETECTION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        set_active(&loaded);
        ESP_LOGW(TAG, "Source detection is not commissioned; safe disabled defaults loaded");
        return ESP_OK;
    }
    if (error != ESP_OK) return error;

    size_t stored_size = 0U;
    error = nvs_get_blob(handle, SOURCE_DETECTION_NVS_KEY, NULL, &stored_size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        set_active(&loaded);
        ESP_LOGW(TAG, "Source detection is not commissioned; safe disabled defaults loaded");
        return ESP_OK;
    }
    if (error != ESP_OK) {
        nvs_close(handle);
        return error;
    }

    if (stored_size != sizeof(loaded)) {
        nvs_close(handle);
        set_active(&loaded);
        ESP_LOGE(TAG,
                 "Stored source-detection schema is %u bytes; expected %u. Existing data was not erased or overwritten",
                 (unsigned)stored_size, (unsigned)sizeof(loaded));
        return ESP_OK;
    }

    size_t read_size = sizeof(loaded);
    error = nvs_get_blob(handle, SOURCE_DETECTION_NVS_KEY, &loaded, &read_size);
    nvs_close(handle);
    if (error != ESP_OK) return error;

    if (!source_detection_config_valid(&loaded)) {
        source_detection_config_defaults(&loaded);
        set_active(&loaded);
        ESP_LOGE(TAG,
                 "Stored source-detection configuration is invalid; it was retained in NVS and detection remains disabled");
        return ESP_OK;
    }

    set_active(&loaded);
    return ESP_OK;
}

esp_err_t source_detection_config_get_snapshot(source_detection_config_t *out_config)
{
    if (!out_config) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    *out_config = s_config;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

uint32_t source_detection_config_generation(void)
{
    uint32_t generation;
    portENTER_CRITICAL(&s_lock);
    generation = s_generation;
    portEXIT_CRITICAL(&s_lock);
    return generation;
}

esp_err_t source_detection_config_save(const source_detection_config_t *config)
{
    if (!source_detection_config_valid(config)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t error = nvs_open(SOURCE_DETECTION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_blob(handle, SOURCE_DETECTION_NVS_KEY, config, sizeof(*config));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    if (error != ESP_OK) return error;

    source_detection_config_t *verify = malloc(sizeof(*verify));
    if (!verify) return ESP_ERR_NO_MEM;
    size_t verify_size = sizeof(*verify);
    error = nvs_open(SOURCE_DETECTION_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, SOURCE_DETECTION_NVS_KEY, verify, &verify_size);
        nvs_close(handle);
    }
    const bool verified = error == ESP_OK && verify_size == sizeof(*verify) &&
                          memcmp(verify, config, sizeof(*config)) == 0;
    free(verify);
    if (!verified) return error == ESP_OK ? ESP_ERR_INVALID_CRC : error;

    set_active(config);
    return ESP_OK;
}
