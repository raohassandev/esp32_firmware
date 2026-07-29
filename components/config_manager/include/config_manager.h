#pragma once

#include "esp_err.h"
#include "config_types.h"

esp_err_t config_manager_init(void);
esp_err_t config_manager_get_snapshot(app_config_t *out_config);
esp_err_t config_manager_save(const app_config_t *config);
esp_err_t config_manager_import_json(const char *json_text);
esp_err_t config_manager_export_json(char **out_json);
esp_err_t config_manager_restore_defaults(void);

/* Resolves meter roles from a configuration snapshot. Never fails; an ambiguous
 * or incomplete assignment is reported through the returned struct so callers
 * can fail closed rather than guessing which instrument to regulate against. */
meter_role_assignment_t config_manager_role_assignment(const app_config_t *config);

/* Stable lowercase identifier for a meter role, for APIs and logs. */
const char *meter_role_name(uint8_t role);
