#include "local_commissioning_backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config_manager.h"
#include "control_engine.h"
#include "engineering_auth.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "inverter_manager.h"
#include "inverter_profile_store.h"
#include "inverter_profiles.h"
#include "meter_manager.h"
#include "solar_grid_config.h"

#define LOCAL_ENGINEERING_SESSION_MS (30ULL * 60ULL * 1000ULL)

static uint64_t s_unlocked_until_ms;
static bool s_setup_required;
static bool s_restart_required;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static bool unlocked(void)
{
    const uint64_t now = now_ms();
    if (s_unlocked_until_ms == 0U || now >= s_unlocked_until_ms) {
        s_unlocked_until_ms = 0U;
        return false;
    }
    return true;
}

static void result_set(screen_commission_action_result_t *result,
                       bool ok, bool restart_required, const char *message)
{
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->ok = ok;
    result->restart_required = restart_required;
    snprintf(result->message, sizeof(result->message), "%s", message ? message : "");
}

static bool require_unlocked(screen_commission_action_result_t *result)
{
    if (unlocked()) {
        s_unlocked_until_ms = now_ms() + LOCAL_ENGINEERING_SESSION_MS;
        return true;
    }
    result_set(result, false, false, "Engineering unlock expired. Unlock again.");
    return false;
}

static screen_commission_auth_result_t local_unlock(void *context,
                                                     const char *credential,
                                                     uint32_t *retry_after_ms,
                                                     bool *setup_required)
{
    (void)context;
    const engineering_local_auth_result_t auth =
        engineering_auth_verify_local_credential(credential, retry_after_ms, &s_setup_required);
    if (setup_required) *setup_required = s_setup_required;
    if (auth == ENGINEERING_LOCAL_AUTH_OK) {
        s_unlocked_until_ms = now_ms() + LOCAL_ENGINEERING_SESSION_MS;
        return SCREEN_COMMISSION_AUTH_OK;
    }
    s_unlocked_until_ms = 0U;
    if (auth == ENGINEERING_LOCAL_AUTH_LOCKED) return SCREEN_COMMISSION_AUTH_LOCKED;
    if (auth == ENGINEERING_LOCAL_AUTH_DENIED) return SCREEN_COMMISSION_AUTH_DENIED;
    return SCREEN_COMMISSION_AUTH_ERROR;
}

static void local_lock(void *context)
{
    (void)context;
    s_unlocked_until_ms = 0U;
}

static void copy_meter_to_screen(uint8_t index,
                                 const app_config_t *config,
                                 screen_commission_meter_t *out)
{
    memset(out, 0, sizeof(*out));
    out->port = 502U;
    out->unit_id = 1U;
    out->timeout_ms = 1000U;
    out->function_code = 3U;
    out->scale = 1.0f;
    out->generator_index = METER_GENERATOR_INDEX_NONE;
    if (!config || index >= config->meter_count || index >= APP_MAX_METERS) return;

    const meter_config_t *m = &config->meters[index];
    out->enabled = m->enabled;
    snprintf(out->name, sizeof(out->name), "%s", m->name);
    snprintf(out->host, sizeof(out->host), "%s", m->endpoint.host);
    out->port = m->endpoint.port;
    out->unit_id = m->endpoint.unit_id;
    out->timeout_ms = m->endpoint.timeout_ms;
    out->function_code = m->function_code;
    out->active_power_address = m->active_power_address;
    out->data_type = (uint8_t)m->active_power_type;
    out->word_order = (uint8_t)m->active_power_order;
    out->scale = m->active_power_scale;
    out->poll_ms = m->poll_interval_ms;
    out->role = (uint8_t)m->role;
    out->generator_index = m->generator_index;

    /* These frozen-HMI fields no longer exist in APP_CONFIG_VERSION 6. Keep
     * them visibly neutral rather than inventing a mapping into current Core. */
    out->model = 0U;
    out->phase_basis = 0U;

    meter_data_t data = {0};
    if (meter_manager_get_data(index, &data)) {
        out->runtime_online = data.online;
        out->runtime_has_data = data.success_count > 0U;
        out->runtime_stale = !data.online || data.consecutive_failures > 0U;
    }
}

