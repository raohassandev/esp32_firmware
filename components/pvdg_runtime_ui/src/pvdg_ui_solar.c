#include "pvdg_ui_solar.h"

#include <stdio.h>
#include <string.h>

#include "pvdg_ui_theme.h"

#define SOLAR_VISIBLE_ROWS 6U

typedef struct {
    lv_obj_t *root;
    lv_obj_t *status_badge;
    lv_obj_t *measured_kw;
    lv_obj_t *online_count;
    lv_obj_t *requested_kw;
    lv_obj_t *applied_kw;
    lv_obj_t *authority;
    lv_obj_t *row_name[SOLAR_VISIBLE_ROWS];
    lv_obj_t *row_status[SOLAR_VISIBLE_ROWS];
    lv_obj_t *row_power[SOLAR_VISIBLE_ROWS];
    lv_obj_t *row_readback[SOLAR_VISIBLE_ROWS];
    lv_obj_t *row_age[SOLAR_VISIBLE_ROWS];
    lv_obj_t *table_note;
} solar_ui_t;

static solar_ui_t s_ui;

static void set_measurement(lv_obj_t *label, const pvdg_ui_measurement_t *m,
                            const char *unit, unsigned decimals)
{
    char text[40];
    pvdg_ui_format_measurement(text, sizeof(text), m, unit, decimals);
    pvdg_ui_label_set_if_changed(label, text);
}

static lv_obj_t *make_section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = pvdg_ui_make_card(parent);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 2, LV_PART_MAIN);
    pvdg_ui_make_title(card, title);
    return card;
}

static lv_obj_t *make_cell(lv_obj_t *parent, int width, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "--");
    pvdg_ui_style_label(label, color, PVDG_UI_FONT_BODY);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    return label;
}

static void make_table_row(lv_obj_t *parent, unsigned index)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 31);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(PVDG_UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);

    s_ui.row_name[index] = make_cell(row, 105, lv_color_hex(PVDG_UI_COLOR_TEXT));
    s_ui.row_status[index] = make_cell(row, 105, lv_color_hex(PVDG_UI_COLOR_MUTED));
    s_ui.row_power[index] = make_cell(row, 90, lv_color_hex(PVDG_UI_COLOR_SOLAR));
    s_ui.row_readback[index] = make_cell(row, 85, lv_color_hex(PVDG_UI_COLOR_TEXT));
    s_ui.row_age[index] = make_cell(row, 80, lv_color_hex(PVDG_UI_COLOR_MUTED));
}

static void make_header_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 28);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    const char *names[] = {"Inverter", "State", "Measured", "Readback", "Age"};
    const int widths[] = {105, 105, 90, 85, 80};
    for (size_t i = 0; i < 5; ++i) {
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, names[i]);
        pvdg_ui_style_label(label, lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
        lv_obj_set_width(label, widths[i]);
    }
}

