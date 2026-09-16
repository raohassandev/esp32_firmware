#include "engineering_config_bridge.h"

#include <stdio.h>
#include <string.h>

#include "config_manager.h"
#include "control_engine.h"
#include "engineering_runtime_bridge.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"
#include "solar_grid_config.h"

static bool s_inverter_config_available;
static bool s_inverter_profiles_available;
static bool s_inverter_assignments_available;

static void copy_text(char *target, size_t capacity, const char *source)
{
    if (!target || capacity == 0U) return;
    snprintf(target, capacity, "%s", source ? source : "");
}

static void signal_to_ui(const solar_grid_signal_config_t *source,
                         pvdg_ui_source_signal_t *target)
{
    target->enabled = source->enabled;
    target->meter_index = source->meter_index;
    target->function_code = source->function_code;
    target->address = source->address;
    target->mask = source->mask;
    target->active_value = source->active_value;
}

static void signal_from_ui(const pvdg_ui_source_signal_t *source,
                           solar_grid_signal_config_t *target)
{
    target->enabled = source->enabled;
    target->meter_index = source->meter_index;
    target->function_code = source->function_code;
    target->address = source->address;
    target->mask = source->mask;
    target->active_value = source->active_value;
}

static void solar_grid_to_ui(const solar_grid_config_t *source,
                             pvdg_ui_source_config_t *target)
{
    memset(target, 0, sizeof(*target));
    target->schema = PVDG_UI_SOURCE_SCHEMA;
    target->policy = (pvdg_ui_source_policy_t)source->policy;
    target->meter_orientation = (pvdg_ui_source_meter_orientation_t)source->meter_orientation;
    target->export_limit_kw = source->export_limit_kw;
    target->minimum_import_kw = source->minimum_import_kw;
    signal_to_ui(&source->grid_available, &target->grid_available);
    signal_to_ui(&source->grid_breaker_closed, &target->grid_breaker_closed);
    target->evidence_poll_interval_ms = source->evidence_poll_interval_ms;
    target->evidence_stale_timeout_ms = source->evidence_stale_timeout_ms;
    target->grid_loss_trip_ms = source->grid_loss_trip_ms;
    target->grid_recovery_stable_ms = source->grid_recovery_stable_ms;
    for (uint8_t i = 0U; i < PVDG_UI_SOURCE_MAX_GENERATORS; ++i) {
        target->generators[i].rated_kw = source->generators[i].rated_kw;
        target->generators[i].minimum_loading_percent =
            source->generators[i].minimum_loading_percent;
        target->generators[i].reserve_kw = source->generators[i].reserve_kw;
        target->generators[i].reverse_power_margin_kw =
            source->generators[i].reverse_power_margin_kw;
        signal_to_ui(&source->generators[i].running, &target->generators[i].running);
        signal_to_ui(&source->generators[i].breaker_closed,
                     &target->generators[i].breaker_closed);
    }
    signal_to_ui(&source->transfer_active, &target->transfer_active);
    signal_to_ui(&source->grid_generator_synchronized,
                 &target->grid_generator_synchronized);
}

static void solar_grid_from_ui(const pvdg_ui_source_config_t *source,
                               solar_grid_config_t *target)
{
    solar_grid_config_defaults(target);
    target->policy = (solar_grid_policy_t)source->policy;
    target->meter_orientation = (solar_grid_meter_orientation_t)source->meter_orientation;
    target->export_limit_kw = (float)source->export_limit_kw;
    target->minimum_import_kw = (float)source->minimum_import_kw;
    signal_from_ui(&source->grid_available, &target->grid_available);
    signal_from_ui(&source->grid_breaker_closed, &target->grid_breaker_closed);
    target->evidence_poll_interval_ms = source->evidence_poll_interval_ms;
    target->evidence_stale_timeout_ms = source->evidence_stale_timeout_ms;
    target->grid_loss_trip_ms = source->grid_loss_trip_ms;
    target->grid_recovery_stable_ms = source->grid_recovery_stable_ms;
    for (uint8_t i = 0U; i < PVDG_UI_SOURCE_MAX_GENERATORS; ++i) {
        target->generators[i].rated_kw = (float)source->generators[i].rated_kw;
        target->generators[i].minimum_loading_percent =
            (float)source->generators[i].minimum_loading_percent;
        target->generators[i].reserve_kw = (float)source->generators[i].reserve_kw;
        target->generators[i].reverse_power_margin_kw =
            (float)source->generators[i].reverse_power_margin_kw;
        signal_from_ui(&source->generators[i].running, &target->generators[i].running);
        signal_from_ui(&source->generators[i].breaker_closed,
                       &target->generators[i].breaker_closed);
    }
    signal_from_ui(&source->transfer_active, &target->transfer_active);
    signal_from_ui(&source->grid_generator_synchronized,
                   &target->grid_generator_synchronized);

    /* Schema-4 compatibility mirror remains owned by the Core config format. */
    target->generator_rated_kw = target->generators[0].rated_kw;
    target->generator_minimum_loading_percent =
        target->generators[0].minimum_loading_percent;
    target->generator_reserve_kw = target->generators[0].reserve_kw;
    target->generator_reverse_power_margin_kw =
        target->generators[0].reverse_power_margin_kw;
    target->generator_running = target->generators[0].running;
    target->generator_breaker_closed = target->generators[0].breaker_closed;
}

