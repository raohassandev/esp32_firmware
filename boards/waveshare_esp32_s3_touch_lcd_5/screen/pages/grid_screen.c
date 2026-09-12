#include "grid_screen.h"

#include <stdio.h>
#include <string.h>

#include "screen_widgets.h"

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *heading;
    lv_obj_t *state;
    lv_obj_t *power;
    lv_obj_t *age;
} grid_row_ui_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *summary;
    lv_obj_t *list;
    lv_obj_t *empty;
    grid_row_ui_t rows[SCREEN_API_MAX_METERS];
} grid_ui_t;

static grid_ui_t s_ui;

static void set_visible(lv_obj_t *obj, bool visible)
{
    if (!obj) return;
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static grid_row_ui_t *ensure_row(size_t index)
{
    if (index >= SCREEN_API_MAX_METERS) return NULL;
    grid_row_ui_t *ui = &s_ui.rows[index];
    if (ui->panel) return ui;

    ui->panel = screen_ui_panel(s_ui.list);
    lv_obj_set_width(ui->panel, LV_PCT(100));
    lv_obj_set_height(ui->panel, LV_SIZE_CONTENT);
    lv_obj_set_layout(ui->panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui->panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui->panel, 4, LV_PART_MAIN);

    ui->heading = screen_ui_title(ui->panel, "--");

    lv_obj_t *line = lv_obj_create(ui->panel);
    lv_obj_remove_style_all(line);
    lv_obj_set_width(line, LV_PCT(100));
    lv_obj_set_height(line, LV_SIZE_CONTENT);
    lv_obj_set_layout(line, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui->state = screen_ui_value_label(line, "--");
    ui->power = screen_ui_value_label(line, "-- kW");
    ui->age = screen_ui_muted_label(ui->panel, "Data age: unavailable");
    return ui;
}

static void hide_all_rows(void)
{
    for (size_t i = 0; i < SCREEN_API_MAX_METERS; ++i) {
        if (s_ui.rows[i].panel) set_visible(s_ui.rows[i].panel, false);
    }
}

lv_obj_t *grid_screen_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.root = screen_ui_panel(parent);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.root, 10, LV_PART_MAIN);

    screen_ui_title(s_ui.root, "Grid / Meters");
    s_ui.summary = screen_ui_muted_label(s_ui.root, "Waiting for /api/meters");

    s_ui.list = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.list);
    lv_obj_set_width(s_ui.list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ui.list, 1);
    lv_obj_set_layout(s_ui.list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_ui.list, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_ui.list, LV_DIR_VER);

    /* Keep a persistent empty/unavailable row instead of deleting and rebuilding
     * the list tree every time data disappears or reappears. */
    s_ui.empty = screen_ui_muted_label(s_ui.list, "No meters configured");
    set_visible(s_ui.empty, false);
    return s_ui.root;
}

void grid_screen_apply(const screen_meters_snapshot_t *snapshot)
{
    if (!s_ui.root || !snapshot || !snapshot->valid) return;

    (void)screen_ui_set_text_fmt_if_changed(
        s_ui.summary,
        "%lu configured | %lu enabled | %lu online | %lu unavailable%s",
        (unsigned long)snapshot->configured_count,
        (unsigned long)snapshot->enabled_count,
        (unsigned long)snapshot->online_count,
        (unsigned long)snapshot->stale_or_unavailable_count,
        snapshot->truncated ? " | list truncated" : "");

    if (snapshot->row_count == 0U) {
        hide_all_rows();
        (void)screen_ui_set_text_if_changed(s_ui.empty, "No meters configured");
        set_visible(s_ui.empty, true);
        return;
    }

    set_visible(s_ui.empty, false);
    const size_t count = snapshot->row_count < SCREEN_API_MAX_METERS
                             ? snapshot->row_count
                             : SCREEN_API_MAX_METERS;
    for (size_t i = 0; i < count; ++i) {
        const screen_meter_row_t *row = &snapshot->rows[i];
        grid_row_ui_t *ui = ensure_row(i);
        if (!ui) continue;
        set_visible(ui->panel, true);

        (void)screen_ui_set_text_fmt_if_changed(
            ui->heading, "%u  %s  [%s]",
            (unsigned)row->index,
            screen_ui_safe_text(row->name, "Unnamed meter"),
            screen_ui_safe_text(row->role_name, "role unknown"));
        screen_ui_set_state_text(ui->state,
                                 screen_ui_safe_text(row->state, "unknown"),
                                 row->online);
        screen_ui_set_kw(ui->power, row->has_power_kw, row->power_kw);

        if (row->has_data_age_ms) {
            (void)screen_ui_set_text_fmt_if_changed(
                ui->age, "Data age: %lu ms", (unsigned long)row->data_age_ms);
        } else {
            (void)screen_ui_set_text_if_changed(ui->age, "Data age: unavailable");
        }
    }

    for (size_t i = count; i < SCREEN_API_MAX_METERS; ++i) {
        if (s_ui.rows[i].panel) set_visible(s_ui.rows[i].panel, false);
    }
}

void grid_screen_show_unavailable(void)
{
    if (!s_ui.root) return;
    (void)screen_ui_set_text_if_changed(s_ui.summary, "Meter backend unavailable");
    hide_all_rows();
    (void)screen_ui_set_text_if_changed(
        s_ui.empty, "Measurements are unknown; no zero value is assumed.");
    set_visible(s_ui.empty, true);
}
