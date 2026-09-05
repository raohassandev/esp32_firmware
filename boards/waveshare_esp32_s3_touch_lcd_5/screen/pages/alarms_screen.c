#include "alarms_screen.h"

#include <stdio.h>
#include <string.h>

#include "screen_widgets.h"

typedef enum {
    ALARM_FILTER_ALL = 0,
    ALARM_FILTER_ACTIVE,
    ALARM_FILTER_UNACKNOWLEDGED,
    ALARM_FILTER_COUNT,
} alarm_filter_t;

typedef enum {
    ALARM_SORT_PRIORITY = 0,
    ALARM_SORT_STATE,
    ALARM_SORT_ID,
    ALARM_SORT_COUNT,
} alarm_sort_t;

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *heading;
    lv_obj_t *detail;
    lv_obj_t *suppression;
    lv_obj_t *action;
    lv_obj_t *ack_button;
    lv_obj_t *ack_label;
    uint32_t code;
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
    lv_obj_t *filter_button;
    lv_obj_t *filter_label;
    lv_obj_t *sort_button;
    lv_obj_t *sort_label;
    lv_obj_t *operation_message;
    lv_obj_t *alarm_list;
    lv_obj_t *alarm_empty;
    alarm_row_ui_t alarms[SCREEN_API_MAX_ALARMS];
    lv_obj_t *event_summary;
    lv_obj_t *event_list;
    lv_obj_t *event_empty;
    event_row_ui_t events[SCREEN_API_MAX_EVENTS];
    const screen_alarms_snapshot_t *alarm_snapshot;
    alarm_filter_t filter;
    alarm_sort_t sort;
    alarms_screen_acknowledge_fn acknowledge;
    void *ack_context;
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

static lv_obj_t *compact_button(lv_obj_t *parent, const char *text, lv_obj_t **label_out,
                                lv_event_cb_t callback, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(button, 34);
    lv_obj_set_width(button, 150);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text ? text : "");
    lv_obj_center(label);
    if (label_out) *label_out = label;
    return button;
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

static const char *filter_name(alarm_filter_t filter)
{
    switch (filter) {
    case ALARM_FILTER_ACTIVE: return "Active";
    case ALARM_FILTER_UNACKNOWLEDGED: return "Unack";
    case ALARM_FILTER_ALL:
    default: return "All";
    }
}

static const char *sort_name(alarm_sort_t sort)
{
    switch (sort) {
    case ALARM_SORT_STATE: return "State";
    case ALARM_SORT_ID: return "ID";
    case ALARM_SORT_PRIORITY:
    default: return "Priority";
    }
}

static int priority_rank(const char *priority)
{
    if (!priority) return 3;
    if (strcmp(priority, "high") == 0) return 0;
    if (strcmp(priority, "medium") == 0) return 1;
    if (strcmp(priority, "low") == 0) return 2;
    return 3;
}

static int state_rank(const char *state)
{
    if (!state) return 4;
    if (strcmp(state, "unacknowledged") == 0) return 0;
    if (strcmp(state, "rtn_unacknowledged") == 0) return 1;
    if (strcmp(state, "acknowledged") == 0) return 2;
    if (strcmp(state, "normal") == 0) return 3;
    return 4;
}

static bool alarm_matches_filter(const screen_alarm_row_t *row)
{
    if (!row) return false;
    switch (s_ui.filter) {
    case ALARM_FILTER_ACTIVE:
        return row->present;
    case ALARM_FILTER_UNACKNOWLEDGED:
        return !row->acknowledged;
    case ALARM_FILTER_ALL:
    default:
        return true;
    }
}

