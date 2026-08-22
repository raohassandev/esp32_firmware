#pragma once

/*
 * Touchscreen commissioning callbacks execute on the LVGL adapter task. This
 * product intentionally places that task stack in PSRAM to preserve scarce
 * internal DRAM for the Core and RGB DMA. ESP32-S3 external RAM is unavailable
 * while flash cache is disabled, so NVS/flash persistence must never execute
 * directly on that PSRAM-stacked callback task.
 *
 * Keep the existing commissioning code and safety semantics unchanged; redirect
 * only the three persistent write APIs used by this board-local HMI through a
 * dedicated internal-DRAM worker. Read-only Core APIs are not redirected.
 */

#include <stdint.h>

#include "config_manager.h"
#include "esp_err.h"
#include "inverter_profile_store.h"
#include "solar_grid_config.h"

esp_err_t local_persistence_save_app(const app_config_t *config);
esp_err_t local_persistence_save_solar_grid(const solar_grid_config_t *config);
esp_err_t local_persistence_set_inverter_profile(uint8_t index, const char *profile_id);

#define config_manager_save local_persistence_save_app
#define solar_grid_config_save local_persistence_save_solar_grid
#define inverter_profile_store_set local_persistence_set_inverter_profile
