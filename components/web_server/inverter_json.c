#include "inverter_json.h"

/* A measured quantity, or null. Never 0.0 as a stand-in: "0.0 V" claims the
 * machine measured zero volts, which is a different fault from not having read
 * it, and the two send an engineer to different places. */
static void add_measured(cJSON *parent, const char *name, bool available, double value)
{
    if (available) cJSON_AddNumberToObject(parent, name, value);
    else cJSON_AddNullToObject(parent, name);
}

static void add_measured_array(cJSON *parent, const char *name, bool available,
                               const float *values, int count)
{
    cJSON *array = cJSON_AddArrayToObject(parent, name);
    for (int index = 0; index < count; ++index) {
        cJSON_AddItemToArray(array, available ? cJSON_CreateNumber(values[index])
                                              : cJSON_CreateNull());
    }
}

void inverter_json_add_measurements(cJSON *parent, const inverter_data_t *data,
                                    uint32_t current_ms)
{
    cJSON *object = cJSON_AddObjectToObject(parent, "measurements");
    const inverter_measurements_t *m = &data->measurements;
    const bool ok = m->valid;

    cJSON_AddBoolToObject(object, "available", ok);
    if (ok && data->measurements_updated_ms != 0) {
        cJSON_AddNumberToObject(object, "age_ms", current_ms - data->measurements_updated_ms);
    } else {
        cJSON_AddNullToObject(object, "age_ms");
    }

    cJSON *dc = cJSON_AddObjectToObject(object, "dc");
    add_measured_array(dc, "string_voltage_v", ok, m->pv_voltage_v, INVERTER_PV_STRINGS);
    add_measured_array(dc, "string_current_a", ok, m->pv_current_a, INVERTER_PV_STRINGS);
    add_measured(dc, "power_kw", ok, m->dc_power_kw);

    cJSON *ac = cJSON_AddObjectToObject(object, "ac");
    add_measured_array(ac, "line_voltage_v", ok, m->line_voltage_v, 3);
    add_measured_array(ac, "phase_voltage_v", ok, m->phase_voltage_v, 3);
    add_measured_array(ac, "phase_current_a", ok, m->phase_current_a, 3);
    add_measured(ac, "active_power_kw", ok, m->active_power_kw);
    add_measured(ac, "reactive_power_kvar", ok, m->reactive_power_kvar);
    add_measured(ac, "peak_active_power_today_kw", ok, m->peak_active_power_today_kw);
    add_measured(ac, "power_factor", ok, m->power_factor);
    add_measured(ac, "frequency_hz", ok, m->frequency_hz);

    cJSON *device = cJSON_AddObjectToObject(object, "device");
    add_measured(device, "efficiency_percent", ok, m->efficiency_percent);
    add_measured(device, "internal_temperature_c", ok, m->internal_temperature_c);
    add_measured(device, "insulation_resistance_mohm", ok, m->insulation_resistance_mohm);
    /*
     * RAW, AND SAID SO IN THE FIELD NAME.
     *
     * Device Status is an enumeration whose code table the manufacturer defers
     * to a separate document this project does not hold, and Fault Code indexes
     * a table that is likewise absent. A label invented for either would be a
     * guess wearing the clothes of a diagnosis -- and a wrong diagnosis on an
     * inverter page is worse than no diagnosis, because it stops the search.
     *
     * The raw number is genuinely useful: it is what a person quotes to the
     * manufacturer's support line. So it is published, named "_raw", and the
     * interface prints it as a code rather than as a word.
     */
    if (ok) {
        cJSON_AddNumberToObject(device, "status_raw", m->device_status_raw);
        cJSON_AddNumberToObject(device, "fault_code_raw", m->fault_code_raw);
    } else {
        cJSON_AddNullToObject(device, "status_raw");
        cJSON_AddNullToObject(device, "fault_code_raw");
    }

    cJSON *energy = cJSON_AddObjectToObject(object, "energy");
    add_measured(energy, "today_kwh", ok, m->daily_yield_kwh);
    add_measured(energy, "month_kwh", ok, m->month_yield_kwh);
    add_measured(energy, "total_kwh", ok, m->total_yield_kwh);
    add_measured(energy, "total_dc_input_kwh", ok, m->total_dc_input_kwh);
}
