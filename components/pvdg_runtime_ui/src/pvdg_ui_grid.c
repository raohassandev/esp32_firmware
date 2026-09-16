#include "pvdg_ui_grid.h"

#include <stdio.h>
#include <string.h>

#include "pvdg_ui_theme.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *state_badge;
    lv_obj_t *import_kw;
    lv_obj_t *export_kw;
    lv_obj_t *voltage_l1;
    lv_obj_t *voltage_l2;
    lv_obj_t *voltage_l3;
    lv_obj_t *current_l1;
    lv_obj_t *current_l2;
    lv_obj_t *current_l3;
    lv_obj_t *frequency;
    lv_obj_t *pf;
    lv_obj_t *reactive;
    lv_obj_t *apparent;
    lv_obj_t *import_energy;
    lv_obj_t *export_energy;
    lv_obj_t *policy;
    lv_obj_t *breaker;
    lv_obj_t *meter;
    lv_obj_t *quality;
} grid_ui_t;

static grid_ui_t s_ui;

static void set_measurement(lv_obj_t *label, const pvdg_ui_measurement_t *m,
                            const char *unit, unsigned decimals)
{
    char text[40];
    pvdg_ui_format_measurement(text, sizeof(text), m, unit, decimals);
    pvdg_ui_label_set_if_changed(label, text);
}

static lv_obj_t *section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = pvdg_ui_make_card(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 2, LV_PART_MAIN);
    pvdg_ui_make_title(card, title);
    return card;
}

