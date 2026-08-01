#include "meter_json.h"

void meter_json_add_age(cJSON *parent, const char *name, bool available,
                        uint32_t current_ms, uint32_t event_ms)
{
    if (available) cJSON_AddNumberToObject(parent, name, current_ms - event_ms);
    else cJSON_AddNullToObject(parent, name);
}

/* A measured quantity, or null.
 *
 * Never 0.0 as a stand-in. On a power screen "0.0 V" and "not measured" are
 * different plants, and a reader who cannot tell them apart will diagnose the
 * wrong one. cJSON emits null for a value the firmware does not have, and the
 * interface renders an em dash for it. */
static void add_measured(cJSON *parent, const char *name, bool available, double value)
{
    if (available) cJSON_AddNumberToObject(parent, name, value);
    else cJSON_AddNullToObject(parent, name);
}

static void add_measured_triple(cJSON *parent, const char *name, bool available,
                                const float *values)
{
    cJSON *array = cJSON_AddArrayToObject(parent, name);
    for (int phase = 0; phase < 3; ++phase) {
        cJSON_AddItemToArray(array, available ? cJSON_CreateNumber(values[phase])
                                              : cJSON_CreateNull());
    }
}

void meter_json_add_phase_power(cJSON *parent, const meter_data_t *data, bool has_data)
{
    cJSON *array = cJSON_AddArrayToObject(parent, "phase_power_kw");
    for (int phase = 0; phase < 3; ++phase) {
        bool valid = has_data && data->phase_valid[phase];
        cJSON_AddItemToArray(array, valid ? cJSON_CreateNumber(data->phase_power_kw[phase])
                                          : cJSON_CreateNull());
    }
}

/*
 * THE FULL INSTANTANEOUS SET, exactly as the meter reported it.
 *
 * Nothing here is derived and nothing is recomputed: every field is one register
 * pair the manual names. That is the point of the block on a screen -- it is the
 * evidence that the instrument is wired the way the drawing says, and evidence
 * that has been massaged in transit is not evidence.
 *
 * In particular the "total" fields are the METER'S own whole-installation
 * figures, not the sum of the three phases. Substituting a sum computed here
 * would hide precisely the disagreement that reveals a CT on the wrong phase.
 */
void meter_json_add_measurements(cJSON *parent, const meter_data_t *data, uint32_t current_ms)
{
    cJSON *object = cJSON_AddObjectToObject(parent, "measurements");
    const em500_measurements_t *m = &data->measurements;
    const bool ok = m->valid;

    cJSON_AddBoolToObject(object, "available", ok);
    meter_json_add_age(object, "age_ms", ok && data->measurements_updated_ms != 0,
                       current_ms, data->measurements_updated_ms);

    add_measured_triple(object, "phase_voltage_v", ok, m->phase_voltage_v);
    add_measured_triple(object, "line_voltage_v", ok, m->line_voltage_v);
    add_measured_triple(object, "current_a", ok, m->current_a);
    add_measured_triple(object, "active_power_kw", ok, m->active_power_kw);
    add_measured_triple(object, "reactive_power_kvar", ok, m->reactive_power_kvar);
    add_measured_triple(object, "apparent_power_kva", ok, m->apparent_power_kva);
    add_measured_triple(object, "power_factor", ok, m->power_factor);

    add_measured(object, "frequency_hz", ok, m->frequency_hz);
    add_measured(object, "equivalent_phase_voltage_v", ok, m->equivalent_phase_voltage_v);
    add_measured(object, "equivalent_line_voltage_v", ok, m->equivalent_line_voltage_v);
    add_measured(object, "equivalent_current_a", ok, m->equivalent_current_a);
    add_measured(object, "total_active_power_kw", ok, m->total_active_power_kw);
    add_measured(object, "total_reactive_power_kvar", ok, m->total_reactive_power_kvar);
    add_measured(object, "total_apparent_power_kva", ok, m->total_apparent_power_kva);
    add_measured(object, "total_power_factor", ok, m->total_power_factor);
    add_measured(object, "voltage_asymmetry_line_percent", ok, m->voltage_asymmetry_line_percent);
    add_measured(object, "voltage_asymmetry_phase_percent", ok, m->voltage_asymmetry_phase_percent);
    add_measured(object, "current_asymmetry_percent", ok, m->current_asymmetry_percent);
    add_measured(object, "neutral_current_a", ok, m->neutral_current_a);
}

void meter_json_add_energy(cJSON *parent, const meter_data_t *data, uint32_t current_ms)
{
    cJSON *object = cJSON_AddObjectToObject(parent, "energy");
    const em500_energy_t *e = &data->energy;
    const bool ok = e->valid;

    cJSON_AddBoolToObject(object, "available", ok);
    meter_json_add_age(object, "age_ms", ok && data->energy_updated_ms != 0,
                       current_ms, data->energy_updated_ms);

    add_measured(object, "total_import_active_kwh", ok, e->total_import_active_kwh);
    add_measured(object, "total_export_active_kwh", ok, e->total_export_active_kwh);
    add_measured(object, "total_import_reactive_kvarh", ok, e->total_import_reactive_kvarh);
    add_measured(object, "total_export_reactive_kvarh", ok, e->total_export_reactive_kvarh);
    add_measured(object, "total_apparent_kvah", ok, e->total_apparent_kvah);
    add_measured(object, "partial_import_active_kwh", ok, e->partial_import_active_kwh);
    add_measured(object, "partial_export_active_kwh", ok, e->partial_export_active_kwh);
    add_measured(object, "partial_import_reactive_kvarh", ok, e->partial_import_reactive_kvarh);
    add_measured(object, "partial_export_reactive_kvarh", ok, e->partial_export_reactive_kvarh);
    add_measured(object, "partial_apparent_kvah", ok, e->partial_apparent_kvah);
}
