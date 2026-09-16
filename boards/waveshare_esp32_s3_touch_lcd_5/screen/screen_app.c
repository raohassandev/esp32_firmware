#include "screen_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pvdg_ui_engineering.h"
#include "pvdg_ui_grid.h"
#include "pvdg_ui_model.h"
#include "pvdg_ui_overview.h"
#include "pvdg_ui_pages.h"
#include "pvdg_ui_shell.h"
#include "pvdg_ui_solar.h"
#include "pvdg_ui_wifi.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *pages[SCREEN_PAGE_COUNT];
    screen_page_t active;
    pvdg_ui_shell_t shell;
    pvdg_ui_model_t model;

    screen_live_snapshot_t live;
    screen_status_snapshot_t status;
    screen_meters_snapshot_t meters;
    screen_inverters_snapshot_t inverters;
    screen_telemetry_snapshot_t telemetry;
    screen_commissioning_snapshot_t commissioning;
    screen_events_snapshot_t events;
    screen_alarms_snapshot_t alarms;
} screen_app_state_t;

static screen_app_state_t s_app;

static void copy_text(char *target, size_t capacity, const char *source)
{
    if (!target || capacity == 0U) return;
    snprintf(target, capacity, "%s", source ? source : "");
}

static bool text_equal_ci(const char *left, const char *right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

static const screen_meter_row_t *find_meter_role(const char *role)
{
    if (!s_app.meters.valid || !role) return NULL;
    for (size_t i = 0U; i < s_app.meters.row_count; ++i) {
        const screen_meter_row_t *row = &s_app.meters.rows[i];
        if (row->enabled && text_equal_ci(row->role_name, role)) return row;
    }
    return NULL;
}

static pvdg_ui_quality_t meter_quality(const screen_meter_row_t *row)
{
    if (!row || !row->has_power_kw) return PVDG_UI_QUALITY_UNAVAILABLE;
    if (row->stale) return PVDG_UI_QUALITY_STALE;
    if (!row->online) return PVDG_UI_QUALITY_UNAVAILABLE;
    return PVDG_UI_QUALITY_GOOD;
}

static void set_measurement(pvdg_ui_measurement_t *measurement,
                            bool available,
                            double value,
                            pvdg_ui_quality_t quality,
                            bool has_age,
                            uint32_t age_ms)
{
    if (!measurement) return;
    memset(measurement, 0, sizeof(*measurement));
    measurement->available = available;
    measurement->value = value;
    measurement->quality = available ? quality : PVDG_UI_QUALITY_UNAVAILABLE;
    measurement->has_age_ms = available && has_age;
    measurement->age_ms = available && has_age ? age_ms : 0U;
}

static void build_model(void)
{
    pvdg_ui_model_reset(&s_app.model);
    pvdg_ui_model_t *model = &s_app.model;

    model->valid = s_app.live.valid || s_app.status.valid || s_app.meters.valid ||
                   s_app.inverters.valid || s_app.telemetry.valid || s_app.alarms.valid;

    if (s_app.status.valid) {
        model->network.online = s_app.status.network_online;
        model->network.rssi = s_app.status.rssi;
    } else if (s_app.telemetry.valid) {
        model->network.online = s_app.telemetry.network_online;
        model->network.rssi = s_app.telemetry.rssi;
    }

    if (s_app.live.valid) {
        model->controller.enabled = s_app.live.control_enabled;
        copy_text(model->controller.inhibit_reason,
                  sizeof(model->controller.inhibit_reason),
                  s_app.live.inhibit_reason[0] ? s_app.live.inhibit_reason
                                               : s_app.live.command_blocked_by);
        set_measurement(&model->controller.requested_pv_kw,
                        s_app.live.has_requested_pv_kw,
                        s_app.live.requested_pv_kw,
                        PVDG_UI_QUALITY_GOOD,
                        false,
                        0U);
        set_measurement(&model->controller.applied_pv_kw,
                        s_app.live.has_applied_pv_kw,
                        s_app.live.applied_pv_kw,
                        PVDG_UI_QUALITY_GOOD,
                        false,
                        0U);
    }

    if (s_app.commissioning.valid) {
        model->controller.command_authority = s_app.commissioning.command_authority;
        copy_text(model->controller.authority_label,
                  sizeof(model->controller.authority_label),
                  s_app.commissioning.command_authority ? "Command authority" : "No command authority");
        if (!model->controller.inhibit_reason[0]) {
            copy_text(model->controller.inhibit_reason,
                      sizeof(model->controller.inhibit_reason),
                      s_app.commissioning.inhibit_reason);
        }
    } else {
        copy_text(model->controller.authority_label,
                  sizeof(model->controller.authority_label),
                  "Authority unknown");
    }

    const screen_meter_row_t *grid = find_meter_role("grid");
    if (grid) {
        const pvdg_ui_quality_t quality = meter_quality(grid);
        set_measurement(&model->grid.power_kw,
                        grid->has_power_kw,
                        grid->power_kw,
                        quality,
                        grid->has_data_age_ms,
                        grid->data_age_ms);
        model->grid.available = grid->online && grid->has_power_kw && !grid->stale;
        copy_text(model->grid.meter_name, sizeof(model->grid.meter_name), grid->name);
    } else if (s_app.live.valid && s_app.live.has_grid_kw) {
        pvdg_ui_quality_t quality = PVDG_UI_QUALITY_GOOD;
        if (s_app.status.valid) {
            if (s_app.status.meter_stale) quality = PVDG_UI_QUALITY_STALE;
            else if (!s_app.status.meter_online || !s_app.status.meter_has_data)
                quality = PVDG_UI_QUALITY_UNAVAILABLE;
        }
        set_measurement(&model->grid.power_kw,
                        true,
                        s_app.live.grid_kw,
                        quality,
                        false,
                        0U);
        model->grid.available = quality == PVDG_UI_QUALITY_GOOD;
    } else if (s_app.telemetry.valid && s_app.telemetry.has_grid_power_kw) {
        set_measurement(&model->grid.power_kw,
                        true,
                        s_app.telemetry.grid_power_kw,
                        PVDG_UI_QUALITY_GOOD,
                        false,
                        0U);
        model->grid.available = true;
    }

    if (s_app.live.valid && s_app.live.has_solar_kw) {
        set_measurement(&model->solar.measured_kw,
                        true,
                        s_app.live.solar_kw,
                        PVDG_UI_QUALITY_GOOD,
                        false,
                        0U);
    }

    if (s_app.inverters.valid) {
        model->solar.configured_count =
            (uint8_t)(s_app.inverters.configured_count > UINT8_MAX ? UINT8_MAX
                                                                   : s_app.inverters.configured_count);
        model->solar.online_count =
            (uint8_t)(s_app.inverters.online_count > UINT8_MAX ? UINT8_MAX
                                                               : s_app.inverters.online_count);
        const size_t count = s_app.inverters.row_count > PVDG_UI_MAX_INVERTERS
                                 ? PVDG_UI_MAX_INVERTERS
                                 : s_app.inverters.row_count;
        model->solar.inverter_count = (uint8_t)count;
        for (size_t i = 0U; i < count; ++i) {
            const screen_inverter_row_t *source = &s_app.inverters.rows[i];
            pvdg_ui_inverter_t *target = &model->solar.inverters[i];
            target->online = source->has_measured_power_kw;
            target->telemetry_valid = source->telemetry_supported && source->has_measured_power_kw;
            target->telemetry_stale = false;
            copy_text(target->name, sizeof(target->name), source->name);
            copy_text(target->status, sizeof(target->status), source->state);
            set_measurement(&target->measured_power_kw,
                            source->has_measured_power_kw,
                            source->measured_power_kw,
                            source->has_measured_power_kw ? PVDG_UI_QUALITY_GOOD
                                                          : PVDG_UI_QUALITY_UNAVAILABLE,
                            source->has_measured_age_ms,
                            source->measured_age_ms);
            /* commanded_percent is intentionally NOT mapped to readback_percent. */
            set_measurement(&target->readback_percent,
                            false,
                            0.0,
                            PVDG_UI_QUALITY_UNAVAILABLE,
                            false,
                            0U);
        }
    }

    bool generator_seen = false;
    bool generator_all_good = true;
    double generator_total_kw = 0.0;
    if (s_app.meters.valid) {
        for (size_t i = 0U; i < s_app.meters.row_count; ++i) {
            const screen_meter_row_t *row = &s_app.meters.rows[i];
            if (!row->enabled || !text_equal_ci(row->role_name, "generator")) continue;
            generator_seen = true;
            if (!row->online || row->stale || !row->has_power_kw) {
                generator_all_good = false;
                continue;
            }
            generator_total_kw += row->power_kw;
        }
    }
    set_measurement(&model->generator.measured_kw,
                    generator_seen && generator_all_good,
                    generator_total_kw,
                    generator_seen && generator_all_good ? PVDG_UI_QUALITY_GOOD
                                                         : PVDG_UI_QUALITY_UNAVAILABLE,
                    false,
                    0U);
    /* Running/breaker evidence is not inferred from kW. */
    model->generator.evidence_configured = false;
    model->generator.breaker_known = false;
    model->generator.running = false;
    model->generator.running_count = 0U;

    const screen_meter_row_t *load = find_meter_role("load");
    if (load && load->has_power_kw) {
        set_measurement(&model->load.power_kw,
                        true,
                        load->power_kw,
                        meter_quality(load),
                        load->has_data_age_ms,
                        load->data_age_ms);
        model->load.derived = false;
    } else if (model->solar.measured_kw.available &&
               model->generator.measured_kw.available &&
               model->grid.power_kw.available &&
               model->solar.measured_kw.quality == PVDG_UI_QUALITY_GOOD &&
               model->generator.measured_kw.quality == PVDG_UI_QUALITY_GOOD &&
               model->grid.power_kw.quality == PVDG_UI_QUALITY_GOOD) {
        set_measurement(&model->load.power_kw,
                        true,
                        model->solar.measured_kw.value +
                            model->generator.measured_kw.value +
                            model->grid.power_kw.value,
                        PVDG_UI_QUALITY_GOOD,
                        false,
                        0U);
        model->load.derived = true;
    }

    model->export_policy.kind = PVDG_UI_EXPORT_POLICY_UNKNOWN;
    copy_text(model->export_policy.label,
              sizeof(model->export_policy.label),
              "Policy unavailable");

    if (s_app.status.valid) {
        model->alarm_flags = s_app.status.alarms;
        model->alarm_count =
            (uint8_t)(s_app.status.alarm_name_count > PVDG_UI_MAX_ALARMS
                          ? PVDG_UI_MAX_ALARMS
                          : s_app.status.alarm_name_count);
        for (uint8_t i = 0U; i < model->alarm_count; ++i) {
            copy_text(model->alarms[i], sizeof(model->alarms[i]), s_app.status.alarm_names[i]);
        }
    } else if (s_app.alarms.valid) {
        uint8_t count = 0U;
        for (size_t i = 0U; i < s_app.alarms.row_count && count < PVDG_UI_MAX_ALARMS; ++i) {
            const screen_alarm_row_t *row = &s_app.alarms.rows[i];
            if (!row->present) continue;
            copy_text(model->alarms[count], sizeof(model->alarms[count]), row->title);
            ++count;
        }
        model->alarm_count = count;
    }
}

static pvdg_ui_route_t route_for_page(screen_page_t page)
{
    switch (page) {
    case SCREEN_PAGE_GRID: return PVDG_UI_ROUTE_GRID;
    case SCREEN_PAGE_SOLAR: return PVDG_UI_ROUTE_SOLAR;
    case SCREEN_PAGE_GENERATOR: return PVDG_UI_ROUTE_GENERATOR;
    case SCREEN_PAGE_LOAD: return PVDG_UI_ROUTE_LOAD;
    case SCREEN_PAGE_REPORTS: return PVDG_UI_ROUTE_REPORTS;
    case SCREEN_PAGE_WIFI: return PVDG_UI_ROUTE_WIFI;
    case SCREEN_PAGE_ENGINEERING: return PVDG_UI_ROUTE_ENGINEERING;
    case SCREEN_PAGE_OVERVIEW:
    case SCREEN_PAGE_COUNT:
    default: return PVDG_UI_ROUTE_OVERVIEW;
    }
}

static screen_page_t page_for_route(pvdg_ui_route_t route)
{
    switch (route) {
    case PVDG_UI_ROUTE_GRID: return SCREEN_PAGE_GRID;
    case PVDG_UI_ROUTE_SOLAR: return SCREEN_PAGE_SOLAR;
    case PVDG_UI_ROUTE_GENERATOR: return SCREEN_PAGE_GENERATOR;
    case PVDG_UI_ROUTE_LOAD: return SCREEN_PAGE_LOAD;
    case PVDG_UI_ROUTE_REPORTS: return SCREEN_PAGE_REPORTS;
    case PVDG_UI_ROUTE_WIFI: return SCREEN_PAGE_WIFI;
    case PVDG_UI_ROUTE_ENGINEERING: return SCREEN_PAGE_ENGINEERING;
    case PVDG_UI_ROUTE_OVERVIEW:
    default: return SCREEN_PAGE_OVERVIEW;
    }
}

static void render_active(void)
{
    if (!s_app.root) return;
    pvdg_ui_shell_apply_model(&s_app.model);

    switch (s_app.active) {
    case SCREEN_PAGE_OVERVIEW:
        pvdg_ui_overview_apply_model(&s_app.model);
        break;
    case SCREEN_PAGE_GRID:
        pvdg_ui_grid_apply_model(&s_app.model);
        break;
    case SCREEN_PAGE_SOLAR:
        pvdg_ui_solar_apply_model(&s_app.model);
        break;
    case SCREEN_PAGE_GENERATOR:
        pvdg_ui_generator_apply_model(&s_app.model);
        break;
    case SCREEN_PAGE_LOAD:
        pvdg_ui_load_apply_model(&s_app.model);
        break;
    case SCREEN_PAGE_REPORTS:
        pvdg_ui_reports_apply_model(&s_app.model);
        pvdg_ui_reports_apply_em500_statistics(NULL);
        break;
    case SCREEN_PAGE_WIFI:
        pvdg_ui_wifi_apply(&s_app.model, NULL);
        break;
    case SCREEN_PAGE_ENGINEERING: {
        pvdg_ui_engineering_state_t state = {
            .authenticated = false,
            .setup_required = false,
            .password_change_recommended = false,
            .locked_out = false,
            .write_contract_verified = false,
            .session_remaining_seconds = 0U,
            .lockout_remaining_seconds = 0U,
            .session_timeout_minutes = 0U,
            .security_state = "Protected",
            .message = "Engineering writes remain protected by the existing commissioning workflow.",
        };
        pvdg_ui_engineering_apply(&s_app.model, &state);
        break;
    }
    case SCREEN_PAGE_COUNT:
    default:
        break;
    }
}

static void rebuild_and_render(void)
{
    build_model();
    render_active();
}

static void route_selected(pvdg_ui_route_t route, void *user)
{
    (void)user;
    screen_app_show_page(page_for_route(route));
}

lv_obj_t *screen_app_create(lv_obj_t *parent)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.active = SCREEN_PAGE_OVERVIEW;
    pvdg_ui_model_reset(&s_app.model);

    const pvdg_ui_shell_callbacks_t callbacks = {
        .on_route = route_selected,
        .user = NULL,
    };
    s_app.shell = pvdg_ui_shell_create(parent ? parent : lv_screen_active(), &callbacks);
    s_app.root = s_app.shell.root;
    if (!s_app.root || !s_app.shell.content) return NULL;

    s_app.pages[SCREEN_PAGE_OVERVIEW] = pvdg_ui_overview_create(s_app.shell.content);
    s_app.pages[SCREEN_PAGE_GRID] = pvdg_ui_grid_create(s_app.shell.content);
    s_app.pages[SCREEN_PAGE_SOLAR] = pvdg_ui_solar_create(s_app.shell.content);
    s_app.pages[SCREEN_PAGE_GENERATOR] = pvdg_ui_generator_create(s_app.shell.content);
    s_app.pages[SCREEN_PAGE_LOAD] = pvdg_ui_load_create(s_app.shell.content);
    s_app.pages[SCREEN_PAGE_REPORTS] = pvdg_ui_reports_create(s_app.shell.content);
    s_app.pages[SCREEN_PAGE_WIFI] = pvdg_ui_wifi_create(s_app.shell.content, NULL);
    s_app.pages[SCREEN_PAGE_ENGINEERING] = pvdg_ui_engineering_create(s_app.shell.content, NULL);

    for (int i = 0; i < (int)SCREEN_PAGE_COUNT; ++i) {
        if (!s_app.pages[i]) return NULL;
        if (i != (int)SCREEN_PAGE_OVERVIEW) lv_obj_add_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    pvdg_ui_wifi_set_config_snapshot(NULL, false);
    pvdg_ui_shell_set_active_route(PVDG_UI_ROUTE_OVERVIEW);
    pvdg_ui_shell_set_clock("--:--", "-- --- ----");
    rebuild_and_render();
    return s_app.root;
}

void screen_app_show_page(screen_page_t page)
{
    if (!s_app.root || (unsigned)page >= (unsigned)SCREEN_PAGE_COUNT) return;
    for (int i = 0; i < (int)SCREEN_PAGE_COUNT; ++i) {
        if (!s_app.pages[i]) continue;
        if (i == (int)page) lv_obj_remove_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_app.pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_app.active = page;
    pvdg_ui_shell_set_active_route(route_for_page(page));
    render_active();
}

screen_page_t screen_app_get_active_page(void)
{
    return s_app.active;
}

void screen_app_set_commissioning_backend(const screen_commissioning_backend_t *backend)
{
    /* Preserve the existing authenticated backend contract. The new runtime
     * Engineering surface does not call it until the native write host is mapped. */
    commissioning_screen_set_backend(backend);
}

void screen_app_set_source_commissioning_backend(const source_commission_backend_t *backend)
{
    source_commissioning_screen_set_backend(backend);
}

void screen_app_apply_live(const screen_live_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.live = *snapshot;
    else memset(&s_app.live, 0, sizeof(s_app.live));
    rebuild_and_render();
}

void screen_app_apply_status(const screen_status_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.status = *snapshot;
    else memset(&s_app.status, 0, sizeof(s_app.status));
    rebuild_and_render();
}

void screen_app_apply_meters(const screen_meters_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.meters = *snapshot;
    else memset(&s_app.meters, 0, sizeof(s_app.meters));
    rebuild_and_render();
}

void screen_app_apply_inverters(const screen_inverters_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.inverters = *snapshot;
    else memset(&s_app.inverters, 0, sizeof(s_app.inverters));
    rebuild_and_render();
}

void screen_app_apply_telemetry(const screen_telemetry_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.telemetry = *snapshot;
    else memset(&s_app.telemetry, 0, sizeof(s_app.telemetry));
    rebuild_and_render();
}

void screen_app_apply_commissioning(const screen_commissioning_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.commissioning = *snapshot;
    else memset(&s_app.commissioning, 0, sizeof(s_app.commissioning));
    rebuild_and_render();
}

void screen_app_apply_events(const screen_events_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.events = *snapshot;
    else memset(&s_app.events, 0, sizeof(s_app.events));
}

void screen_app_apply_alarms(const screen_alarms_snapshot_t *snapshot)
{
    if (snapshot && snapshot->valid) s_app.alarms = *snapshot;
    else memset(&s_app.alarms, 0, sizeof(s_app.alarms));
    rebuild_and_render();
}

void screen_app_show_live_unavailable(void)
{
    memset(&s_app.live, 0, sizeof(s_app.live));
    rebuild_and_render();
}

void screen_app_show_meters_unavailable(void)
{
    memset(&s_app.meters, 0, sizeof(s_app.meters));
    rebuild_and_render();
}

void screen_app_show_inverters_unavailable(void)
{
    memset(&s_app.inverters, 0, sizeof(s_app.inverters));
    rebuild_and_render();
}

void screen_app_show_operations_unavailable(void)
{
    memset(&s_app.events, 0, sizeof(s_app.events));
    memset(&s_app.alarms, 0, sizeof(s_app.alarms));
    rebuild_and_render();
}

void screen_app_show_readiness_unavailable(void)
{
    memset(&s_app.status, 0, sizeof(s_app.status));
    memset(&s_app.telemetry, 0, sizeof(s_app.telemetry));
    rebuild_and_render();
}

void screen_app_show_commissioning_unavailable(void)
{
    memset(&s_app.commissioning, 0, sizeof(s_app.commissioning));
    rebuild_and_render();
}

void screen_app_show_backend_unavailable(void)
{
    memset(&s_app.live, 0, sizeof(s_app.live));
    memset(&s_app.status, 0, sizeof(s_app.status));
    memset(&s_app.meters, 0, sizeof(s_app.meters));
    memset(&s_app.inverters, 0, sizeof(s_app.inverters));
    memset(&s_app.telemetry, 0, sizeof(s_app.telemetry));
    memset(&s_app.commissioning, 0, sizeof(s_app.commissioning));
    memset(&s_app.events, 0, sizeof(s_app.events));
    memset(&s_app.alarms, 0, sizeof(s_app.alarms));
    rebuild_and_render();
}
