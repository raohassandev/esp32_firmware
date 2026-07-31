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

/* Stable lowercase slug for a commissioned meter model, for the API and logs.
 * An unrecognised value reports "undeclared" rather than inventing a name, which
 * is also the safe answer: an undeclared model commissions nothing. */
const char *meter_model_name(uint32_t model);

/* True when the recovery access point is still using the passphrase compiled
 * into this build - a value that is identical on every unit and published in a
 * public repository. Reports the fact, never the value. */
bool config_manager_recovery_ap_is_build_default(const app_config_t *config);
