#include "inverter_profile_decode.h"

#include <math.h>

esp_err_t inverter_profile_decode_value(const uint16_t *registers,
                                        uint8_t register_count,
                                        inverter_value_type_t type,
                                        inverter_word_order_t word_order,
                                        float scale,
                                        float *value)
{
    if (!registers || !value || !isfinite(scale)) return ESP_ERR_INVALID_ARG;

    switch (type) {
        case INVERTER_VALUE_U16:
            if (register_count < 1) return ESP_ERR_INVALID_SIZE;
            *value = (float)registers[0] * scale;
            return ESP_OK;
        case INVERTER_VALUE_S16:
            if (register_count < 1) return ESP_ERR_INVALID_SIZE;
            *value = (float)(int16_t)registers[0] * scale;
            return ESP_OK;
        case INVERTER_VALUE_U32:
        case INVERTER_VALUE_S32: {
            if (register_count < 2) return ESP_ERR_INVALID_SIZE;
            uint16_t high = word_order == INVERTER_WORD_ORDER_AB ? registers[0] : registers[1];
            uint16_t low = word_order == INVERTER_WORD_ORDER_AB ? registers[1] : registers[0];
            uint32_t raw = ((uint32_t)high << 16) | low;
            if (type == INVERTER_VALUE_S32) *value = (float)(int32_t)raw * scale;
            else *value = (float)raw * scale;
            return ESP_OK;
        }
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
}

bool inverter_profile_readback_matches(float requested_percent,
                                       float readback_percent,
                                       float tolerance_percent)
{
    if (!isfinite(requested_percent) || !isfinite(readback_percent) ||
        !isfinite(tolerance_percent) || tolerance_percent < 0.0f) return false;
    return fabsf(requested_percent - readback_percent) <= tolerance_percent;
}