static bool latch_control_disabled(void)
{
    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK) return false;
    app.control.enabled = false;
    control_engine_force_disable();
    return config_manager_save(&app) == ESP_OK;
}

static void source_state(bool available, const char *message, bool restart_required)
{
    const pvdg_ui_source_setup_state_t state = {
        .config_available = available,
        .write_allowed = engineering_runtime_bridge_is_authorized(),
        .busy = false,
        .restart_required = restart_required,
        .message = message,
    };
    pvdg_ui_source_setup_apply_state(&state);
}

static void export_state(bool available, const char *message, bool restart_required)
{
    const pvdg_ui_export_control_state_t state = {
        .config_available = available,
        .write_allowed = engineering_runtime_bridge_is_authorized(),
        .busy = false,
        .restart_required = restart_required,
        .message = message,
    };
    pvdg_ui_export_control_apply_state(&state);
}

void engineering_config_source_request(void *user)
{
    (void)user;
    solar_grid_config_t core = {0};
    if (solar_grid_config_get_snapshot(&core) != ESP_OK) {
        source_state(false, "Current source configuration is unavailable.", false);
        return;
    }
    pvdg_ui_source_config_t ui;
    solar_grid_to_ui(&core, &ui);
    pvdg_ui_source_setup_set_config(&ui);
    source_state(true,
                 engineering_runtime_bridge_is_authorized()
                     ? "Source configuration loaded. Saves disable automatic control and require restart."
                     : "Read-only until Engineering authentication.",
                 false);
}

void engineering_config_source_submit(const pvdg_ui_source_config_t *config, void *user)
{
    (void)user;
    if (!config || !engineering_runtime_bridge_is_authorized()) {
        source_state(config != NULL, "Engineering authentication required for source writes.", false);
        return;
    }
    char error[192] = {0};
    if (pvdg_ui_source_config_validate(config, error, sizeof(error)) != PVDG_UI_SOURCE_CONFIG_OK) {
        source_state(true, error[0] ? error : "Source configuration validation failed.", false);
        return;
    }
    solar_grid_config_t core;
    solar_grid_from_ui(config, &core);
    if (!solar_grid_config_valid(&core)) {
        source_state(true, "Core rejected the source configuration.", false);
        return;
    }
    if (!latch_control_disabled()) {
        source_state(true, "Could not persist the control-disable interlock; source save aborted.", false);
        return;
    }
    if (solar_grid_config_save(&core) != ESP_OK) {
        source_state(true, "Source save failed. Automatic control remains disabled.", false);
        return;
    }
    pvdg_ui_source_setup_set_config(config);
    source_state(true, "Source configuration saved. Automatic control is disabled; restart required.", true);
}

static void meter_state(bool available, const char *message, bool restart_required)
{
    const pvdg_ui_meter_setup_state_t state = {
        .config_available = available,
        .write_allowed = engineering_runtime_bridge_is_authorized(),
        .busy = false,
        .restart_required = restart_required,
        .message = message,
    };
    pvdg_ui_meter_setup_apply_state(&state);
}