static void copy_inverter_to_screen(uint8_t index,
                                    const app_config_t *config,
                                    screen_commission_inverter_t *out)
{
    memset(out, 0, sizeof(*out));
    out->port = 502U;
    out->unit_id = 1U;
    out->timeout_ms = 1000U;
    if (!config || index >= APP_MAX_INVERTERS) return;
    if (index < config->inverter_count) {
        const inverter_config_t *v = &config->inverters[index];
        out->enabled = v->enabled;
        snprintf(out->name, sizeof(out->name), "%s", v->name);
        snprintf(out->host, sizeof(out->host), "%s", v->endpoint.host);
        out->port = v->endpoint.port;
        out->unit_id = v->endpoint.unit_id;
        out->timeout_ms = v->endpoint.timeout_ms;
        out->rated_kw = v->rated_power_kw;
    }
    out->comms_failsafe_ms = 0U;
    (void)inverter_profile_store_get(index, out->profile_id, sizeof(out->profile_id));
    inverter_data_t data = {0};
    if (inverter_manager_get_data(index, &data)) {
        out->runtime_online = data.online;
        out->telemetry_valid = data.telemetry_valid;
        out->identity_verified = data.identity_verified;
    }
}

static void copy_plant_to_screen(const app_config_t *app,
                                 const solar_grid_config_t *solar,
                                 screen_commission_plant_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!app || !solar) return;

    out->policy = (uint8_t)solar->policy;
    out->meter_orientation = (uint8_t)solar->meter_orientation;
    out->export_limit_kw = solar->export_limit_kw;
    out->minimum_import_kw = solar->minimum_import_kw;

    for (uint8_t i = 0U;
         i < SCREEN_COMMISSIONING_MAX_GENERATORS && i < SOLAR_GRID_MAX_GENERATORS;
         ++i) {
        const solar_grid_generator_config_t *g = &solar->generators[i];
        out->generators[i].enabled = g->rated_kw > 0.0f;
        out->generators[i].rated_kw = g->rated_kw;
        out->generators[i].minimum_loading_percent = g->minimum_loading_percent;
        out->generators[i].reserve_kw = g->reserve_kw;
        out->generators[i].reverse_power_margin_kw = g->reverse_power_margin_kw;
        out->generators[i].role = 0U;
        out->generators[i].base_load_kw = 0.0f;
    }

    out->grid_import_target_kw = app->control.grid_import_target_kw;
    out->deadband_kw = app->control.deadband_kw;
    out->kp = app->control.kp;
    out->ki = app->control.ki;
    out->control_interval_ms = app->control.interval_ms;
    out->meter_stale_timeout_ms = app->control.meter_stale_timeout_ms;
    out->grid_ramp_enabled = app->control.grid_ramp.enabled;
    out->grid_ramp_up_percent_per_second = app->control.grid_ramp.up_percent_per_second;
    out->grid_ramp_down_percent_per_second = app->control.grid_ramp.down_percent_per_second;
    out->generator_ramp_enabled = app->control.generator_ramp.enabled;
    out->generator_ramp_up_percent_per_second = app->control.generator_ramp.up_percent_per_second;
    out->generator_ramp_down_percent_per_second = app->control.generator_ramp.down_percent_per_second;

    /* Removed Core semantics stay neutral on the frozen UI. */
    out->load_sharing_mode = 0U;
    out->base_load_tolerance_kw = 0.0f;
    out->base_load_tolerance_percent = 0.0f;
    out->urgent_loading_fraction = 0.0f;
    out->urgent_ramp_multiplier = 0.0f;
}

