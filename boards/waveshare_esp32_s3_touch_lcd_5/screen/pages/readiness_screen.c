#include "readiness_screen.h"

#include <stdio.h>
#include <string.h>

#include "screen_widgets.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *commissioning;
    lv_obj_t *commissioning_scope;
    lv_obj_t *commissioning_progress;
    lv_obj_t *automatic_permitted;
    lv_obj_t *commissioning_blocker;
    lv_obj_t *monitoring;
    lv_obj_t *command_path;
    lv_obj_t *automatic;
    lv_obj_t *network;
    lv_obj_t *meter;
    lv_obj_t *inverters;
    lv_obj_t *controller;
    lv_obj_t *source;
    lv_obj_t *authority;
} readiness_ui_t;

static readiness_ui_t s_ui;

static void set_blocker(const char *text, bool healthy)
{
    if (!s_ui.commissioning_blocker) return;
    const char *next = text && text[0] ? text : "Commissioning state unavailable";
    const char *current = lv_label_get_text(s_ui.commissioning_blocker);
    if (!current || strcmp(current, next) != 0) lv_label_set_text(s_ui.commissioning_blocker, next);
    lv_obj_set_style_text_color(s_ui.commissioning_blocker,
                                lv_color_hex(healthy ? 0x62D28F : 0xF2B84B), LV_PART_MAIN);
}

lv_obj_t *readiness_screen_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.root = screen_ui_panel(parent);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 5, LV_PART_MAIN);

    screen_ui_title(s_ui.root, "Site Commissioning / Readiness");
    screen_ui_row(s_ui.root, "Commissioning gate", &s_ui.commissioning);
    screen_ui_row(s_ui.root, "Commissioning scope", &s_ui.commissioning_scope);
    screen_ui_row(s_ui.root, "Prerequisites", &s_ui.commissioning_progress);
    screen_ui_row(s_ui.root, "Automatic permitted", &s_ui.automatic_permitted);

    screen_ui_muted_label(s_ui.root, "Next commissioning blocker");
    s_ui.commissioning_blocker = screen_ui_value_label(s_ui.root, "Reading controller gate...");
    lv_obj_set_width(s_ui.commissioning_blocker, LV_PCT(100));
    lv_label_set_long_mode(s_ui.commissioning_blocker, LV_LABEL_LONG_WRAP);

    lv_obj_t *hint = screen_ui_muted_label(
        s_ui.root,
        "Configure site: Engineering web > Commissioning. This HMI mirrors the Core gate; it does not bypass Engineering authentication.");
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);

    screen_ui_row(s_ui.root, "Monitoring ready", &s_ui.monitoring);
    screen_ui_row(s_ui.root, "Command path ready", &s_ui.command_path);
    screen_ui_row(s_ui.root, "Automatic control", &s_ui.automatic);
    screen_ui_row(s_ui.root, "Network", &s_ui.network);
    screen_ui_row(s_ui.root, "Grid meter", &s_ui.meter);
    screen_ui_row(s_ui.root, "Inverters", &s_ui.inverters);
    screen_ui_row(s_ui.root, "Controller", &s_ui.controller);
    screen_ui_row(s_ui.root, "Active source", &s_ui.source);
    screen_ui_row(s_ui.root, "Control authority", &s_ui.authority);
    return s_ui.root;
}

void readiness_screen_apply_commissioning(const screen_commissioning_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    screen_ui_set_state_text(s_ui.commissioning,
                             snapshot->commissioned ? "Commissioned" : "Not commissioned",
                             snapshot->commissioned);
    screen_ui_set_state_text(s_ui.commissioning_scope,
                             screen_ui_safe_text(snapshot->scope, "none"),
                             snapshot->production_qualified);

    char progress[64];
    snprintf(progress, sizeof(progress), "%lu/%lu met / %lu unmet",
             (unsigned long)snapshot->satisfied_count,
             (unsigned long)snapshot->prerequisite_count,
             (unsigned long)snapshot->unmet_count);
    screen_ui_set_state_text(s_ui.commissioning_progress, progress, snapshot->commissioned);
    screen_ui_set_state_text(s_ui.automatic_permitted,
                             snapshot->automatic_control_permitted ? "Permitted" : "Blocked",
                             snapshot->automatic_control_permitted);

    if (snapshot->commissioned) {
        set_blocker("All commissioning prerequisites satisfied. Runtime evidence is still checked every control cycle.",
                    true);
    } else if (snapshot->first_unmet_title[0] != '\0') {
        char blocker[256];
        snprintf(blocker, sizeof(blocker), "%s%s%s",
                 snapshot->first_unmet_title,
                 snapshot->first_unmet_detail[0] != '\0' ? ": " : "",
                 snapshot->first_unmet_detail);
        set_blocker(blocker, false);
    } else {
        set_blocker(screen_ui_safe_text(snapshot->summary, snapshot->inhibit_reason), false);
    }
}