void engineering_config_meter_request(void *user)
{
    (void)user;
    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK) {
        meter_state(false, "Current meter configuration is unavailable.", false);
        return;
    }
    pvdg_ui_meter_list_t list = {0};
    list.count = app.meter_count > PVDG_UI_MAX_METERS ? PVDG_UI_MAX_METERS : app.meter_count;
    for (uint8_t i = 0U; i < list.count; ++i) {
        const meter_config_t *source = &app.meters[i];
        pvdg_ui_meter_config_t *target = &list.meters[i];
        target->enabled = source->enabled;
        copy_text(target->name, sizeof(target->name), source->name);
        copy_text(target->host, sizeof(target->host), source->endpoint.host);
        target->port = source->endpoint.port;
        target->unit_id = source->endpoint.unit_id;
        target->timeout_ms = source->endpoint.timeout_ms;
        target->function_code = source->function_code;
        target->active_power_address = source->active_power_address;
        target->data_type = (pvdg_ui_meter_data_type_t)source->active_power_type;
        target->word_order = (pvdg_ui_meter_word_order_t)source->active_power_order;
        target->scale = source->active_power_scale;
        target->poll_ms = source->poll_interval_ms;
        target->role = (pvdg_ui_meter_role_t)source->role;
        target->generator_index = source->generator_index;
    }
    pvdg_ui_meter_setup_set_list(&list);
    meter_state(true,
                engineering_runtime_bridge_is_authorized()
                    ? "Meter list loaded. Saves disable automatic control and require restart."
                    : "Read-only until Engineering authentication.",
                false);
}

void engineering_config_meter_submit(const pvdg_ui_meter_list_t *list, void *user)
{
    (void)user;
    if (!list || !engineering_runtime_bridge_is_authorized()) {
        meter_state(list != NULL, "Engineering authentication required for meter writes.", false);
        return;
    }
    char error[192] = {0};
    if (pvdg_ui_meter_list_validate(list, error, sizeof(error)) != PVDG_UI_METER_CONFIG_OK) {
        meter_state(true, error[0] ? error : "Meter configuration validation failed.", false);
        return;
    }
    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK) {
        meter_state(true, "Current controller configuration is unavailable.", false);
        return;
    }
    meter_config_t next[APP_MAX_METERS] = {0};
    for (uint8_t i = 0U; i < list->count && i < APP_MAX_METERS; ++i) {
        if (i < app.meter_count) next[i] = app.meters[i];
        const pvdg_ui_meter_config_t *source = &list->meters[i];
        next[i].enabled = source->enabled;
        copy_text(next[i].name, sizeof(next[i].name), source->name);
        copy_text(next[i].endpoint.host, sizeof(next[i].endpoint.host), source->host);
        next[i].endpoint.port = source->port;
        next[i].endpoint.unit_id = source->unit_id;
        next[i].endpoint.timeout_ms = source->timeout_ms;
        next[i].function_code = source->function_code;
        next[i].active_power_address = source->active_power_address;
        next[i].active_power_type = (modbus_data_type_t)source->data_type;
        next[i].active_power_order = (modbus_word_order_t)source->word_order;
        next[i].active_power_scale = (float)source->scale;
        next[i].poll_interval_ms = source->poll_ms;
        next[i].role = (uint8_t)source->role;
        next[i].generator_index = source->role == PVDG_UI_METER_ROLE_GENERATOR
                                      ? source->generator_index : METER_GENERATOR_INDEX_NONE;
    }
    memset(app.meters, 0, sizeof(app.meters));
    memcpy(app.meters, next, sizeof(next));
    app.meter_count = list->count;
    app.control.enabled = false;
    control_engine_force_disable();
    if (config_manager_save(&app) != ESP_OK) {
        meter_state(true, "Meter save failed. Automatic control remains disabled.", false);
        return;
    }
    pvdg_ui_meter_setup_set_list(list);
    meter_state(true, "Meter configuration saved. Automatic control is disabled; restart required.", true);
}

static void inverter_state(const char *message, bool restart_required)
{
    const pvdg_ui_inverter_setup_state_t state = {
        .config_available = s_inverter_config_available,
        .profiles_available = s_inverter_profiles_available,
        .assignments_available = s_inverter_assignments_available,
        .write_allowed = engineering_runtime_bridge_is_authorized(),
        .busy = false,
        .restart_required = restart_required,
        .message = message,
    };
    pvdg_ui_inverter_setup_apply_state(&state);
}

void engineering_config_inverter_request(void *user)
{
    (void)user;
    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK) {
        s_inverter_config_available = false;
        inverter_state("Current inverter configuration is unavailable.", false);
        return;
    }
    pvdg_ui_inverter_list_t list = {0};
    list.count = app.inverter_count > PVDG_UI_MAX_INVERTER_CONFIGS
                     ? PVDG_UI_MAX_INVERTER_CONFIGS : app.inverter_count;
    for (uint8_t i = 0U; i < list.count; ++i) {
        const inverter_config_t *source = &app.inverters[i];
        pvdg_ui_inverter_config_t *target = &list.inverters[i];
        target->enabled = source->enabled;
        copy_text(target->name, sizeof(target->name), source->name);
        copy_text(target->host, sizeof(target->host), source->endpoint.host);
        target->port = source->endpoint.port;
        target->unit_id = source->endpoint.unit_id;
        target->timeout_ms = source->endpoint.timeout_ms;
        target->rated_kw = source->rated_power_kw;
    }
    pvdg_ui_inverter_setup_set_config(&list);
    s_inverter_config_available = true;
    inverter_state(engineering_runtime_bridge_is_authorized()
                       ? "Inverter list loaded. Edits disable automatic control and require restart."
                       : "Read-only until Engineering authentication.",
                   false);
}