static bool local_read_config(void *context, screen_commissioning_config_t *out)
{
    (void)context;
    if (!out || !unlocked()) return false;

    app_config_t app = {0};
    solar_grid_config_t solar = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK ||
        solar_grid_config_get_snapshot(&solar) != ESP_OK) return false;

    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->unlocked = true;
    out->setup_required = s_setup_required;
    out->restart_required = s_restart_required;
    snprintf(out->device_name, sizeof(out->device_name), "%s", app.device_name);

    out->meter_count = app.meter_count;
    for (uint8_t i = 0U; i < SCREEN_COMMISSIONING_MAX_METERS; ++i) {
        copy_meter_to_screen(i, &app, &out->meters[i]);
    }

    out->inverter_count = app.inverter_count;
    for (uint8_t i = 0U; i < SCREEN_COMMISSIONING_MAX_INVERTERS; ++i) {
        copy_inverter_to_screen(i, &app, &out->inverters[i]);
    }

    const size_t available_profiles = inverter_profiles_count();
    out->profile_count = (uint8_t)(available_profiles < SCREEN_COMMISSIONING_MAX_PROFILES
                                       ? available_profiles
                                       : SCREEN_COMMISSIONING_MAX_PROFILES);
    for (uint8_t i = 0U; i < out->profile_count; ++i) {
        const inverter_profile_t *profile = inverter_profiles_get(i);
        if (!profile) continue;
        screen_commission_profile_t *p = &out->profiles[i];
        snprintf(p->id, sizeof(p->id), "%s", profile->id ? profile->id : "");
        snprintf(p->manufacturer, sizeof(p->manufacturer), "%s",
                 profile->manufacturer ? profile->manufacturer : "");
        snprintf(p->model, sizeof(p->model), "%s",
                 profile->model_family ? profile->model_family : "");
        p->read_allowed = inverter_profile_allows_read(profile);
        p->write_allowed = inverter_profile_allows_write(profile);
        p->deferred_this_phase = false;
    }

    copy_plant_to_screen(&app, &solar, &out->plant);
    s_unlocked_until_ms = now_ms() + LOCAL_ENGINEERING_SESSION_MS;
    return true;
}

static bool save_app_config_disabled(app_config_t *next,
                                     bool restart_required,
                                     const char *success,
                                     screen_commission_action_result_t *result)
{
    if (!next || !require_unlocked(result)) return false;

    /* Configuration changes never land under live command authority. */
    control_engine_force_disable();
    next->control.enabled = false;
    const esp_err_t err = config_manager_save(next);
    if (err != ESP_OK) {
        char text[128];
        snprintf(text, sizeof(text), "Core rejected configuration: %s", esp_err_to_name(err));
        result_set(result, false, false, text);
        return false;
    }
    if (restart_required) s_restart_required = true;
    result_set(result, true, restart_required, success);
    return true;
}

static bool local_save_site(void *context, const char *device_name,
                            screen_commission_action_result_t *result)
{
    (void)context;
    if (!require_unlocked(result)) return false;
    if (!device_name || !device_name[0] ||
        strlen(device_name) >= sizeof(((app_config_t *)0)->device_name)) {
        result_set(result, false, false, "Site/controller name must be 1-31 characters.");
        return false;
    }

    app_config_t next = {0};
    if (config_manager_get_snapshot(&next) != ESP_OK) {
        result_set(result, false, false, "Controller configuration is unavailable.");
        return false;
    }
    snprintf(next.device_name, sizeof(next.device_name), "%s", device_name);
    return save_app_config_disabled(&next, false,
                                    "Site/controller name saved. Automatic control is disabled.",
                                    result);
}

static void init_safe_meter_slot(meter_config_t *meter)
{
    memset(meter, 0, sizeof(*meter));
    meter->endpoint.port = 502U;
    meter->endpoint.unit_id = 1U;
    meter->endpoint.timeout_ms = 1000U;
    meter->function_code = 3U;
    meter->active_power_type = MODBUS_DATA_INT32;
    meter->active_power_order = MODBUS_ORDER_ABCD;
    meter->active_power_scale = 1.0f;
    meter->poll_interval_ms = 1000U;
    meter->role = METER_ROLE_UNASSIGNED;
    meter->generator_index = METER_GENERATOR_INDEX_NONE;
}