static int compare_alarm_rows(const screen_alarm_row_t *left,
                              const screen_alarm_row_t *right)
{
    if (!left || !right) return 0;
    int cmp = 0;
    switch (s_ui.sort) {
    case ALARM_SORT_STATE:
        cmp = state_rank(left->state) - state_rank(right->state);
        break;
    case ALARM_SORT_ID:
        cmp = strcmp(left->id, right->id);
        break;
    case ALARM_SORT_PRIORITY:
    default:
        cmp = priority_rank(left->priority) - priority_rank(right->priority);
        if (cmp == 0) cmp = state_rank(left->state) - state_rank(right->state);
        break;
    }
    if (cmp == 0) cmp = strcmp(left->id, right->id);
    return cmp;
}

static void set_operation_message(const char *text, bool good)
{
    if (!s_ui.operation_message) return;
    (void)screen_ui_set_text_if_changed(s_ui.operation_message, text ? text : "");
    lv_obj_set_style_text_color(s_ui.operation_message,
                                lv_color_hex(good ? 0x62D28F : 0xF07178),
                                LV_PART_MAIN);
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

static void render_alarm_snapshot(void)
{
    const screen_alarms_snapshot_t *snapshot = s_ui.alarm_snapshot;
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    size_t indices[SCREEN_API_MAX_ALARMS];
    size_t visible_count = 0U;
    const size_t count = snapshot->row_count < SCREEN_API_MAX_ALARMS
                             ? snapshot->row_count
                             : SCREEN_API_MAX_ALARMS;
    for (size_t i = 0U; i < count; ++i) {
        if (alarm_matches_filter(&snapshot->rows[i])) indices[visible_count++] = i;
    }

    /* Bounded insertion sort: at most 16 rows, no allocation and deterministic
     * runtime inside the LVGL task. */
    for (size_t i = 1U; i < visible_count; ++i) {
        const size_t key = indices[i];
        size_t j = i;
        while (j > 0U &&
               compare_alarm_rows(&snapshot->rows[key],
                                  &snapshot->rows[indices[j - 1U]]) < 0) {
            indices[j] = indices[j - 1U];
            --j;
        }
        indices[j] = key;
    }

    (void)screen_ui_set_text_fmt_if_changed(
        s_ui.alarm_summary,
        "Primary %lu | Consequential %lu | Unack %lu | Showing %lu%s",
        (unsigned long)snapshot->primary_active_count,
        (unsigned long)snapshot->consequential_active_count,
        (unsigned long)snapshot->unacknowledged_count,
        (unsigned long)visible_count,
        snapshot->truncated ? " | source truncated" : "");

    if (s_ui.filter_label) {
        char text[32];
        snprintf(text, sizeof(text), "Filter: %s", filter_name(s_ui.filter));
        lv_label_set_text(s_ui.filter_label, text);
    }
    if (s_ui.sort_label) {
        char text[32];
        snprintf(text, sizeof(text), "Sort: %s", sort_name(s_ui.sort));
        lv_label_set_text(s_ui.sort_label, text);
    }

    set_visible(s_ui.alarm_empty, visible_count == 0U);
    if (visible_count == 0U) {
        (void)screen_ui_set_text_fmt_if_changed(s_ui.alarm_empty,
                                                "No alarms match filter: %s",
                                                filter_name(s_ui.filter));
    }

    for (size_t display_index = 0U; display_index < visible_count; ++display_index) {
        const screen_alarm_row_t *row = &snapshot->rows[indices[display_index]];
        alarm_row_ui_t *ui = &s_ui.alarms[display_index];
        ui->code = row->code;

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

        const bool outstanding = !row->acknowledged;
        set_visible(ui->ack_button, outstanding);
        if (outstanding) {
            if (s_ui.acknowledge) lv_obj_remove_state(ui->ack_button, LV_STATE_DISABLED);
            else lv_obj_add_state(ui->ack_button, LV_STATE_DISABLED);
            lv_label_set_text(ui->ack_label,
                              s_ui.acknowledge ? "Acknowledge" : "Ack unavailable");
        }
        set_visible(ui->panel, true);
    }
    hide_alarm_rows_from(visible_count);
}

static void acknowledge_clicked(lv_event_t *event)
{
    alarm_row_ui_t *ui = (alarm_row_ui_t *)lv_event_get_user_data(event);
    if (!ui) return;
    if (!s_ui.acknowledge) {
        set_operation_message("Alarm acknowledgement backend unavailable.", false);
        return;
    }

    char message[160] = {0};
    const bool ok = s_ui.acknowledge(s_ui.ack_context, ui->code,
                                     message, sizeof(message));
    set_operation_message(message[0] ? message
                                     : (ok ? "Alarm acknowledged."
                                           : "Alarm acknowledgement refused."),
                          ok);
    if (ok) {
        lv_obj_add_state(ui->ack_button, LV_STATE_DISABLED);
        lv_label_set_text(ui->ack_label, "Acknowledged");
    }
}

static void filter_clicked(lv_event_t *event)
{
    (void)event;
    s_ui.filter = (alarm_filter_t)(((unsigned)s_ui.filter + 1U) % ALARM_FILTER_COUNT);
    render_alarm_snapshot();
}

static void sort_clicked(lv_event_t *event)
{
    (void)event;
    s_ui.sort = (alarm_sort_t)(((unsigned)s_ui.sort + 1U) % ALARM_SORT_COUNT);
    render_alarm_snapshot();
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
        ui->ack_button = compact_button(ui->panel, "Acknowledge", &ui->ack_label,
                                        acknowledge_clicked, ui);
        set_visible(ui->ack_button, false);
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

lv_obj_t *alarms_screen_create(lv_obj_t *parent)
{
    s_ui.filter = ALARM_FILTER_ALL;
    s_ui.sort = ALARM_SORT_PRIORITY;
    s_ui.alarm_snapshot = NULL;

    s_ui.root = screen_ui_panel(parent);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_ui.root, LV_DIR_VER);

    screen_ui_title(s_ui.root, "Alarms / Events");
    s_ui.alarm_summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/operator/alarms");

    lv_obj_t *controls = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(controls);
    lv_obj_set_width(controls, LV_PCT(100));
    lv_obj_set_height(controls, 36);
    lv_obj_set_layout(controls, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(controls, 8, LV_PART_MAIN);
    s_ui.filter_button = compact_button(controls, "Filter: All", &s_ui.filter_label,
                                        filter_clicked, NULL);
    s_ui.sort_button = compact_button(controls, "Sort: Priority", &s_ui.sort_label,
                                      sort_clicked, NULL);

    s_ui.operation_message = screen_ui_muted_label(
        s_ui.root,
        "Acknowledgement requires an unlocked local Engineering session (Commission page).");

    s_ui.alarm_list = make_scroll_list(s_ui.root, 180);
    init_alarm_rows();
    s_ui.event_summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/operator/events");
    s_ui.event_list = make_scroll_list(s_ui.root, 160);
    init_event_rows();
    return s_ui.root;
}

void alarms_screen_set_acknowledge_backend(alarms_screen_acknowledge_fn acknowledge,
                                           void *context)
{
    s_ui.acknowledge = acknowledge;
    s_ui.ack_context = context;
    if (s_ui.alarm_snapshot) render_alarm_snapshot();
}

void alarms_screen_apply_alarms(const screen_alarms_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;
    s_ui.alarm_snapshot = snapshot;
    render_alarm_snapshot();
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
    s_ui.alarm_snapshot = NULL;
    (void)screen_ui_set_text_if_changed(s_ui.alarm_summary, "Alarm backend unavailable");
    (void)screen_ui_set_text_if_changed(s_ui.event_summary, "Event backend unavailable");
    hide_alarm_rows_from(0U);
    hide_event_rows_from(0U);
    (void)screen_ui_set_text_if_changed(s_ui.alarm_empty, "Alarm state unknown");
    (void)screen_ui_set_text_if_changed(s_ui.event_empty, "Event history unavailable");
    set_visible(s_ui.alarm_empty, true);
    set_visible(s_ui.event_empty, true);
}
