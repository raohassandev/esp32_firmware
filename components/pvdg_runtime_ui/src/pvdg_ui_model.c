#include "pvdg_ui_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void pvdg_ui_model_reset(pvdg_ui_model_t *model)
{
    if (!model) return;
    memset(model, 0, sizeof(*model));
    model->grid.power_kw.quality = PVDG_UI_QUALITY_UNKNOWN;
    model->solar.measured_kw.quality = PVDG_UI_QUALITY_UNKNOWN;
    model->generator.measured_kw.quality = PVDG_UI_QUALITY_UNKNOWN;
    model->load.power_kw.quality = PVDG_UI_QUALITY_UNKNOWN;
    model->export_policy.kind = PVDG_UI_EXPORT_POLICY_UNKNOWN;
}

static bool flow_measurement_fresh(const pvdg_ui_measurement_t *measurement)
{
    return measurement && measurement->available &&
           measurement->quality == PVDG_UI_QUALITY_GOOD &&
           isfinite(measurement->value);
}

pvdg_ui_power_flow_t pvdg_ui_resolve_power_flow(const pvdg_ui_model_t *model)
{
    pvdg_ui_power_flow_t flow = {0};
    if (!model) return flow;

    flow.solar_feeding_bus = flow_measurement_fresh(&model->solar.measured_kw) &&
                             model->solar.measured_kw.value > 0.01;
    flow.generator_feeding_bus = model->generator.running &&
                                 flow_measurement_fresh(&model->generator.measured_kw) &&
                                 model->generator.measured_kw.value > 0.01;
    flow.load_consuming = flow_measurement_fresh(&model->load.power_kw) &&
                          model->load.power_kw.value > 0.01;

    flow.reverse_flow_permitted =
        model->export_policy.kind == PVDG_UI_EXPORT_POLICY_EXPORT_ENABLED;
    flow.zero_export_blocked =
        model->export_policy.kind == PVDG_UI_EXPORT_POLICY_ZERO_EXPORT;

    /* A stale/degraded PCC value may still be displayed with its quality/age,
     * but it must not be animated or presented as the current flow direction. */
    if (!flow_measurement_fresh(&model->grid.power_kw)) {
        flow.grid_flow = PVDG_UI_GRID_FLOW_UNKNOWN;
        return flow;
    }

    const double grid_kw = model->grid.power_kw.value;
    if (fabs(grid_kw) < 0.01) {
        flow.grid_flow = PVDG_UI_GRID_FLOW_IDLE;
    } else if (grid_kw > 0.0) {
        flow.grid_flow = PVDG_UI_GRID_FLOW_IMPORT;
        flow.grid_import_kw = grid_kw;
    } else {
        flow.grid_flow = PVDG_UI_GRID_FLOW_EXPORT;
        flow.grid_export_kw = -grid_kw;
    }
    return flow;
}

const char *pvdg_ui_quality_label(pvdg_ui_quality_t quality)
{
    switch (quality) {
        case PVDG_UI_QUALITY_GOOD: return "Good";
        case PVDG_UI_QUALITY_STALE: return "Stale";
        case PVDG_UI_QUALITY_DEGRADED: return "Degraded";
        case PVDG_UI_QUALITY_UNAVAILABLE: return "Unavailable";
        case PVDG_UI_QUALITY_UNKNOWN:
        default: return "Unknown";
    }
}

const char *pvdg_ui_grid_flow_label(pvdg_ui_grid_flow_t flow)
{
    switch (flow) {
        case PVDG_UI_GRID_FLOW_IMPORT: return "Importing";
        case PVDG_UI_GRID_FLOW_EXPORT: return "Exporting";
        case PVDG_UI_GRID_FLOW_IDLE: return "Idle";
        case PVDG_UI_GRID_FLOW_UNKNOWN:
        default: return "Unknown";
    }
}

void pvdg_ui_format_measurement(char *out, size_t out_size,
                                const pvdg_ui_measurement_t *measurement,
                                const char *unit, unsigned decimals)
{
    if (!out || out_size == 0U) return;
    if (!measurement || !measurement->available || !isfinite(measurement->value)) {
        snprintf(out, out_size, "--");
        return;
    }
    if (decimals > 3U) decimals = 3U;
    snprintf(out, out_size, "%.*f%s%s", (int)decimals, measurement->value,
             unit && unit[0] ? " " : "", unit ? unit : "");
}
