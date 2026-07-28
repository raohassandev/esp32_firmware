#include "modbus_decoder.h"
#include <math.h>
#include <string.h>

size_t modbus_data_register_count(modbus_data_type_t type)
{
    return (type == MODBUS_DATA_UINT16 || type == MODBUS_DATA_INT16) ? 1u : 2u;
}

static uint32_t ordered_u32(const uint16_t *registers, modbus_word_order_t order)
{
    uint8_t src[4] = {
        (uint8_t)(registers[0] >> 8), (uint8_t)registers[0],
        (uint8_t)(registers[1] >> 8), (uint8_t)registers[1]
    };
    uint8_t out[4];
    static const uint8_t map[4][4] = {
        {0, 1, 2, 3}, {2, 3, 0, 1}, {1, 0, 3, 2}, {3, 2, 1, 0}
    };
    if ((unsigned)order > MODBUS_ORDER_DCBA) order = MODBUS_ORDER_ABCD;
    for (int i = 0; i < 4; ++i) out[i] = src[map[order][i]];
    return ((uint32_t)out[0] << 24) | ((uint32_t)out[1] << 16) | ((uint32_t)out[2] << 8) | out[3];
}

esp_err_t modbus_decode_scaled(const uint16_t *registers, size_t register_count,
                               modbus_data_type_t type, modbus_word_order_t order,
                               float scale, float offset, float *out_value)
{
    if (!registers || !out_value ||
        register_count < modbus_data_register_count(type) ||
        !isfinite(scale) || !isfinite(offset)) {
        return ESP_ERR_INVALID_ARG;
    }

    float value = 0.0f;
    if (type == MODBUS_DATA_UINT16) value = registers[0];
    else if (type == MODBUS_DATA_INT16) value = (int16_t)registers[0];
    else {
        uint32_t raw = ordered_u32(registers, order);
        if (type == MODBUS_DATA_UINT32) value = (float)raw;
        else if (type == MODBUS_DATA_INT32) value = (float)(int32_t)raw;
        else if (type == MODBUS_DATA_FLOAT32) memcpy(&value, &raw, sizeof(value));
        else return ESP_ERR_NOT_SUPPORTED;
    }

    if (!isfinite(value)) return ESP_ERR_INVALID_RESPONSE;
    float scaled = value * scale + offset;
    if (!isfinite(scaled)) return ESP_ERR_INVALID_RESPONSE;

    *out_value = scaled;
    return ESP_OK;
}

esp_err_t modbus_decode_u64_be_scaled(const uint16_t *registers,
                                      size_t register_count,
                                      double scale,
                                      double offset,
                                      double *out_value)
{
    if (!registers || !out_value || register_count < 4U ||
        !isfinite(scale) || !isfinite(offset)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t raw = ((uint64_t)registers[0] << 48) |
                   ((uint64_t)registers[1] << 32) |
                   ((uint64_t)registers[2] << 16) |
                   (uint64_t)registers[3];
    double scaled = (double)raw * scale + offset;
    if (!isfinite(scaled)) return ESP_ERR_INVALID_RESPONSE;
    *out_value = scaled;
    return ESP_OK;
}
