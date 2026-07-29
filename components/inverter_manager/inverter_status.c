#include "inverter_status.h"

bool inverter_status_function_is_read_only(uint8_t function_code)
{
    return function_code == INVERTER_STATUS_FUNCTION_HOLDING ||
           function_code == INVERTER_STATUS_FUNCTION_INPUT;
}

bool inverter_status_register_is_configured(const inverter_status_register_t *status_register)
{
    if (!status_register || !status_register->configured) return false;
    if (!inverter_status_function_is_read_only(status_register->function)) return false;
    if (status_register->words == 0U || status_register->words > INVERTER_STATUS_MAX_WORDS) return false;
    if (status_register->mapping_count == 0U ||
        status_register->mapping_count > INVERTER_STATUS_MAX_MAPPINGS) {
        return false;
    }
    return true;
}

bool inverter_status_decode_raw(const inverter_status_register_t *status_register,
                                const uint16_t *registers,
                                uint8_t register_count,
                                uint32_t *raw_value)
{
    if (!inverter_status_register_is_configured(status_register) || !registers || !raw_value) {
        return false;
    }
    if (register_count < status_register->words) return false;

    switch (status_register->type) {
        case INVERTER_VALUE_U16:
        case INVERTER_VALUE_S16:
            *raw_value = registers[0];
            return true;
        case INVERTER_VALUE_U32:
        case INVERTER_VALUE_S32: {
            if (status_register->words < 2U || register_count < 2U) return false;
            uint16_t high = status_register->word_order == INVERTER_WORD_ORDER_AB
                                ? registers[0]
                                : registers[1];
            uint16_t low = status_register->word_order == INVERTER_WORD_ORDER_AB
                               ? registers[1]
                               : registers[0];
            *raw_value = ((uint32_t)high << 16) | low;
            return true;
        }
        default:
            return false;
    }
}

inverter_state_t inverter_status_map_raw(const inverter_status_register_t *status_register,
                                         uint32_t raw_value)
{
    if (!inverter_status_register_is_configured(status_register)) return INVERTER_STATE_UNKNOWN;

    for (uint8_t index = 0; index < status_register->mapping_count; ++index) {
        const inverter_status_mapping_t *mapping = &status_register->mappings[index];
        uint32_t mask = mapping->raw_mask ? mapping->raw_mask : UINT32_MAX;
        if ((raw_value & mask) == (mapping->raw_value & mask)) return mapping->state;
    }
    return INVERTER_STATE_UNKNOWN;
}

inverter_state_t inverter_status_evaluate(const inverter_status_register_t *status_register,
                                          bool read_succeeded,
                                          uint32_t raw_value,
                                          uint32_t sample_age_ms,
                                          uint32_t stale_timeout_ms)
{
    if (!inverter_status_register_is_configured(status_register)) return INVERTER_STATE_UNKNOWN;
    if (!read_succeeded) return INVERTER_STATE_UNKNOWN;
    if (stale_timeout_ms == 0U || sample_age_ms > stale_timeout_ms) return INVERTER_STATE_UNKNOWN;
    return inverter_status_map_raw(status_register, raw_value);
}

const char *inverter_state_label(inverter_state_t state)
{
    switch (state) {
        case INVERTER_STATE_OFFLINE: return "offline";
        case INVERTER_STATE_STANDBY: return "standby";
        case INVERTER_STATE_CHECKING: return "checking";
        case INVERTER_STATE_ON_GRID: return "on_grid";
        case INVERTER_STATE_DERATED: return "derated";
        case INVERTER_STATE_FAULT: return "fault";
        case INVERTER_STATE_UNKNOWN:
        default: return "unknown";
    }
}

bool inverter_state_is_synchronised(inverter_state_t state)
{
    return state == INVERTER_STATE_ON_GRID;
}

bool inverter_status_fleet_synchronised(const inverter_status_sample_t *samples, uint8_t count)
{
    if (!samples || count == 0U) return false;

    bool any_enabled = false;
    for (uint8_t index = 0; index < count; ++index) {
        const inverter_status_sample_t *sample = &samples[index];
        if (!sample->enabled) continue;
        any_enabled = true;
        if (!sample->sample_fresh) return false;
        if (!inverter_state_is_synchronised(sample->state)) return false;
    }
    return any_enabled;
}
