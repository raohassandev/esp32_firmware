#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INVERTER_PROFILE_ID_MAX 40

esp_err_t inverter_profile_store_init(void);
esp_err_t inverter_profile_store_get(uint8_t inverter_index, char *profile_id, size_t profile_id_size);
esp_err_t inverter_profile_store_set(uint8_t inverter_index, const char *profile_id);

/* Declares that the endpoint configured for this inverter is a Modbus simulator
 * and not real equipment.
 *
 * Why this exists: the site's inverters are thousands of miles away, so the
 * control loop has to be exercised against a simulator before anyone travels.
 * Writing requires a profile that has been qualified against physical hardware,
 * and none has been, so without an explicit declaration the loop can never run
 * at all.
 *
 * Why it is safe: the declaration is made by an authenticated engineer, per
 * inverter, and it is the ONLY thing that unlocks a write through a profile that
 * is not production-approved. Commanding real equipment through this path would
 * require a human to declare real equipment a simulator -- a deliberate false
 * statement, not an accident or a default. Everything stays fail-closed: absent
 * or unreadable state means "not a lab target", and a lab declaration never
 * upgrades a profile's qualification or permits a production release.
 *
 * Declaring or revoking a lab target disables automatic control, exactly as
 * changing a profile does, so the change can never take effect underneath a
 * running control loop. */
esp_err_t inverter_profile_store_lab_target_get(uint8_t inverter_index, bool *out_declared);
esp_err_t inverter_profile_store_lab_target_set(uint8_t inverter_index, bool declared);

#ifdef __cplusplus
}
#endif