static bool local_save_meter(void *context, uint8_t index,
                             const screen_commission_meter_t *meter,
                             screen_commission_action_result_t *result)
{
    (void)context;
    if (!meter || index >= APP_MAX_METERS || !require_unlocked(result)) {
        if (meter && result && index >= APP_MAX_METERS) {
            result_set(result, false, false, "Meter slot is out of range.");
        }
        return false;
    }

    if (meter->model != 0U || meter->phase_basis != 0U) {
        result_set(result, false, false,
                   "Meter model/phase-basis fields are legacy UI fields and are not authoritative in current Core. Leave them at default before saving.");
        return false;
    }

    app_config_t next = {0};
    if (config_manager_get_snapshot(&next) != ESP_OK) {
        result_set(result, false, false, "Controller configuration is unavailable.");
        return false;
    }
    while (next.meter_count <= index && next.meter_count < APP_MAX_METERS) {
        init_safe_meter_slot(&next.meters[next.meter_count]);
        next.meter_count++;
    }

    meter_config_t *m = &next.meters[index];
    m->enabled = meter->enabled;
    snprintf(m->name, sizeof(m->name), "%s", meter->name);
    snprintf(m->endpoint.host, sizeof(m->endpoint.host), "%s", meter->host);
    m->endpoint.port = meter->port;
    m->endpoint.unit_id = meter->unit_id;
    m->endpoint.timeout_ms = meter->timeout_ms;
    m->function_code = meter->function_code;
    m->active_power_address = meter->active_power_address;
    m->active_power_type = (modbus_data_type_t)meter->data_type;
    m->active_power_order = (modbus_word_order_t)meter->word_order;
    m->active_power_scale = meter->scale;
    m->poll_interval_ms = meter->poll_ms;
    m->role = (meter_role_t)meter->role;
    m->generator_index = m->role == METER_ROLE_GENERATOR
                             ? meter->generator_index
                             : METER_GENERATOR_INDEX_NONE;

    return save_app_config_disabled(
        &next, true,
        "Meter saved. Automatic control is disabled; restart before qualification.",
        result);
}

static bool local_save_inverter(void *context, uint8_t index,
                                const screen_commission_inverter_t *inverter,
                                screen_commission_action_result_t *result)
{
    (void)context;
    if (!inverter || index >= APP_MAX_INVERTERS || !require_unlocked(result)) {
        if (inverter && result && index >= APP_MAX_INVERTERS) {
            result_set(result, false, false, "Inverter slot is out of range.");
        }
        return false;
    }

    if (inverter->comms_failsafe_ms != 0U) {
        result_set(result, false, false,
                   "Per-inverter comms fail-safe is a legacy UI field. Current Core owns telemetry staleness in the qualified profile/control path; leave this field at 0.");
        return false;
    }

    const inverter_profile_t *profile = inverter_profiles_find(inverter->profile_id);
    if (inverter->enabled && !profile) {
        result_set(result, false, false,
                   "Select a known inverter profile before enabling this inverter.");
        return false;
    }
    if (inverter->enabled && profile &&
        (!isfinite(profile->raw_units_per_percent) || profile->raw_units_per_percent <= 0.0f)) {
        result_set(result, false, false,
                   "Selected profile has no valid command scaling; Core configuration remains unchanged.");
        return false;
    }

    app_config_t next = {0};
    if (config_manager_get_snapshot(&next) != ESP_OK) {
        result_set(result, false, false, "Controller configuration is unavailable.");
        return false;
    }
    if (next.inverter_count <= index) next.inverter_count = index + 1U;

    inverter_config_t *v = &next.inverters[index];
    v->enabled = inverter->enabled;
    snprintf(v->name, sizeof(v->name), "%s", inverter->name);
    snprintf(v->endpoint.host, sizeof(v->endpoint.host), "%s", inverter->host);
    v->endpoint.port = inverter->port;
    v->endpoint.unit_id = inverter->unit_id;
    v->endpoint.timeout_ms = inverter->timeout_ms;
    v->rated_power_kw = inverter->rated_kw;
    if (profile) {
        v->power_limit_address = profile->power_limit_address;
        v->power_limit_function = profile->has_power_limit ? profile->power_limit_function : 0U;
        v->raw_units_per_percent = profile->raw_units_per_percent;
        v->minimum_percent = profile->minimum_percent;
        v->maximum_percent = profile->maximum_percent;
    }

    if (!save_app_config_disabled(&next, true,
                                  "Inverter endpoint saved. Applying profile assignment...",
                                  result)) {
        return false;
    }
    if (profile && inverter_profile_store_set(index, profile->id) != ESP_OK) {
        result_set(result, false, true,
                   "Endpoint was saved but profile assignment failed. Control remains disabled; fix profile before commissioning.");
        return false;
    }

    s_restart_required = true;
    result_set(result, true, true,
               "Inverter and profile saved. Automatic control is disabled; restart before qualification.");
    return true;
}

