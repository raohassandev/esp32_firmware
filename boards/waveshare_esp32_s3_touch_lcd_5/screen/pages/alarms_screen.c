#include "alarms_screen.h"

#include <stdio.h>

#include "screen_widgets.h"

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *heading;
    lv_obj_t *detail;
    lv_obj_t *suppression;
    lv_obj_t *action;
} alarm_row_ui_t;

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *heading;
    lv_obj_t *age;
    lv_obj_t *detail;
} event_row_ui_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *alarm_summary;
    lv_obj_t *alarm_list;
    lv_obj_t *alarm_empty;
    alarm_row_ui_t alarms[SCREEN_API_MAX_ALARMS];
    lv_obj_t *event_summary;
    lv_obj_t *event_list;
    lv_obj_t *event_empty;
    event_row_ui_t events[SCREEN_API_MAX_EVENTS];
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

static void set_visible(lv_obj_t *obj, bool visible)
{
    if (!obj) return;
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/* Row labels are clipped to a fixed width instead of wrapping.
 *
 * Tapping Alarms wedged the LVGL task: the serial log showed the task watchdog
 * firing with "IDLE0 (CPU 0)" starved and "CPU 0: lvgl" running, the page never
 * appeared, and every button stopped responding because the one LVGL task was
 * no longer servicing input.
 *
 * The cause is layout, not data. Each row panel is LV_SIZE_CONTENT tall inside a
 * scrollable flex column, and its labels defaulted to wrapping. A wrapping label
 * changes height when its width changes, while the panel's height depends on the
 * label - so the two re-measure each other, and across 16 alarm rows plus 16
 * event rows that reflow does not settle in reasonable time.
 *
 * Fixing a width and clipping breaks the circular dependency: label height no
 * longer depends on the measured width, so the pass terminates. This is the same
 * remedy already applied to the Overview status labels for the same reason. */
#define ALARM_ROW_TEXT_WIDTH 300

static void clip_row_label(lv_obj_t *label)
{
    if (!label) return;
    lv_obj_set_width(label, ALARM_ROW_TEXT_WIDTH);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
}

static void init_alarm_rows(void)
{
    s_ui.alarm_empty = screen_ui_muted_label(s_ui.alarm_list, "No alarm conditions recorded");
    set_visible(s_ui.alarm_empty, false);

    for (size_t i = 0; i < SCREEN_API_MAX_ALARMS; ++i) {
        alarm_row_ui_t *ui = &s_ui.alarms[i];
        ui->panel = screen_ui_panel(s_ui.alarm_list);
        lv_obj_set_width(ui->panel, LV_PCT(100));
        lv_obj_set_height(ui->panel, LV_SIZE_CONTENT);
        lv_obj_set_layout(ui->panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(ui->panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(ui->panel, 3, LV_PART_MAIN);

        ui->heading = screen_ui_title(ui->panel, "--");
        ui->detail = screen_ui_muted_label(ui->panel, "--");
        ui->suppression = screen_ui_muted_label(ui->panel, "--");
        ui->action = screen_ui_muted_label(ui->panel, "--");
        clip_row_label(ui->heading);
        clip_row_label(ui->detail);
        clip_row_label(ui->suppression);
        clip_row_label(ui->action);
        set_visible(ui->panel, false);
    }
}

static void init_event_rows(void)
{
    s_ui.event_empty = screen_ui_muted_label(s_ui.event_list, "No events recorded");
    set_visible(s_ui.event_empty, false);

    for (size_t i = 0; i < SCREEN_API_MAX_EVENTS; ++i) {
        event_row_ui_t *ui = &s_ui.events[i];
        ui->panel = screen_ui_panel(s_ui.event_list);
        lv_obj_set_width(ui->panel, LV_PCT(100));
        lv_obj_set_height(ui->panel, LV_SIZE_CONTENT);
        lv_obj_set_layout(ui->panel, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(ui->panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(ui->panel, 3, LV_PART_MAIN);

        ui->heading = screen_ui_title(ui->panel, "--");
        ui->age = screen_ui_muted_label(ui->panel, "--");
        ui->detail = screen_ui_muted_label(ui->panel, "--");
        clip_row_label(ui->heading);
        clip_row_label(ui->age);
        clip_row_label(ui->detail);
        set_visible(ui->panel, false);
    }
}

static void hide_alarm_rows_from(size_t first)
{
    for (size_t i = first; i < SCREEN_API_MAX_ALARMS; ++i) {
        set_visible(s_ui.alarms[i].panel, false);
    }
}

static void hide_event_rows_from(size_t first)
{
    for (size_t i = first; i < SCREEN_API_MAX_EVENTS; ++i) {
        set_visible(s_ui.events[i].panel, false);
    }
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
    init_alarm_rows();
    s_ui.event_summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/operator/events");
    s_ui.event_list = make_scroll_list(s_ui.root, 190);
    init_event_rows();
    return s_ui.root;
}

void alarms_screen_apply_alarms(const screen_alarms_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    (void)screen_ui_set_text_fmt_if_changed(
        s_ui.alarm_summary,
        "Primary active %lu | Consequential %lu | Unacknowledged %lu%s",
        (unsigned long)snapshot->primary_active_count,
        (unsigned long)snapshot->consequential_active_count,
        (unsigned long)snapshot->unacknowledged_count,
        snapshot->truncated ? " | list truncated" : "");

    set_visible(s_ui.alarm_empty, snapshot->row_count == 0U);
    const size_t count = snapshot->row_count < SCREEN_API_MAX_ALARMS
                             ? snapshot->row_count
                             : SCREEN_API_MAX_ALARMS;

    for (size_t i = 0; i < count; ++i) {
        const screen_alarm_row_t *row = &snapshot->rows[i];
        alarm_row_ui_t *ui = &s_ui.alarms[i];

        (void)screen_ui_set_text_fmt_if_changed(
            ui->heading, "%s  %s  [%s/%s]",
            screen_ui_safe_text(row->id, "ALARM"),
            screen_ui_safe_text(row->title, "Unnamed alarm"),
            screen_ui_safe_text(row->priority, "priority unknown"),
            screen_ui_safe_text(row->state, "state unknown"));

        (void)screen_ui_set_text_fmt_if_changed(
            ui->detail, "Role: %s%s%s%s",
            screen_ui_safe_text(row->role, "unknown"),
            row->caused_by[0] ? " | caused by " : "",
            row->caused_by[0] ? row->caused_by : "",
            row->stale ? " | STALE" : "");

        const bool suppressed = row->shelved || row->suppressed_by_design || row->out_of_service;
        if (suppressed) {
            (void)screen_ui_set_text_fmt_if_changed(
                ui->suppression, "Suppression:%s%s%s",
                row->shelved ? " shelved" : "",
                row->suppressed_by_design ? " by-design" : "",
                row->out_of_service ? " out-of-service" : "");
        }
        set_visible(ui->suppression, suppressed);

        (void)screen_ui_set_text_if_changed(
            ui->action,
            screen_ui_safe_text(row->recommended_action,
                                "No recommended action published"));
        set_visible(ui->panel, true);
    }
    hide_alarm_rows_from(count);
}

void alarms_screen_apply_events(const screen_events_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    (void)screen_ui_set_text_fmt_if_changed(
        s_ui.event_summary,
        "Recent events | active critical %lu | warning %lu | stored %lu%s",
        (unsigned long)snapshot->active_critical,
        (unsigned long)snapshot->active_warning,
        (unsigned long)snapshot->stored_events,
        snapshot->truncated ? " | list truncated" : "");

    set_visible(s_ui.event_empty, snapshot->row_count == 0U);
    const size_t count = snapshot->row_count < SCREEN_API_MAX_EVENTS
                             ? snapshot->row_count
                             : SCREEN_API_MAX_EVENTS;

    for (size_t i = 0; i < count; ++i) {
        const screen_event_row_t *row = &snapshot->rows[i];
        event_row_ui_t *ui = &s_ui.events[i];

        (void)screen_ui_set_text_fmt_if_changed(
            ui->heading, "%s | %s | %s",
            screen_ui_safe_text(row->severity, "unknown"),
            screen_ui_safe_text(row->state, "unknown"),
            screen_ui_safe_text(row->title, "Event"));
        (void)screen_ui_set_text_fmt_if_changed(
            ui->age, "Age: %lu ms", (unsigned long)row->age_ms);
        (void)screen_ui_set_text_if_changed(ui->detail,
                                            screen_ui_safe_text(row->detail, "No detail"));
        set_visible(ui->panel, true);
    }
    hide_event_rows_from(count);
}

void alarms_screen_show_unavailable(void)
{
    if (!s_ui.root) return;
    (void)screen_ui_set_text_if_changed(s_ui.alarm_summary, "Alarm backend unavailable");
    (void)screen_ui_set_text_if_changed(s_ui.event_summary, "Event backend unavailable");
    hide_alarm_rows_from(0U);
    hide_event_rows_from(0U);
    (void)screen_ui_set_text_if_changed(s_ui.alarm_empty, "Alarm state unknown");
    (void)screen_ui_set_text_if_changed(s_ui.event_empty, "Event history unavailable");
    set_visible(s_ui.alarm_empty, true);
    set_visible(s_ui.event_empty, true);
}