static void add_phase_row(lv_obj_t *parent, const char *name,
                          lv_obj_t **voltage, lv_obj_t **current)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 28);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *phase = lv_label_create(row);
    lv_label_set_text(phase, name);
    pvdg_ui_style_label(phase, lv_color_hex(PVDG_UI_COLOR_GRID), PVDG_UI_FONT_BODY);
    lv_obj_set_width(phase, 50);

    *voltage = lv_label_create(row);
    lv_label_set_text(*voltage, "--");
    pvdg_ui_style_label(*voltage, lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_BODY);
    lv_obj_set_width(*voltage, 90);
    lv_obj_set_style_text_align(*voltage, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    *current = lv_label_create(row);
    lv_label_set_text(*current, "--");
    pvdg_ui_style_label(*current, lv_color_hex(PVDG_UI_COLOR_TEXT), PVDG_UI_FONT_BODY);
    lv_obj_set_width(*current, 90);
    lv_obj_set_style_text_align(*current, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

lv_obj_t *pvdg_ui_grid_create(lv_obj_t *parent)
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
    pvdg_ui_make_title(head, "Grid / PCC");
    s_ui.state_badge = pvdg_ui_make_badge(head, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));

    lv_obj_t *body = lv_obj_create(s_ui.root);
    pvdg_ui_style_root(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_layout(body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *left = lv_obj_create(body);
    pvdg_ui_style_root(left);
    lv_obj_set_flex_grow(left, 6);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *flow = section(left, "PCC Power Flow");
    lv_obj_set_height(flow, 105);
    lv_obj_t *flow_row = lv_obj_create(flow);
    lv_obj_remove_style_all(flow_row);
    lv_obj_set_width(flow_row, LV_PCT(100));
    lv_obj_set_flex_grow(flow_row, 1);
    lv_obj_set_layout(flow_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(flow_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(flow_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *import_box = pvdg_ui_make_card(flow_row);
    lv_obj_set_size(import_box, 190, 62);
    lv_obj_set_layout(import_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(import_box, LV_FLEX_FLOW_COLUMN);
    pvdg_ui_make_muted(import_box, "Utility -> Site Import");
    s_ui.import_kw = lv_label_create(import_box);
    pvdg_ui_style_label(s_ui.import_kw, lv_color_hex(PVDG_UI_COLOR_GRID), PVDG_UI_FONT_HERO);

    lv_obj_t *export_box = pvdg_ui_make_card(flow_row);
    lv_obj_set_size(export_box, 190, 62);
    lv_obj_set_layout(export_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(export_box, LV_FLEX_FLOW_COLUMN);
    pvdg_ui_make_muted(export_box, "Site -> Utility Export");
    s_ui.export_kw = lv_label_create(export_box);
    pvdg_ui_style_label(s_ui.export_kw, lv_color_hex(PVDG_UI_COLOR_SUCCESS), PVDG_UI_FONT_HERO);

    lv_obj_t *phase = section(left, "Phase Measurements");
    lv_obj_set_flex_grow(phase, 1);
    add_phase_row(phase, "L1", &s_ui.voltage_l1, &s_ui.current_l1);
    add_phase_row(phase, "L2", &s_ui.voltage_l2, &s_ui.current_l2);
    add_phase_row(phase, "L3", &s_ui.voltage_l3, &s_ui.current_l3);

    lv_obj_t *right = lv_obj_create(body);
    pvdg_ui_style_root(right);
    lv_obj_set_flex_grow(right, 4);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_layout(right, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, PVDG_UI_GAP_SM, LV_PART_MAIN);

    lv_obj_t *power = section(right, "Power / Quality");
    lv_obj_set_flex_grow(power, 1);
    pvdg_ui_make_metric_row(power, "Frequency", &s_ui.frequency);
    pvdg_ui_make_metric_row(power, "Power factor", &s_ui.pf);
    pvdg_ui_make_metric_row(power, "Reactive power", &s_ui.reactive);
    pvdg_ui_make_metric_row(power, "Apparent power", &s_ui.apparent);
    pvdg_ui_make_metric_row(power, "Quality / age", &s_ui.quality);

    lv_obj_t *energy = section(right, "Energy / Policy");
    lv_obj_set_flex_grow(energy, 1);
    pvdg_ui_make_metric_row(energy, "Import energy", &s_ui.import_energy);
    pvdg_ui_make_metric_row(energy, "Export energy", &s_ui.export_energy);
    pvdg_ui_make_metric_row(energy, "Export policy", &s_ui.policy);
    pvdg_ui_make_metric_row(energy, "Grid breaker", &s_ui.breaker);
    pvdg_ui_make_metric_row(energy, "Meter", &s_ui.meter);

    pvdg_ui_grid_show_unavailable();
    return s_ui.root;
}

void pvdg_ui_grid_apply_model(const pvdg_ui_model_t *model)
{
    if (!model || !s_ui.root) return;
    pvdg_ui_power_flow_t flow = pvdg_ui_resolve_power_flow(model);
    const bool flow_current = flow.grid_flow != PVDG_UI_GRID_FLOW_UNKNOWN;

    pvdg_ui_measurement_t import_m = {.available = flow_current,
                                      .value = flow.grid_import_kw,
                                      .quality = model->grid.power_kw.quality};
    pvdg_ui_measurement_t export_m = {.available = flow_current,
                                      .value = flow.grid_export_kw,
                                      .quality = model->grid.power_kw.quality};
    set_measurement(s_ui.import_kw, &import_m, "kW", 1);
    set_measurement(s_ui.export_kw, &export_m, "kW", 1);

    set_measurement(s_ui.voltage_l1, &model->grid.voltage_l1_v, "V", 1);
    set_measurement(s_ui.voltage_l2, &model->grid.voltage_l2_v, "V", 1);
    set_measurement(s_ui.voltage_l3, &model->grid.voltage_l3_v, "V", 1);
    set_measurement(s_ui.current_l1, &model->grid.current_l1_a, "A", 1);
    set_measurement(s_ui.current_l2, &model->grid.current_l2_a, "A", 1);
    set_measurement(s_ui.current_l3, &model->grid.current_l3_a, "A", 1);
    set_measurement(s_ui.frequency, &model->grid.frequency_hz, "Hz", 1);
    set_measurement(s_ui.pf, &model->grid.power_factor, "", 2);
    set_measurement(s_ui.reactive, &model->grid.reactive_kvar, "kvar", 1);
    set_measurement(s_ui.apparent, &model->grid.apparent_kva, "kVA", 1);
    set_measurement(s_ui.import_energy, &model->grid.imported_energy_kwh, "kWh", 1);
    set_measurement(s_ui.export_energy, &model->grid.exported_energy_kwh, "kWh", 1);

    pvdg_ui_label_set_if_changed(s_ui.policy,
        model->export_policy.label[0] ? model->export_policy.label : "Unknown");
    pvdg_ui_label_set_if_changed(s_ui.breaker,
        !model->grid.breaker_known ? "Unknown" : model->grid.breaker_closed ? "Closed" : "Open");
    pvdg_ui_label_set_if_changed(s_ui.meter,
        model->grid.meter_name[0] ? model->grid.meter_name : "--");

    char quality[64];
    if (model->grid.power_kw.has_age_ms) {
        snprintf(quality, sizeof(quality), "%s · %lu ms",
                 pvdg_ui_quality_label(model->grid.power_kw.quality),
                 (unsigned long)model->grid.power_kw.age_ms);
    } else {
        snprintf(quality, sizeof(quality), "%s",
                 pvdg_ui_quality_label(model->grid.power_kw.quality));
    }
    pvdg_ui_label_set_if_changed(s_ui.quality, quality);
    lv_obj_set_style_text_color(
        s_ui.quality,
        lv_color_hex(model->grid.power_kw.quality == PVDG_UI_QUALITY_GOOD
                         ? PVDG_UI_COLOR_SUCCESS
                         : model->grid.power_kw.quality == PVDG_UI_QUALITY_STALE ||
                                   model->grid.power_kw.quality == PVDG_UI_QUALITY_DEGRADED
                               ? PVDG_UI_COLOR_WARNING
                               : PVDG_UI_COLOR_INACTIVE),
        LV_PART_MAIN);

    lv_color_t state_color = flow.grid_flow == PVDG_UI_GRID_FLOW_IMPORT ? lv_color_hex(PVDG_UI_COLOR_GRID) :
                             flow.grid_flow == PVDG_UI_GRID_FLOW_EXPORT ? lv_color_hex(PVDG_UI_COLOR_SUCCESS) :
                             lv_color_hex(PVDG_UI_COLOR_INACTIVE);
    pvdg_ui_badge_set(s_ui.state_badge, pvdg_ui_grid_flow_label(flow.grid_flow), state_color);
}

void pvdg_ui_grid_show_unavailable(void)
{
    if (!s_ui.root) return;
    lv_obj_t *labels[] = {
        s_ui.import_kw, s_ui.export_kw, s_ui.voltage_l1, s_ui.voltage_l2, s_ui.voltage_l3,
        s_ui.current_l1, s_ui.current_l2, s_ui.current_l3, s_ui.frequency, s_ui.pf,
        s_ui.reactive, s_ui.apparent, s_ui.import_energy, s_ui.export_energy, s_ui.policy,
        s_ui.breaker, s_ui.meter, s_ui.quality,
    };
    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        pvdg_ui_label_set_if_changed(labels[i], "--");
    }
    pvdg_ui_badge_set(s_ui.state_badge, "Unavailable", lv_color_hex(PVDG_UI_COLOR_INACTIVE));
}
