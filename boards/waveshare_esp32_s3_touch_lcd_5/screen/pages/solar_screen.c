#include "solar_screen.h"

#include <stdio.h>

#include "screen_widgets.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *summary;
    lv_obj_t *capacity;
    lv_obj_t *list;
} solar_ui_t;

static solar_ui_t s_ui;

lv_obj_t *solar_screen_create(lv_obj_t *parent)
{
    s_ui.root = screen_ui_panel(parent);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 10, LV_PART_MAIN);

    screen_ui_title(s_ui.root, "Solar / Inverters");
    s_ui.summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/inverters");
    s_ui.capacity = screen_ui_muted_label(s_ui.root, "Capacity: --");

    s_ui.list = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.list);
    lv_obj_set_width(s_ui.list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ui.list, 1);
    lv_obj_set_layout(s_ui.list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.list, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_ui.list, LV_DIR_VER);
    return s_ui.root;
}

void solar_screen_apply(const screen_inverters_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    lv_label_set_text_fmt(s_ui.summary,
                          "%lu configured | %lu enabled | %lu online%s",
                          (unsigned long)snapshot->configured_count,
                          (unsigned long)snapshot->enabled_count,
                          (unsigned long)snapshot->online_count,
                          snapshot->truncated ? " | list truncated" : "");

    if (snapshot->has_enabled_rated_kw || snapshot->has_commandable_rated_kw) {
        lv_label_set_text_fmt(s_ui.capacity, "Enabled %.1f kW | Commandable %.1f kW",
                              snapshot->has_enabled_rated_kw ? snapshot->enabled_rated_kw : 0.0,
                              snapshot->has_commandable_rated_kw ? snapshot->commandable_rated_kw : 0.0);
    } else {
        lv_label_set_text(s_ui.capacity, "Capacity: unavailable");
    }

    lv_obj_clean(s_ui.list);
    if (snapshot->row_count == 0U) {
        screen_ui_muted_label(s_ui.list, "No inverters configured");
        return;
    }

    for (size_t i = 0; i < snapshot->row_count; ++i) {
        const screen_inverter_row_t *row = &snapshot->rows[i];
        lv_obj_t *panel = screen_ui_panel(s_ui.list);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 4, LV_PART_MAIN);

        char heading[96];
        snprintf(heading, sizeof(heading), "%u  %s",
                 (unsigned)row->index,
                 screen_ui_safe_text(row->name, "Unnamed inverter"));
        screen_ui_title(panel, heading);

        lv_obj_t *line = lv_obj_create(panel);
        lv_obj_remove_style_all(line);
        lv_obj_set_width(line, LV_PCT(100));
        lv_obj_set_height(line, LV_SIZE_CONTENT);
        lv_obj_set_layout(line, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *state = screen_ui_value_label(line, screen_ui_safe_text(row->state, "unknown"));
        screen_ui_set_state_text(state, screen_ui_safe_text(row->state, "unknown"),
                                 row->enabled && row->state[0] != '\0' &&
                                 row->state[0] != 'i');
        lv_obj_t *power = screen_ui_value_label(line, "-- kW");
        screen_ui_set_kw(power, row->has_measured_power_kw, row->measured_power_kw);

        if (!row->telemetry_supported) {
            screen_ui_muted_label(panel, "Measured power not supported by current profile");
        } else if (row->has_measured_age_ms) {
            char age[64];
            snprintf(age, sizeof(age), "Measured age: %lu ms", (unsigned long)row->measured_age_ms);
            screen_ui_muted_label(panel, age);
        } else {
            screen_ui_muted_label(panel, "Measured power unavailable");
        }

        if (row->has_commanded_percent) {
            char command[64];
            snprintf(command, sizeof(command), "Last commanded: %.0f%%", row->commanded_percent);
            screen_ui_muted_label(panel, command);
        }
    }
}

void solar_screen_show_unavailable(void)
{
    if (!s_ui.root) return;
    lv_label_set_text(s_ui.summary, "Inverter backend unavailable");
    lv_label_set_text(s_ui.capacity, "Capacity: unavailable");
    lv_obj_clean(s_ui.list);
    screen_ui_muted_label(s_ui.list, "Solar measurement is unknown; no zero output is assumed.");
}
