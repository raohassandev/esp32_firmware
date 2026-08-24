#include "alarms_screen.h"

#include <stdio.h>

#include "screen_widgets.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *alarm_summary;
    lv_obj_t *alarm_list;
    lv_obj_t *event_summary;
    lv_obj_t *event_list;
} alarms_ui_t;

static alarms_ui_t s_ui;

static lv_obj_t *make_scroll_list(lv_obj_t *parent, int height)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, height);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    return list;
}

lv_obj_t *alarms_screen_create(lv_obj_t *parent)
{
    s_ui.root = screen_ui_panel(parent);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_ui.root, LV_DIR_VER);

    screen_ui_title(s_ui.root, "Alarms / Events");
    s_ui.alarm_summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/operator/alarms");
    s_ui.alarm_list = make_scroll_list(s_ui.root, 190);
    s_ui.event_summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/operator/events");
    s_ui.event_list = make_scroll_list(s_ui.root, 190);
    return s_ui.root;
}

void alarms_screen_apply_alarms(const screen_alarms_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    lv_label_set_text_fmt(s_ui.alarm_summary,
                          "Primary active %lu | Consequential %lu | Unacknowledged %lu%s",
                          (unsigned long)snapshot->primary_active_count,
                          (unsigned long)snapshot->consequential_active_count,
                          (unsigned long)snapshot->unacknowledged_count,
                          snapshot->truncated ? " | list truncated" : "");
    lv_obj_clean(s_ui.alarm_list);

    if (snapshot->row_count == 0U) {
        screen_ui_muted_label(s_ui.alarm_list, "No alarm conditions recorded");
        return;
    }

    for (size_t i = 0; i < snapshot->row_count; ++i) {
        const screen_alarm_row_t *row = &snapshot->rows[i];
        lv_obj_t *panel = screen_ui_panel(s_ui.alarm_list);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, LV_PART_MAIN);

        char heading[160];
        snprintf(heading, sizeof(heading), "%s  %s  [%s/%s]",
                 screen_ui_safe_text(row->id, "ALARM"),
                 screen_ui_safe_text(row->title, "Unnamed alarm"),
                 screen_ui_safe_text(row->priority, "priority unknown"),
                 screen_ui_safe_text(row->state, "state unknown"));
        screen_ui_title(panel, heading);

        char detail[192];
        snprintf(detail, sizeof(detail), "Role: %s%s%s%s",
                 screen_ui_safe_text(row->role, "unknown"),
                 row->caused_by[0] ? " | caused by " : "",
                 row->caused_by[0] ? row->caused_by : "",
                 row->stale ? " | STALE" : "");
        screen_ui_muted_label(panel, detail);

        if (row->shelved || row->suppressed_by_design || row->out_of_service) {
            char suppression[160];
            snprintf(suppression, sizeof(suppression), "Suppression:%s%s%s",
                     row->shelved ? " shelved" : "",
                     row->suppressed_by_design ? " by-design" : "",
                     row->out_of_service ? " out-of-service" : "");
            screen_ui_muted_label(panel, suppression);
        }

        screen_ui_muted_label(panel,
                              screen_ui_safe_text(row->recommended_action,
                                                  "No recommended action published"));
    }
}

void alarms_screen_apply_events(const screen_events_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    lv_label_set_text_fmt(s_ui.event_summary,
                          "Recent events | active critical %lu | warning %lu | stored %lu%s",
                          (unsigned long)snapshot->active_critical,
                          (unsigned long)snapshot->active_warning,
                          (unsigned long)snapshot->stored_events,
                          snapshot->truncated ? " | list truncated" : "");
    lv_obj_clean(s_ui.event_list);

    if (snapshot->row_count == 0U) {
        screen_ui_muted_label(s_ui.event_list, "No events recorded");
        return;
    }

    for (size_t i = 0; i < snapshot->row_count; ++i) {
        const screen_event_row_t *row = &snapshot->rows[i];
        lv_obj_t *panel = screen_ui_panel(s_ui.event_list);
        lv_obj_set_width(panel, LV_PCT(100));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, LV_PART_MAIN);

        char heading[160];
        snprintf(heading, sizeof(heading), "%s | %s | %s",
                 screen_ui_safe_text(row->severity, "unknown"),
                 screen_ui_safe_text(row->state, "unknown"),
                 screen_ui_safe_text(row->title, "Event"));
        screen_ui_title(panel, heading);

        char age[48];
        snprintf(age, sizeof(age), "Age: %lu ms", (unsigned long)row->age_ms);
        screen_ui_muted_label(panel, age);
        screen_ui_muted_label(panel, screen_ui_safe_text(row->detail, "No detail"));
    }
}

void alarms_screen_show_unavailable(void)
{
    if (!s_ui.root) return;
    lv_label_set_text(s_ui.alarm_summary, "Alarm backend unavailable");
    lv_label_set_text(s_ui.event_summary, "Event backend unavailable");
    lv_obj_clean(s_ui.alarm_list);
    lv_obj_clean(s_ui.event_list);
    screen_ui_muted_label(s_ui.alarm_list, "Alarm state unknown");
    screen_ui_muted_label(s_ui.event_list, "Event history unavailable");
}