void engineering_config_inverter_profiles_request(void *user)
{
    (void)user;
    pvdg_ui_inverter_profile_catalog_t catalog = {0};
    const size_t total = inverter_profiles_count();
    const size_t count = total > PVDG_UI_MAX_INVERTER_PROFILES
                             ? PVDG_UI_MAX_INVERTER_PROFILES : total;
    catalog.count = (uint8_t)count;
    catalog.truncated = total > count;

    for (size_t i = 0U; i < count; ++i) {
        const inverter_profile_t *source = inverter_profiles_get(i);
        if (!source || !source->id) {
            s_inverter_profiles_available = false;
            inverter_state("Compiled inverter profile catalog is unavailable.", false);
            return;
        }
        pvdg_ui_inverter_profile_t *target = &catalog.profiles[i];
        copy_text(target->id, sizeof(target->id), source->id);
        copy_text(target->manufacturer, sizeof(target->manufacturer), source->manufacturer);
        copy_text(target->model_family, sizeof(target->model_family), source->model_family);
        copy_text(target->protocol, sizeof(target->protocol), source->protocol);
        copy_text(target->connection, sizeof(target->connection),
                  inverter_profile_connection_label(source->connection));
        copy_text(target->manual_reference, sizeof(target->manual_reference),
                  source->manual_reference);
        target->qualification =
            (pvdg_ui_profile_qualification_t)source->qualification;
        target->simulator_only = source->simulator_only;
        target->read_allowed = inverter_profile_allows_read(source);
        target->write_allowed = inverter_profile_allows_write(source);
        target->identity_probe_supported = source->has_identity_probe;
        target->active_power_supported = source->has_active_power;
        target->power_limit_supported = source->has_power_limit;
        target->power_limit_readback_supported = source->has_power_limit_readback;
        target->status_register_supported = inverter_profile_has_status_register(source);
        target->status_register_commissioning_required =
            !inverter_profile_has_status_register(source);
        target->minimum_percent = source->minimum_percent;
        target->maximum_percent = source->maximum_percent;
        target->fingerprint[0] = '\0';
    }

    pvdg_ui_inverter_setup_set_profiles(&catalog);
    s_inverter_profiles_available = true;
    inverter_state("Compiled inverter profiles loaded.", false);
}

void engineering_config_inverter_assignments_request(void *user)
{
    (void)user;
    inverter_profile_assignment_manifest_t manifest = {0};
    if (inverter_profile_store_get_all(&manifest) != ESP_OK) {
        s_inverter_assignments_available = false;
        inverter_state("Inverter profile assignments are unavailable.", false);
        return;
    }

    pvdg_ui_inverter_assignment_map_t assignments = {0};
    for (uint8_t i = 0U; i < PVDG_UI_MAX_INVERTER_CONFIGS; ++i) {
        copy_text(assignments.assignments[i].profile_id,
                  sizeof(assignments.assignments[i].profile_id),
                  manifest.profile_ids[i]);
        assignments.assignments[i].fingerprint[0] = '\0';
    }
    pvdg_ui_inverter_setup_set_assignments(&assignments);
    s_inverter_assignments_available = true;
    inverter_state("Inverter profile assignments loaded.", false);
}