static bool legacy_plant_fields_clear(const screen_commission_plant_t *plant)
{
    if (!plant) return false;
    if (plant->load_sharing_mode != 0U ||
        plant->base_load_tolerance_kw != 0.0f ||
        plant->base_load_tolerance_percent != 0.0f ||
        plant->urgent_loading_fraction != 0.0f ||
        plant->urgent_ramp_multiplier != 0.0f) {
        return false;
    }
    for (uint8_t i = 0U; i < SCREEN_COMMISSIONING_MAX_GENERATORS; ++i) {
        if (plant->generators[i].role != 0U ||
            plant->generators[i].base_load_kw != 0.0f) return false;
    }
    return true;
}

static void sync_generator0_compat(solar_grid_config_t *solar)
{
    const solar_grid_generator_config_t *g = &solar->generators[0];
    solar->generator_rated_kw = g->rated_kw;
    solar->generator_minimum_loading_percent = g->minimum_loading_percent;
    solar->generator_reserve_kw = g->reserve_kw;
    solar->generator_reverse_power_margin_kw = g->reverse_power_margin_kw;
    solar->generator_running = g->running;
    solar->generator_breaker_closed = g->breaker_closed;
}

static bool plant_to_core(const screen_commission_plant_t *plant,
                          app_config_t *app,
                          solar_grid_config_t *solar,
                          screen_commission_action_result_t *result)
{
    if (!plant || !app || !solar) return false;
    if (!legacy_plant_fields_clear(plant)) {
        result_set(result, false, false,
                   "Load-sharing/base-load/urgent-ramp fields belong to the retired schema. Current Core does not accept guessed translations; leave those legacy fields at default.");
        return false;
    }

    solar->policy = (solar_grid_policy_t)plant->policy;
    solar->meter_orientation = (solar_grid_meter_orientation_t)plant->meter_orientation;
    solar->export_limit_kw = plant->export_limit_kw;
    solar->minimum_import_kw = plant->minimum_import_kw;

    for (uint8_t i = 0U;
         i < SCREEN_COMMISSIONING_MAX_GENERATORS && i < SOLAR_GRID_MAX_GENERATORS;
         ++i) {
        const screen_commission_generator_t *source = &plant->generators[i];
        solar_grid_generator_config_t *target = &solar->generators[i];
        target->rated_kw = source->enabled ? source->rated_kw : 0.0f;
        target->minimum_loading_percent = source->minimum_loading_percent;
        target->reserve_kw = source->reserve_kw;
        target->reverse_power_margin_kw = source->reverse_power_margin_kw;
        /* running/breaker evidence is deliberately preserved from the current
         * authoritative source model; this page never guesses signal mapping. */
    }
    sync_generator0_compat(solar);

    app->control.enabled = false;
    app->control.grid_import_target_kw = plant->grid_import_target_kw;
    app->control.deadband_kw = plant->deadband_kw;
    app->control.kp = plant->kp;
    app->control.ki = plant->ki;
    app->control.interval_ms = plant->control_interval_ms;
    app->control.meter_stale_timeout_ms = plant->meter_stale_timeout_ms;
    app->control.grid_ramp.enabled = plant->grid_ramp_enabled;
    app->control.grid_ramp.up_percent_per_second = plant->grid_ramp_up_percent_per_second;
    app->control.grid_ramp.down_percent_per_second = plant->grid_ramp_down_percent_per_second;
    app->control.generator_ramp.enabled = plant->generator_ramp_enabled;
    app->control.generator_ramp.up_percent_per_second =
        plant->generator_ramp_up_percent_per_second;
    app->control.generator_ramp.down_percent_per_second =
        plant->generator_ramp_down_percent_per_second;

    if (!solar_grid_config_valid(solar)) {
        result_set(result, false, false,
                   "Core rejected plant/source configuration values.");
        return false;
    }
    return true;
}

