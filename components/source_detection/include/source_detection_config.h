#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "source_detection_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOURCE_DETECTION_CONFIG_MAGIC 0x53524344u
#define SOURCE_DETECTION_CONFIG_VERSION 1u
#define SOURCE_DETECTION_METER_INDEX_NONE UINT8_MAX
#define SOURCE_DETECTION_ADDRESS_BASE_UNCONFIGURED UINT8_MAX
/* Verified on the installed meters 2026-07-29 by energising the 220 VAC
 * source-detection input and observing the register change: 0 with no supply,
 * 1 with supply present. 0x2160 was the previous default and returns exception
 * 0x02, illegal data address, on these units. 0x2100 is the Lovato DMG610
 * "OR of all digital inputs" register, read with function code 3. */
#define SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT 0x2100u
#define SOURCE_DETECTION_POWER_THRESHOLD_DEFAULT_KW 1.0f

typedef struct {
    uint8_t meter_index;
    uint8_t function_code;
    uint8_t address_base;
    uint8_t reserved;
    uint16_t register_address;
    uint16_t grid_value;
    uint16_t generator_value;
} source_detection_single_config_t;

typedef struct {
    uint8_t grid_meter_index;
    uint8_t generator_meter_index;
    uint16_t reserved;
    float grid_threshold_kw;
    float generator_threshold_kw;
} source_detection_dual_config_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t mode;
    uint8_t reserved;
    uint32_t debounce_ms;
    uint32_t stale_timeout_ms;
    source_detection_single_config_t single;
    source_detection_dual_config_t dual;
} source_detection_config_t;

void source_detection_config_defaults(source_detection_config_t *config);
bool source_detection_config_valid(const source_detection_config_t *config);
source_detection_policy_t source_detection_config_policy(
    const source_detection_config_t *config);
esp_err_t source_detection_config_init(void);
esp_err_t source_detection_config_get_snapshot(source_detection_config_t *out_config);
esp_err_t source_detection_config_save(const source_detection_config_t *config);
uint32_t source_detection_config_generation(void);

#ifdef __cplusplus
}
#endif
