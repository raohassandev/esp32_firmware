#include "config_manager.h"
#include "control_engine.h"

/* Generic configuration import always persists automatic control disabled, but
 * the running control task caches its boot-time configuration. Route only the
 * HTTP import path through this bridge so command authority is removed before
 * imported mappings/tuning can become authoritative. Safe-zero I/O remains on
 * the control task; the HTTP server never waits on an inverter transaction. */
esp_err_t web_config_manager_import_json_guarded(const char *json_text)
{
    control_engine_force_disable();
    return config_manager_import_json(json_text);
}