lv_obj_t *pvdg_ui_solar_create(lv_obj_t *parent)
{
    memset(&s_ui, 0, sizeof(s_ui));
    pvdg_ui_theme_init();

    s_ui.root = lv_obj_create(parent);
    pvdg_ui_style_root(s_ui.root);
    lv_obj_set_size(s_ui.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(s_ui.root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_ui.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_ui.root, PVDG_UI_GAP_SM, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_ui.root, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *head = lv_obj_create(s_ui.root);
    pvdg_ui_style_root(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 38);
    lv_obj_set_layout(head, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    pvdg_ui_make_title(head, "Solar PV");
    s_ui.status_badge = pvdg_ui_make_badge(head, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));

    lv_obj_t *summary = lv_obj_create(s_ui.root);
    pvdg_ui_style_root(summary);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_obj_set_height(summary, 75);
    lv_obj_set_layout(summary, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(summary, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *production = make_section(summary, "Measured Production");
    lv_obj_set_flex_grow(production, 1);
    s_ui.measured_kw = lv_label_create(production);
    pvdg_ui_style_label(s_ui.measured_kw, lv_color_hex(PVDG_UI_COLOR_SOLAR), PVDG_UI_FONT_HERO);
    s_ui.online_count = pvdg_ui_make_muted(production, "-- online");

    lv_obj_t *command = make_section(summary, "PV Command State");
    lv_obj_set_flex_grow(command, 2);
    lv_obj_t *command_rows = lv_obj_create(command);
    lv_obj_remove_style_all(command_rows);
    lv_obj_set_width(command_rows, LV_PCT(100));
    lv_obj_set_flex_grow(command_rows, 1);
    lv_obj_set_layout(command_rows, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(command_rows, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(command_rows, PVDG_UI_GAP_LG, LV_PART_MAIN);
    lv_obj_t *req = lv_obj_create(command_rows);
    lv_obj_remove_style_all(req);
    lv_obj_set_flex_grow(req, 1);
    lv_obj_set_layout(req, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(req, LV_FLEX_FLOW_COLUMN);
    pvdg_ui_make_muted(req, "Requested");
    s_ui.requested_kw = lv_label_create(req);
    pvdg_ui_style_label(s_ui.requested_kw, lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    lv_obj_t *applied = lv_obj_create(command_rows);
    lv_obj_remove_style_all(applied);
    lv_obj_set_flex_grow(applied, 1);
    lv_obj_set_layout(applied, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(applied, LV_FLEX_FLOW_COLUMN);
    pvdg_ui_make_muted(applied, "Applied");
    s_ui.applied_kw = lv_label_create(applied);
    pvdg_ui_style_label(s_ui.applied_kw, lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    lv_obj_t *auth = lv_obj_create(command_rows);
    lv_obj_remove_style_all(auth);
    lv_obj_set_flex_grow(auth, 1);
    lv_obj_set_layout(auth, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(auth, LV_FLEX_FLOW_COLUMN);
    pvdg_ui_make_muted(auth, "Authority");
    s_ui.authority = lv_label_create(auth);
    pvdg_ui_style_label(s_ui.authority, lv_color_hex(PVDG_UI_COLOR_SUCCESS), PVDG_UI_FONT_BODY);

    lv_obj_t *table = make_section(s_ui.root, "Inverter Status");
    lv_obj_set_width(table, LV_PCT(100));
    lv_obj_set_flex_grow(table, 1);
    make_header_row(table);
    for (unsigned i = 0; i < SOLAR_VISIBLE_ROWS; ++i) make_table_row(table, i);
    s_ui.table_note = pvdg_ui_make_muted(table, "Showing first 6 configured inverters");

    pvdg_ui_solar_show_unavailable();
    return s_ui.root;
}

void pvdg_ui_solar_apply_model(const pvdg_ui_model_t *model)
{
    if (!model || !s_ui.root) return;

    set_measurement(s_ui.measured_kw, &model->solar.measured_kw, "kW", 1);
    char online[48];
    snprintf(online, sizeof(online), "%u / %u online",
             (unsigned)model->solar.online_count,
             (unsigned)model->solar.configured_count);
    pvdg_ui_label_set_if_changed(s_ui.online_count, online);
    set_measurement(s_ui.requested_kw, &model->controller.requested_pv_kw, "kW", 1);
    set_measurement(s_ui.applied_kw, &model->controller.applied_pv_kw, "kW", 1);
    pvdg_ui_label_set_if_changed(s_ui.authority,
        model->controller.authority_label[0] ? model->controller.authority_label : "Unknown");

    lv_color_t color = model->solar.measured_kw.available
                       ? lv_color_hex(PVDG_UI_COLOR_SUCCESS)
                       : lv_color_hex(PVDG_UI_COLOR_WARNING);
    pvdg_ui_badge_set(s_ui.status_badge,
        model->solar.measured_kw.available ? "Measured" : "Telemetry unavailable", color);

    for (unsigned i = 0; i < SOLAR_VISIBLE_ROWS; ++i) {
        if (i >= model->solar.inverter_count) {
            pvdg_ui_label_set_if_changed(s_ui.row_name[i], "--");
            pvdg_ui_label_set_if_changed(s_ui.row_status[i], "--");
            pvdg_ui_label_set_if_changed(s_ui.row_power[i], "--");
            pvdg_ui_label_set_if_changed(s_ui.row_readback[i], "--");
            pvdg_ui_label_set_if_changed(s_ui.row_age[i], "--");
            continue;
        }
        const pvdg_ui_inverter_t *inv = &model->solar.inverters[i];
        char name[40];
        snprintf(name, sizeof(name), "%u  %s", i + 1U, inv->name[0] ? inv->name : "Inverter");
        pvdg_ui_label_set_if_changed(s_ui.row_name[i], name);

        const char *state = inv->telemetry_stale ? "Stale" :
                            !inv->telemetry_valid ? "Invalid" :
                            inv->status[0] ? inv->status :
                            inv->online ? "Online" : "Offline";
        pvdg_ui_label_set_if_changed(s_ui.row_status[i], state);
        set_measurement(s_ui.row_power[i], &inv->measured_power_kw, "kW", 1);
        set_measurement(s_ui.row_readback[i], &inv->readback_percent, "%", 1);
        char age[32] = "--";
        if (inv->measured_power_kw.has_age_ms) {
            snprintf(age, sizeof(age), "%lu ms",
                     (unsigned long)inv->measured_power_kw.age_ms);
        } else if (inv->readback_percent.has_age_ms) {
            snprintf(age, sizeof(age), "%lu ms",
                     (unsigned long)inv->readback_percent.age_ms);
        }
        pvdg_ui_label_set_if_changed(s_ui.row_age[i], age);
        lv_obj_set_style_text_color(
            s_ui.row_status[i],
            inv->telemetry_stale || !inv->telemetry_valid
                ? lv_color_hex(PVDG_UI_COLOR_WARNING)
                : inv->online ? lv_color_hex(PVDG_UI_COLOR_SUCCESS)
                              : lv_color_hex(PVDG_UI_COLOR_DANGER),
            LV_PART_MAIN);
    }

    char note[72];
    snprintf(note, sizeof(note), "Showing %u of %u inverter%s",
             (unsigned)(model->solar.inverter_count < SOLAR_VISIBLE_ROWS ? model->solar.inverter_count : SOLAR_VISIBLE_ROWS),
             (unsigned)model->solar.inverter_count,
             model->solar.inverter_count == 1U ? "" : "s");
    pvdg_ui_label_set_if_changed(s_ui.table_note, note);
}

void pvdg_ui_solar_show_unavailable(void)
{
    if (!s_ui.root) return;
    pvdg_ui_label_set_if_changed(s_ui.measured_kw, "--");
    pvdg_ui_label_set_if_changed(s_ui.online_count, "-- online");
    pvdg_ui_label_set_if_changed(s_ui.requested_kw, "--");
    pvdg_ui_label_set_if_changed(s_ui.applied_kw, "--");
    pvdg_ui_label_set_if_changed(s_ui.authority, "Unknown");
    for (unsigned i = 0; i < SOLAR_VISIBLE_ROWS; ++i) {
        pvdg_ui_label_set_if_changed(s_ui.row_name[i], "--");
        pvdg_ui_label_set_if_changed(s_ui.row_status[i], "--");
        pvdg_ui_label_set_if_changed(s_ui.row_power[i], "--");
        pvdg_ui_label_set_if_changed(s_ui.row_readback[i], "--");
        pvdg_ui_label_set_if_changed(s_ui.row_age[i], "--");
    }
    pvdg_ui_badge_set(s_ui.status_badge, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));
}