void engineering_config_inverter_submit(const pvdg_ui_inverter_list_t *list, void *user)
{
    (void)user;
    if (!list || !engineering_runtime_bridge_is_authorized()) {
        inverter_state("Engineering authentication required for inverter writes.", false);
        return;
    }
    char error[192] = {0};
    if (pvdg_ui_inverter_list_validate(list, error, sizeof(error)) != PVDG_UI_INVERTER_CONFIG_OK) {
        inverter_state(error[0] ? error : "Inverter configuration validation failed.", false);
        return;
    }
    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK) {
        inverter_state("Current controller configuration is unavailable.", false);
        return;
    }
    inverter_config_t next[APP_MAX_INVERTERS] = {0};
    for (uint8_t i = 0U; i < list->count && i < APP_MAX_INVERTERS; ++i) {
        if (i < app.inverter_count) next[i] = app.inverters[i];
        const pvdg_ui_inverter_config_t *source = &list->inverters[i];
        next[i].enabled = source->enabled;
        copy_text(next[i].name, sizeof(next[i].name), source->name);
        copy_text(next[i].endpoint.host, sizeof(next[i].endpoint.host), source->host);
        next[i].endpoint.port = source->port;
        next[i].endpoint.unit_id = source->unit_id;
        next[i].endpoint.timeout_ms = source->timeout_ms;
        next[i].rated_power_kw = (float)source->rated_kw;
    }
    memset(app.inverters, 0, sizeof(app.inverters));
    memcpy(app.inverters, next, sizeof(next));
    app.inverter_count = list->count;
    app.control.enabled = false;
    control_engine_force_disable();
    if (config_manager_save(&app) != ESP_OK) {
        inverter_state("Inverter save failed. Automatic control remains disabled.", false);
        return;
    }
    s_inverter_config_available = true;
    pvdg_ui_inverter_setup_set_config(list);
    inverter_state("Inverter configuration saved. Automatic control is disabled; restart required.", true);
}

void engineering_config_inverter_assign_profile(uint8_t inverter_index,
                                                const char *profile_id,
                                                void *user)
{
    (void)user;
    if (!engineering_runtime_bridge_is_authorized()) {
        inverter_state("Engineering authentication required for profile assignment.", false);
        return;
    }
    if (!profile_id || !profile_id[0] || inverter_index >= APP_MAX_INVERTERS) {
        inverter_state("Invalid inverter profile assignment.", false);
        return;
    }
    const inverter_profile_t *profile = inverter_profiles_find(profile_id);
    if (!profile) {
        inverter_state("Selected inverter profile is not compiled into this firmware.", false);
        return;
    }

    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK ||
        inverter_index >= app.inverter_count) {
        inverter_state("Profile can only be assigned to a configured inverter slot.", false);
        return;
    }

    app.control.enabled = false;
    control_engine_force_disable();
    if (config_manager_save(&app) != ESP_OK) {
        inverter_state("Could not persist the control-disable interlock; profile change aborted.", false);
        return;
    }
    if (inverter_profile_store_set(inverter_index, profile->id) != ESP_OK) {
        inverter_state("Profile assignment save failed. Automatic control remains disabled.", false);
        return;
    }

    engineering_config_inverter_assignments_request(NULL);
    char message[160];
    snprintf(message, sizeof(message),
             "Profile '%s' assigned to inverter %u. Automatic control is disabled; restart required.",
             profile->id, (unsigned)inverter_index + 1U);
    inverter_state(message, true);
}

void engineering_config_export_request(void *user)
{
    (void)user;
    solar_grid_config_t core = {0};
    if (solar_grid_config_get_snapshot(&core) != ESP_OK) {
        export_state(false, "Current export-control configuration is unavailable.", false);
        return;
    }
    pvdg_ui_source_config_t ui;
    solar_grid_to_ui(&core, &ui);
    pvdg_ui_export_control_set_config(&ui);
    export_state(true,
                 engineering_runtime_bridge_is_authorized()
                     ? "Export policy loaded. Save forces automatic control disabled."
                     : "Read-only until Engineering authentication.",
                 false);
}

void engineering_config_export_submit(const pvdg_ui_source_config_t *config, void *user)
{
    (void)user;
    if (!config || !engineering_runtime_bridge_is_authorized()) {
        export_state(config != NULL, "Engineering authentication required for export-control writes.", false);
        return;
    }
    char error[192] = {0};
    if (pvdg_ui_source_config_validate(config, error, sizeof(error)) != PVDG_UI_SOURCE_CONFIG_OK) {
        export_state(true, error[0] ? error : "Export-control validation failed.", false);
        return;
    }
    solar_grid_config_t core;
    solar_grid_from_ui(config, &core);
    if (!solar_grid_config_valid(&core)) {
        export_state(true, "Core rejected the export-control configuration.", false);
        return;
    }
    if (!latch_control_disabled()) {
        export_state(true, "Could not persist the control-disable interlock; save aborted.", false);
        return;
    }
    if (solar_grid_config_save(&core) != ESP_OK) {
        export_state(true, "Export-control save failed. Automatic control remains disabled.", false);
        return;
    }
    pvdg_ui_export_control_set_config(config);
    export_state(true, "Export control saved. Automatic control is disabled; restart required.", true);
}
