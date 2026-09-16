#include "pvdg_ui_overview.h"

#include <stdio.h>
#include <string.h>

#include "pvdg_ui_theme.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *load_value;
    lv_obj_t *load_note;
    lv_obj_t *solar_value;
    lv_obj_t *solar_note;
    lv_obj_t *grid_value;
    lv_obj_t *grid_note;
    lv_obj_t *generator_value;
    lv_obj_t *generator_note;
    lv_obj_t *policy_value;
    lv_obj_t *policy_note;

    lv_obj_t *flow_solar_value;
    lv_obj_t *flow_solar_state;
    lv_obj_t *flow_generator_value;
    lv_obj_t *flow_generator_state;
    lv_obj_t *flow_grid_value;
    lv_obj_t *flow_grid_state;
    lv_obj_t *flow_load_value;
    lv_obj_t *flow_load_state;
    lv_obj_t *flow_policy_badge;
    lv_obj_t *flow_policy_note;
    lv_obj_t *grid_arrow;
    lv_obj_t *solar_power_path;
    lv_obj_t *generator_power_path;
    lv_obj_t *grid_power_path;
    lv_obj_t *load_power_path;

    lv_obj_t *grid_card_value;
    lv_obj_t *grid_card_state;
    lv_obj_t *solar_card_value;
    lv_obj_t *solar_card_state;
    lv_obj_t *generator_card_value;
    lv_obj_t *generator_card_state;
    lv_obj_t *load_card_value;
    lv_obj_t *load_card_state;
    lv_obj_t *alarm_summary;
} overview_ui_t;

static overview_ui_t s_ui;

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    pvdg_ui_style_label(label, color, font);
    return label;
}

