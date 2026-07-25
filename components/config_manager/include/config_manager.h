#pragma once

#include "esp_err.h"
#include "config_types.h"

esp_err_t config_manager_init(void);
esp_err_t config_manager_get_snapshot(app_config_t *out_config);
esp_err_t config_manager_save(const app_config_t *config);
esp_err_t config_manager_import_json(const char *json_text);
esp_err_t config_manager_export_json(char **out_json);
esp_err_t config_manager_restore_defaults(void);
