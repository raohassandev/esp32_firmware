#pragma once

/* Controller audit trail - ESP-IDF binding.
 *
 * WHY THIS HEADER IS FORCE-INCLUDED
 * ---------------------------------
 * The endpoints that persist configuration and arm or retune automatic control
 * live in translation units owned by other work in flight. Rather than edit
 * them, this component uses the same build-level interposition the web_server
 * component already uses elsewhere (see the meter_manager_read_registers
 * substitution in CMakeLists.txt): the config-writing sources are compiled with
 *
 *     -include audit_log.h -Dconfig_manager_save=audit_config_manager_save
 *
 * so every persisted write is routed through a wrapper that records it and then
 * calls the real config_manager_save(). The wrapper is the ONLY place the audit
 * classification lives, which means a new configuration endpoint is audited the
 * day it is written, without anyone having to remember to add a call.
 *
 * The audited fact is derived from the data, not asserted by the caller: the
 * wrapper snapshots the persisted configuration, compares it with the incoming
 * one, and records a control entry only for fields that actually changed.
 *
 * NO IDENTITY IS INVENTED. The controller has no operator identity model, so an
 * entry states that an authenticated engineering session performed the action
 * and nothing more. Every audited write path sits behind the engineering
 * authorization gateway, which is what makes even that claim true.
 */

#include <stdbool.h>
#include <stdint.h>

#include "audit_log_core.h"
#include "config_types.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "solar_grid_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Records one entry. Allocation-free and log-free; the ring spinlock is held
 * only across scalar stores. Safe from any task context. */
void audit_log_record(audit_category_t category, audit_action_t action, audit_outcome_t outcome);

/* As above, with one numeric quantity attached - used for a changed control
 * setpoint or source mode. Never called from the authentication paths: a
 * numeric field adjacent to a credential event is exactly how a password length
 * ends up in a public repository's log format. */
void audit_log_record_value(audit_category_t category, audit_action_t action,
                            audit_outcome_t outcome, float value);

/* Snapshot accessors for the HTTP exporter. Copy out under the lock so the
 * exporter can build JSON with the heap, outside any critical section. */
uint16_t audit_log_snapshot(audit_entry_t *out_entries, uint16_t max_entries,
                            uint32_t *out_overwritten, uint32_t *out_last_sequence);

/* GET /api/system/audit-log. Requires an authenticated engineering session. */
esp_err_t audit_log_api_register(httpd_handle_t server);

/* Interposed persistence entry points. See the note above. */
esp_err_t audit_config_manager_save(const app_config_t *config);
esp_err_t audit_solar_grid_config_save(const solar_grid_config_t *config);

#ifdef __cplusplus
}
#endif
