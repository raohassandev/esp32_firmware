#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { PVDG_UI_RESOURCE_UNKNOWN=0,PVDG_UI_RESOURCE_HEALTHY,PVDG_UI_RESOURCE_REVIEW,PVDG_UI_RESOURCE_CRITICAL } pvdg_ui_resource_state_t;
typedef struct { bool available; const char *target; uint32_t chip_revision; uint8_t cpu_cores; bool cpu_frequency_available; uint32_t cpu_frequency_mhz; uint64_t uptime_ms; uint32_t task_count; const char *reset_reason_name; uint64_t total_internal_heap_bytes; uint64_t free_heap_bytes; uint64_t minimum_free_heap_bytes; uint64_t free_internal_heap_bytes; uint64_t minimum_internal_heap_bytes; uint64_t largest_internal_block_bytes; double internal_fragmentation_ratio; bool psram_available; uint64_t psram_total_bytes; uint64_t psram_free_bytes; uint64_t psram_largest_block_bytes; bool flash_size_available; uint64_t flash_size_bytes; bool temperature_available; double temperature_c; const char *temperature_note; pvdg_ui_resource_state_t resource_state; } pvdg_ui_system_resources_t;
pvdg_ui_resource_state_t pvdg_ui_resource_state_parse(const char *name); const char *pvdg_ui_resource_state_label(pvdg_ui_resource_state_t state); lv_obj_t *pvdg_ui_diagnostics_create(lv_obj_t *parent); void pvdg_ui_diagnostics_apply(const pvdg_ui_system_resources_t *resources); void pvdg_ui_diagnostics_show_unavailable(void);
#ifdef __cplusplus
}
#endif
