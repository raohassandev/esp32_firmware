#include "pvdg_ui_meter_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *text)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", text ? text : "");
}

static size_t bounded_length(const char *text, size_t capacity)
{
    if (!text) return capacity;
    size_t n = 0U;
    while (n < capacity && text[n]) n++;
    return n;
}

void pvdg_ui_meter_defaults(pvdg_ui_meter_config_t *meter, uint8_t index)
{
    if (!meter) return;
    memset(meter, 0, sizeof(*meter));
    snprintf(meter->name, sizeof(meter->name), "Meter %u", (unsigned)index + 1U);
    meter->port = 502U;
    meter->unit_id = (uint8_t)(index + 1U);
    meter->timeout_ms = 500U;
    meter->function_code = 3U;
    meter->data_type = PVDG_UI_MODBUS_INT32;
    meter->word_order = PVDG_UI_ORDER_ABCD;
    meter->scale = 1.0;
    meter->poll_ms = 1000U;
    meter->role = PVDG_UI_METER_ROLE_UNASSIGNED;
    meter->generator_index = PVDG_UI_METER_GENERATOR_NONE;
}

static pvdg_ui_meter_config_result_t validate_meter(
    const pvdg_ui_meter_config_t *meter, uint8_t index,
    char *error, size_t error_size)
{
    char message[192];
    if (!meter) return PVDG_UI_METER_CONFIG_INVALID_ARGUMENT;
    const size_t name_len = bounded_length(meter->name, sizeof(meter->name));
    const size_t host_len = bounded_length(meter->host, sizeof(meter->host));
    if (name_len == 0U || name_len >= sizeof(meter->name)) {
        snprintf(message, sizeof(message), "Meter %u requires a name shorter than 24 bytes.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_NAME_INVALID;
    }
    if (host_len >= sizeof(meter->host) || meter->port == 0U ||
        meter->unit_id == 0U || meter->unit_id > 247U ||
        meter->timeout_ms < 100U || meter->timeout_ms > 60000U) {
        snprintf(message, sizeof(message), "Meter %u endpoint must use port 1-65535, unit ID 1-247 and timeout 100-60000 ms.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_ENDPOINT_INVALID;
    }
    if (meter->enabled && host_len == 0U) {
        snprintf(message, sizeof(message), "Enabled meter %u requires a host.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_ENDPOINT_INVALID;
    }
    if (meter->role > PVDG_UI_METER_ROLE_PV ||
        (meter->role == PVDG_UI_METER_ROLE_GENERATOR && meter->generator_index >= 3U) ||
        (meter->role != PVDG_UI_METER_ROLE_GENERATOR && meter->generator_index != PVDG_UI_METER_GENERATOR_NONE)) {
        snprintf(message, sizeof(message), "Meter %u role/generator assignment is invalid.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_ROLE_INVALID;
    }
    if ((meter->function_code != 3U && meter->function_code != 4U) ||
        meter->data_type > PVDG_UI_MODBUS_FLOAT32 ||
        meter->word_order > PVDG_UI_ORDER_DCBA) {
        snprintf(message, sizeof(message), "Meter %u active-power register format is invalid.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_REGISTER_INVALID;
    }
    if (!isfinite(meter->scale) || meter->scale == 0.0 || fabs(meter->scale) > 1000000.0) {
        snprintf(message, sizeof(message), "Meter %u scale must be finite, non-zero and within +/-1,000,000.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_SCALE_INVALID;
    }
    if (meter->poll_ms < 100U || meter->poll_ms > 60000U) {
        snprintf(message, sizeof(message), "Meter %u poll interval must be 100-60000 ms.", (unsigned)index + 1U);
        set_error(error, error_size, message);
        return PVDG_UI_METER_CONFIG_TIMING_INVALID;
    }
    return PVDG_UI_METER_CONFIG_OK;
}

pvdg_ui_meter_config_result_t pvdg_ui_meter_list_validate(
    const pvdg_ui_meter_list_t *list, char *error, size_t error_size)
{
    set_error(error, error_size, "");
    if (!list) return PVDG_UI_METER_CONFIG_INVALID_ARGUMENT;
    if (list->count > PVDG_UI_MAX_METERS) {
        set_error(error, error_size, "Meter count exceeds the controller limit of four.");
        return PVDG_UI_METER_CONFIG_COUNT_INVALID;
    }
    for (uint8_t i = 0U; i < list->count; ++i) {
        pvdg_ui_meter_config_result_t result = validate_meter(&list->meters[i], i, error, error_size);
        if (result != PVDG_UI_METER_CONFIG_OK) return result;
    }
    for (uint8_t left = 0U; left < list->count; ++left) {
        if (!list->meters[left].enabled) continue;
        for (uint8_t right = (uint8_t)(left + 1U); right < list->count; ++right) {
            if (!list->meters[right].enabled) continue;
            const pvdg_ui_meter_config_t *a = &list->meters[left];
            const pvdg_ui_meter_config_t *b = &list->meters[right];
            if (a->port == b->port && a->unit_id == b->unit_id && strcmp(a->host, b->host) == 0) {
                char message[160];
                snprintf(message, sizeof(message), "Meters %u and %u use the same enabled host, port and unit ID.", (unsigned)left + 1U, (unsigned)right + 1U);
                set_error(error, error_size, message);
                return PVDG_UI_METER_CONFIG_DUPLICATE_ENDPOINT;
            }
        }
    }
    return PVDG_UI_METER_CONFIG_OK;
}

pvdg_ui_meter_role_state_t pvdg_ui_meter_role_state(const pvdg_ui_meter_list_t *list)
{
    pvdg_ui_meter_role_state_t state = {0};
    if (!list || list->count > PVDG_UI_MAX_METERS) return state;
    for (uint8_t i = 0U; i < list->count; ++i) {
        const pvdg_ui_meter_config_t *meter = &list->meters[i];
        if (!meter->enabled) continue;
        if (meter->role == PVDG_UI_METER_ROLE_GRID) {
            state.grid_count++;
        } else if (meter->role == PVDG_UI_METER_ROLE_GENERATOR && meter->generator_index < 3U) {
            state.generator_count[meter->generator_index]++;
            if (state.generator_count[meter->generator_index] > 1U) state.duplicate_generator = true;
        }
    }
    state.valid = state.grid_count == 1U && !state.duplicate_generator;
    return state;
}