static bool local_save_plant(void *context, const screen_commission_plant_t *plant,
                             screen_commission_action_result_t *result)
{
    (void)context;
    if (!require_unlocked(result)) return false;

    app_config_t app = {0};
    solar_grid_config_t solar = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK ||
        solar_grid_config_get_snapshot(&solar) != ESP_OK) {
        result_set(result, false, false, "Plant configuration is unavailable.");
        return false;
    }
    if (!plant_to_core(plant, &app, &solar, result)) return false;

    /* Disable the running loop before either persistent model changes. A
     * partial save is allowed only in the fail-closed direction. */
    control_engine_force_disable();
    app.control.enabled = false;
    if (config_manager_save(&app) != ESP_OK) {
        result_set(result, false, false,
                   "Core rejected control/ramp configuration; nothing was armed.");
        return false;
    }
    if (solar_grid_config_save(&solar) != ESP_OK) {
        s_restart_required = true;
        result_set(result, false, true,
                   "Control parameters saved but plant source model failed to persist. Control remains disabled; review before restart.");
        return false;
    }

    s_restart_required = true;
    result_set(result, true, true,
               "Plant policy, generator protections and ramps saved. Control is disabled; restart required.");
    return true;
}

static bool local_set_control_enabled(void *context, bool enabled,
                                      screen_commission_action_result_t *result)
{
    (void)context;
    if (!require_unlocked(result)) return false;

    app_config_t app = {0};
    if (config_manager_get_snapshot(&app) != ESP_OK) {
        result_set(result, false, false,
                   "Controller configuration is unavailable; control state was not changed.");
        return false;
    }

    /* Current Core intentionally has no runtime-enable setter. Keep this
     * process fail-closed and persist the requested boot state. On enable, the
     * next boot starts in FAILSAFE with requested/applied PV at zero and only
     * obtains command authority if the Core's evidence gates pass. */
    control_engine_force_disable();
    app.control.enabled = enabled;
    if (config_manager_save(&app) != ESP_OK) {
        if (enabled) control_engine_force_disable();
        result_set(result, false, false,
                   enabled ? "Persistent arm failed; running control remains forced disabled."
                           : "Running control is disabled but persistent disable failed.");
        return false;
    }

    if (enabled) {
        s_restart_required = true;
        result_set(result, true, true,
                   "Automatic control armed for next restart. Runtime remains disabled now; after restart Core starts fail-safe and requires valid source/meter/control evidence before command authority.");
    } else {
        result_set(result, true, s_restart_required,
                   s_restart_required
                       ? "Automatic control disarmed and persisted. A restart is still required for earlier commissioning changes."
                       : "Automatic control disarmed and persisted.");
    }
    return true;
}

static bool local_restart_controller(void *context, screen_commission_action_result_t *result)
{
    (void)context;
    if (!require_unlocked(result)) return false;
    result_set(result, true, false, "Controller restarting...");
    esp_restart();
    return true;
}

bool local_commissioning_backend_init(screen_commissioning_backend_t *backend)
{
    if (!backend) return false;
    memset(backend, 0, sizeof(*backend));
    s_unlocked_until_ms = 0U;
    s_setup_required = false;
    s_restart_required = false;
    backend->unlock = local_unlock;
    backend->lock = local_lock;
    backend->read_config = local_read_config;
    backend->save_site = local_save_site;
    backend->save_meter = local_save_meter;
    backend->save_inverter = local_save_inverter;
    backend->save_plant = local_save_plant;
    backend->set_control_enabled = local_set_control_enabled;
    backend->restart_controller = local_restart_controller;
    return true;
}