static lv_obj_t *make_kpi(lv_obj_t *parent, const char *title,
                          lv_color_t accent, lv_obj_t **value_out,
                          lv_obj_t **note_out)
{
    lv_obj_t *card = pvdg_ui_make_card(parent);
    lv_obj_set_height(card, 62);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 1, LV_PART_MAIN);

    lv_obj_t *title_label = make_label(card, title, lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    lv_obj_set_style_text_color(title_label, accent, LV_PART_MAIN);
    lv_obj_t *value = make_label(card, "--", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_HERO);
    lv_obj_t *note = make_label(card, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    if (value_out) *value_out = value;
    if (note_out) *note_out = note;
    return card;
}

static lv_obj_t *make_flow_source(lv_obj_t *parent, int x, int y, int w,
                                  const char *title, lv_color_t accent,
                                  lv_obj_t **value_out, lv_obj_t **state_out)
{
    lv_obj_t *card = pvdg_ui_make_card(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, 64);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 1, LV_PART_MAIN);

    lv_obj_t *heading = make_label(card, title, accent, PVDG_UI_FONT_BODY);
    lv_obj_t *value = make_label(card, "--", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    lv_obj_t *state = make_label(card, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    if (value_out) *value_out = value;
    if (state_out) *state_out = state;
    return card;
}

static lv_obj_t *make_segment(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 2, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void make_dashed_h(lv_obj_t *parent, int x, int y, int width, lv_color_t color)
{
    for (int offset = 0; offset < width; offset += 13) {
        int remaining = width - offset;
        make_segment(parent, x + offset, y, remaining > 7 ? 7 : remaining, 2, color);
    }
}

static void make_dashed_v(lv_obj_t *parent, int x, int y, int height, lv_color_t color)
{
    for (int offset = 0; offset < height; offset += 13) {
        int remaining = height - offset;
        make_segment(parent, x, y + offset, 2, remaining > 7 ? 7 : remaining, color);
    }
}

static lv_obj_t *make_source_summary(lv_obj_t *parent, const char *title,
                                     lv_color_t accent, lv_obj_t **value_out,
                                     lv_obj_t **state_out)
{
    lv_obj_t *card = pvdg_ui_make_card(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 2, LV_PART_MAIN);

    make_label(card, title, accent, PVDG_UI_FONT_BODY);
    lv_obj_t *value = make_label(card, "--", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    lv_obj_t *state = make_label(card, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    if (value_out) *value_out = value;
    if (state_out) *state_out = state;
    return card;
}

static void set_measurement(lv_obj_t *label, const pvdg_ui_measurement_t *measurement,
                            const char *unit, unsigned decimals)
{
    char text[40];
    pvdg_ui_format_measurement(text, sizeof(text), measurement, unit, decimals);
    pvdg_ui_label_set_if_changed(label, text);
}

static void set_note(lv_obj_t *label, const char *text, lv_color_t color)
{
    pvdg_ui_label_set_if_changed(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

lv_obj_t *pvdg_ui_overview_create(lv_obj_t *parent)
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

    lv_obj_t *kpis = lv_obj_create(s_ui.root);
    pvdg_ui_style_root(kpis);
    lv_obj_set_width(kpis, LV_PCT(100));
    lv_obj_set_height(kpis, 62);
    lv_obj_set_layout(kpis, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(kpis, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(kpis, PVDG_UI_GAP_SM, LV_PART_MAIN);

    make_kpi(kpis, "Total Load", lv_color_hex(PVDG_UI_COLOR_LOAD), &s_ui.load_value, &s_ui.load_note);
    make_kpi(kpis, "Solar Power", lv_color_hex(PVDG_UI_COLOR_SOLAR), &s_ui.solar_value, &s_ui.solar_note);
    make_kpi(kpis, "Grid / PCC", lv_color_hex(PVDG_UI_COLOR_GRID), &s_ui.grid_value, &s_ui.grid_note);
    make_kpi(kpis, "Generator", lv_color_hex(PVDG_UI_COLOR_GENERATOR), &s_ui.generator_value, &s_ui.generator_note);
    make_kpi(kpis, "Export Mode", lv_color_hex(PVDG_UI_COLOR_SUCCESS), &s_ui.policy_value, &s_ui.policy_note);

    lv_obj_t *middle = lv_obj_create(s_ui.root);
    pvdg_ui_style_root(middle);
    lv_obj_set_width(middle, LV_PCT(100));
    lv_obj_set_flex_grow(middle, 1);
    lv_obj_set_layout(middle, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(middle, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *flow_card = pvdg_ui_make_card(middle);
    lv_obj_set_flex_grow(flow_card, 7);
    lv_obj_set_height(flow_card, LV_PCT(100));
    lv_obj_set_style_pad_all(flow_card, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *flow_title = make_label(flow_card, "Power Flow", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    lv_obj_set_pos(flow_title, 4, 0);
    lv_obj_t *legend = make_label(flow_card, "Solid: electrical    Dotted: communication", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, -3, 1);

    lv_obj_t *controller = pvdg_ui_make_card(flow_card);
    lv_obj_set_pos(controller, 165, 25);
    lv_obj_set_size(controller, 170, 49);
    lv_obj_set_layout(controller, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controller, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(controller, "PV-DG Energy Controller", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_BODY);
    make_label(controller, "Monitoring / Control", lv_color_hex(PVDG_UI_COLOR_COMM), PVDG_UI_FONT_BODY);

    make_flow_source(flow_card, 16, 93, 135, "Solar", lv_color_hex(PVDG_UI_COLOR_SOLAR), &s_ui.flow_solar_value, &s_ui.flow_solar_state);
    make_flow_source(flow_card, 183, 93, 135, "Generator", lv_color_hex(PVDG_UI_COLOR_GENERATOR), &s_ui.flow_generator_value, &s_ui.flow_generator_state);
    make_flow_source(flow_card, 350, 93, 135, "Grid / PCC", lv_color_hex(PVDG_UI_COLOR_GRID), &s_ui.flow_grid_value, &s_ui.flow_grid_state);

    make_dashed_h(flow_card, 83, 66, 102, lv_color_hex(PVDG_UI_COLOR_COMM));
    make_dashed_v(flow_card, 83, 66, 27, lv_color_hex(PVDG_UI_COLOR_COMM));
    make_dashed_v(flow_card, 250, 73, 20, lv_color_hex(PVDG_UI_COLOR_COMM));
    make_dashed_h(flow_card, 335, 66, 83, lv_color_hex(PVDG_UI_COLOR_COMM));
    make_dashed_v(flow_card, 417, 66, 27, lv_color_hex(PVDG_UI_COLOR_COMM));

    s_ui.solar_power_path = make_segment(flow_card, 82, 157, 3, 43, lv_color_hex(PVDG_UI_COLOR_SOLAR));
    s_ui.generator_power_path = make_segment(flow_card, 249, 157, 3, 43, lv_color_hex(PVDG_UI_COLOR_GENERATOR));
    s_ui.grid_power_path = make_segment(flow_card, 416, 157, 3, 43, lv_color_hex(PVDG_UI_COLOR_GRID));
    make_segment(flow_card, 48, 199, 406, 8, lv_color_hex(PVDG_UI_COLOR_GRID));

    lv_obj_t *bus_label = make_label(flow_card, "SITE BUS", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_BODY);
    lv_obj_set_pos(bus_label, 218, 192);

    s_ui.load_power_path = make_segment(flow_card, 249, 207, 3, 31, lv_color_hex(PVDG_UI_COLOR_LOAD));
    lv_obj_t *load = pvdg_ui_make_card(flow_card);
    lv_obj_set_pos(load, 185, 236);
    lv_obj_set_size(load, 130, 58);
    lv_obj_set_layout(load, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(load, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(load, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(load, "Load", lv_color_hex(PVDG_UI_COLOR_LOAD), PVDG_UI_FONT_BODY);
    s_ui.flow_load_value = make_label(load, "--", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    s_ui.flow_load_state = make_label(load, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);

    s_ui.grid_arrow = make_label(flow_card, LV_SYMBOL_DOWN, lv_color_hex(PVDG_UI_COLOR_GRID), PVDG_UI_FONT_TITLE);
    lv_obj_set_pos(s_ui.grid_arrow, 423, 171);

    s_ui.flow_policy_badge = pvdg_ui_make_badge(flow_card, "Unknown policy", lv_color_hex(PVDG_UI_COLOR_INACTIVE));
    lv_obj_set_pos(s_ui.flow_policy_badge, 332, 235);
    s_ui.flow_policy_note = make_label(flow_card, "PCC export state unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    lv_obj_set_pos(s_ui.flow_policy_note, 332, 268);
    lv_obj_set_width(s_ui.flow_policy_note, 150);
    lv_label_set_long_mode(s_ui.flow_policy_note, LV_LABEL_LONG_WRAP);

    lv_obj_t *right = lv_obj_create(middle);
    pvdg_ui_style_root(right);
    lv_obj_set_flex_grow(right, 3);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *status = pvdg_ui_make_card(right);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_flex_grow(status, 1);
    make_label(status, "System Status", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_TITLE);
    s_ui.alarm_summary = make_label(status, "No live model", lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    lv_obj_set_width(s_ui.alarm_summary, LV_PCT(100));
    lv_label_set_long_mode(s_ui.alarm_summary, LV_LABEL_LONG_WRAP);

    lv_obj_t *note = pvdg_ui_make_card(right);
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_set_flex_grow(note, 1);
    make_label(note, "Data Rules", lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_BODY);
    lv_obj_t *rules = make_label(note,
        "Measured values only. Commands are not measurements. Grid export uses PCC only. Derived load is labelled.",
        lv_color_hex(PVDG_UI_COLOR_MUTED), PVDG_UI_FONT_BODY);
    lv_obj_set_width(rules, LV_PCT(100));
    lv_label_set_long_mode(rules, LV_LABEL_LONG_WRAP);

    lv_obj_t *bottom = lv_obj_create(s_ui.root);
    pvdg_ui_style_root(bottom);
    lv_obj_set_width(bottom, LV_PCT(100));
    lv_obj_set_height(bottom, 74);
    lv_obj_set_layout(bottom, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bottom, PVDG_UI_GAP_SM, LV_PART_MAIN);

    make_source_summary(bottom, "Grid / PCC", lv_color_hex(PVDG_UI_COLOR_GRID), &s_ui.grid_card_value, &s_ui.grid_card_state);
    make_source_summary(bottom, "Solar", lv_color_hex(PVDG_UI_COLOR_SOLAR), &s_ui.solar_card_value, &s_ui.solar_card_state);
    make_source_summary(bottom, "Generator", lv_color_hex(PVDG_UI_COLOR_GENERATOR), &s_ui.generator_card_value, &s_ui.generator_card_state);
    make_source_summary(bottom, "Load", lv_color_hex(PVDG_UI_COLOR_LOAD), &s_ui.load_card_value, &s_ui.load_card_state);

    pvdg_ui_overview_show_unavailable();
    return s_ui.root;
}

void pvdg_ui_overview_apply_model(const pvdg_ui_model_t *model)
{
    if (!model || !s_ui.root) return;

    pvdg_ui_power_flow_t flow = pvdg_ui_resolve_power_flow(model);

    set_measurement(s_ui.load_value, &model->load.power_kw, "kW", 1);
    set_note(s_ui.load_note, model->load.derived ? "Derived source balance" : "Measured",
             model->load.derived ? lv_color_hex(PVDG_UI_COLOR_WARNING) : lv_color_hex(PVDG_UI_COLOR_MUTED));

    set_measurement(s_ui.solar_value, &model->solar.measured_kw, "kW", 1);
    set_note(s_ui.solar_note, model->solar.measured_kw.available ? "Measured inverter telemetry" : "Measurement unavailable",
             model->solar.measured_kw.available ? lv_color_hex(PVDG_UI_COLOR_MUTED) : lv_color_hex(PVDG_UI_COLOR_WARNING));

    pvdg_ui_measurement_t grid_abs = model->grid.power_kw;
    if (grid_abs.available && grid_abs.value < 0.0) grid_abs.value = -grid_abs.value;
    set_measurement(s_ui.grid_value, &grid_abs, "kW", 1);
    set_note(s_ui.grid_note, pvdg_ui_grid_flow_label(flow.grid_flow), lv_color_hex(PVDG_UI_COLOR_GRID));

    set_measurement(s_ui.generator_value, &model->generator.measured_kw, "kW", 1);
    set_note(s_ui.generator_note, model->generator.running ? "Running" : "Standby",
             model->generator.running ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) : lv_color_hex(PVDG_UI_COLOR_MUTED));

    pvdg_ui_label_set_if_changed(s_ui.policy_value, model->export_policy.label[0] ? model->export_policy.label : "Unknown");
    set_note(s_ui.policy_note,
             flow.zero_export_blocked ? "No power exported to grid" :
             flow.reverse_flow_permitted ? "PCC export permitted" : "Policy unavailable",
             flow.zero_export_blocked ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) : lv_color_hex(PVDG_UI_COLOR_MUTED));

    set_measurement(s_ui.flow_solar_value, &model->solar.measured_kw, "kW", 1);
    set_note(s_ui.flow_solar_state, flow.solar_feeding_bus ? "Feeding Site Bus" : "No measured flow",
             flow.solar_feeding_bus ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) : lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_measurement(s_ui.flow_generator_value, &model->generator.measured_kw, "kW", 1);
    set_note(s_ui.flow_generator_state, flow.generator_feeding_bus ? "Feeding Site Bus" : (model->generator.running ? "Running / no measured kW" : "Standby"),
             flow.generator_feeding_bus ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) : lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_measurement(s_ui.flow_grid_value, &grid_abs, "kW", 1);
    set_note(s_ui.flow_grid_state, pvdg_ui_grid_flow_label(flow.grid_flow), lv_color_hex(PVDG_UI_COLOR_GRID));
    set_measurement(s_ui.flow_load_value, &model->load.power_kw, "kW", 1);
    set_note(s_ui.flow_load_state, model->load.derived ? "Derived" : "Measured",
             model->load.derived ? lv_color_hex(PVDG_UI_COLOR_WARNING) : lv_color_hex(PVDG_UI_COLOR_SUCCESS));

    if (flow.grid_flow == PVDG_UI_GRID_FLOW_EXPORT) {
        pvdg_ui_label_set_if_changed(s_ui.grid_arrow, LV_SYMBOL_UP);
    } else if (flow.grid_flow == PVDG_UI_GRID_FLOW_IMPORT) {
        pvdg_ui_label_set_if_changed(s_ui.grid_arrow, LV_SYMBOL_DOWN);
    } else {
        pvdg_ui_label_set_if_changed(s_ui.grid_arrow, "-");
    }

    if (flow.solar_feeding_bus) lv_obj_remove_flag(s_ui.solar_power_path, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.solar_power_path, LV_OBJ_FLAG_HIDDEN);
    if (flow.generator_feeding_bus) lv_obj_remove_flag(s_ui.generator_power_path, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.generator_power_path, LV_OBJ_FLAG_HIDDEN);
    if (flow.grid_flow == PVDG_UI_GRID_FLOW_UNKNOWN) lv_obj_add_flag(s_ui.grid_power_path, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_ui.grid_power_path, LV_OBJ_FLAG_HIDDEN);
    if (flow.load_consuming) lv_obj_remove_flag(s_ui.load_power_path, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.load_power_path, LV_OBJ_FLAG_HIDDEN);

    lv_color_t policy_color = flow.zero_export_blocked ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) :
                              flow.reverse_flow_permitted ? lv_color_hex(PVDG_UI_COLOR_GRID) :
                              lv_color_hex(PVDG_UI_COLOR_INACTIVE);
    pvdg_ui_badge_set(s_ui.flow_policy_badge,
                      model->export_policy.label[0] ? model->export_policy.label : "Unknown policy",
                      policy_color);
    pvdg_ui_label_set_if_changed(s_ui.flow_policy_note,
        flow.zero_export_blocked ? "Reverse flow blocked at PCC" :
        flow.reverse_flow_permitted ? "Export only through Site Bus / PCC" :
        "Export policy unavailable");

    set_measurement(s_ui.grid_card_value, &grid_abs, "kW", 1);
    set_note(s_ui.grid_card_state, pvdg_ui_grid_flow_label(flow.grid_flow), lv_color_hex(PVDG_UI_COLOR_GRID));
    set_measurement(s_ui.solar_card_value, &model->solar.measured_kw, "kW", 1);
    set_note(s_ui.solar_card_state, model->solar.measured_kw.available ? "Measured" : "Unavailable",
             model->solar.measured_kw.available ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) : lv_color_hex(PVDG_UI_COLOR_WARNING));
    set_measurement(s_ui.generator_card_value, &model->generator.measured_kw, "kW", 1);
    set_note(s_ui.generator_card_state, model->generator.running ? "Running" : "Standby",
             model->generator.running ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) : lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_measurement(s_ui.load_card_value, &model->load.power_kw, "kW", 1);
    set_note(s_ui.load_card_state, model->load.derived ? "Derived" : "Measured",
             model->load.derived ? lv_color_hex(PVDG_UI_COLOR_WARNING) : lv_color_hex(PVDG_UI_COLOR_SUCCESS));

    char summary[160];
    if (model->alarm_count > 0U) {
        snprintf(summary, sizeof(summary), "%u active alarm%s\n%s",
                 (unsigned)model->alarm_count, model->alarm_count == 1U ? "" : "s",
                 model->alarms[0]);
        set_note(s_ui.alarm_summary, summary, lv_color_hex(PVDG_UI_COLOR_WARNING));
    } else {
        snprintf(summary, sizeof(summary), "System normal\nGrid: %s\nControl: %s",
                 pvdg_ui_grid_flow_label(flow.grid_flow),
                 model->controller.authority_label[0] ? model->controller.authority_label : "Unknown");
        set_note(s_ui.alarm_summary, summary, lv_color_hex(PVDG_UI_COLOR_SUCCESS));
    }
}

void pvdg_ui_overview_show_unavailable(void)
{
    if (!s_ui.root) return;
    const char *dash = "--";
    pvdg_ui_label_set_if_changed(s_ui.load_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.solar_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.grid_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.generator_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.policy_value, "Unknown");
    pvdg_ui_label_set_if_changed(s_ui.flow_solar_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.flow_generator_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.flow_grid_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.flow_load_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.grid_card_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.solar_card_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.generator_card_value, dash);
    pvdg_ui_label_set_if_changed(s_ui.load_card_value, dash);
    set_note(s_ui.load_note, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_note(s_ui.solar_note, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_note(s_ui.grid_note, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_note(s_ui.generator_note, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_note(s_ui.policy_note, "Unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED));
    set_note(s_ui.alarm_summary, "Live model unavailable", lv_color_hex(PVDG_UI_COLOR_MUTED));
}