void readiness_screen_apply(const screen_telemetry_snapshot_t *snapshot,
                            const screen_status_snapshot_t *status)
{
    if (!s_ui.root) return;

    if (snapshot && snapshot->valid) {
        screen_ui_set_state_text(s_ui.monitoring,
                                 snapshot->monitoring_ready ? "Ready" : "Not ready",
                                 snapshot->monitoring_ready);
        screen_ui_set_state_text(s_ui.command_path,
                                 snapshot->command_path_ready ? "Ready" : "Not ready",
                                 snapshot->command_path_ready);
        screen_ui_set_state_text(s_ui.automatic,
                                 snapshot->automatic_control_active ? "Active" : "Not active",
                                 snapshot->automatic_control_active);

        char network[64];
        snprintf(network, sizeof(network), "%s / %d dBm",
                 snapshot->network_online ? "Online" : "Offline", snapshot->rssi);
        screen_ui_set_state_text(s_ui.network, network, snapshot->network_online);

        char meter[96];
        if (snapshot->has_grid_power_kw) {
            snprintf(meter, sizeof(meter), "%s / %.1f kW",
                     screen_ui_safe_text(snapshot->grid_state, "unknown"),
                     snapshot->grid_power_kw);
        } else {
            snprintf(meter, sizeof(meter), "%s / power unavailable",
                     screen_ui_safe_text(snapshot->grid_state, "unknown"));
        }
        screen_ui_set_state_text(s_ui.meter, meter,
                                 snapshot->monitoring_ready && snapshot->has_grid_power_kw);

        char inverters[96];
        snprintf(inverters, sizeof(inverters), "%lu configured / %lu enabled / init failures %lu",
                 (unsigned long)snapshot->inverters_configured,
                 (unsigned long)snapshot->inverters_enabled,
                 (unsigned long)snapshot->inverters_initialization_failed);
        screen_ui_set_state_text(s_ui.inverters, inverters,
                                 snapshot->inverters_initialization_failed == 0U);
    }

    if (status && status->valid) {
        char controller[96];
        snprintf(controller, sizeof(controller), "%s%s",
                 screen_ui_safe_text(status->controller_state, "unknown"),
                 status->last_reboot_unexpected ? " / unexpected reboot" : "");
        screen_ui_set_state_text(s_ui.controller, controller,
                                 !status->last_reboot_unexpected &&
                                 status->controller_state[0] != '\0');

        const bool source_known = status->source_attributed_to[0] != '\0' &&
                                  strcmp(status->source_attributed_to, "unknown") != 0;
        screen_ui_set_state_text(s_ui.source,
                                 screen_ui_safe_text(status->source_attributed_to, "unknown"),
                                 source_known);
        screen_ui_set_state_text(s_ui.authority,
                                 screen_ui_safe_text(status->control_mode_label, "unknown"),
                                 status->control_mode_label[0] != '\0');
    }
}

void readiness_screen_show_unavailable(void)
{
    if (!s_ui.root) return;
    screen_ui_set_state_text(s_ui.monitoring, "Unknown", false);
    screen_ui_set_state_text(s_ui.command_path, "Unknown", false);
    screen_ui_set_state_text(s_ui.automatic, "Unknown", false);
    screen_ui_set_state_text(s_ui.network, "Unavailable", false);
    screen_ui_set_state_text(s_ui.meter, "Unavailable", false);
    screen_ui_set_state_text(s_ui.inverters, "Unavailable", false);
    screen_ui_set_state_text(s_ui.controller, "Unavailable", false);
    screen_ui_set_state_text(s_ui.source, "Unknown", false);
    screen_ui_set_state_text(s_ui.authority, "Unknown", false);
}

void readiness_screen_show_commissioning_unavailable(void)
{
    if (!s_ui.root) return;
    screen_ui_set_state_text(s_ui.commissioning, "Unavailable", false);
    screen_ui_set_state_text(s_ui.commissioning_scope, "Unknown", false);
    screen_ui_set_state_text(s_ui.commissioning_progress, "Unknown", false);
    screen_ui_set_state_text(s_ui.automatic_permitted, "Unknown", false);
    set_blocker("Commissioning gate unavailable", false);
}
