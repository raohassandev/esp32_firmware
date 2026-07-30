#include "inverter_profile_decode.h"

#include <math.h>
#include <string.h>

bool inverter_value_type_is_wide(inverter_value_type_t type)
{
    return type == INVERTER_VALUE_U32 || type == INVERTER_VALUE_S32 ||
           type == INVERTER_VALUE_FLOAT32;
}

/* Assembles two registers into a 32-bit word honouring the profile word order.
 * The single place that knowledge lives on the READ side. */
static uint32_t combine_words(const uint16_t *registers, inverter_word_order_t word_order)
{
    uint16_t high = word_order == INVERTER_WORD_ORDER_AB ? registers[0] : registers[1];
    uint16_t low = word_order == INVERTER_WORD_ORDER_AB ? registers[1] : registers[0];
    return ((uint32_t)high << 16) | low;
}

/* The exact inverse of combine_words(), and the single place that knowledge lives
 * on the WRITE side. Kept adjacent to it deliberately: these two must never drift
 * apart, because a mismatch would make a wrong value read back as the value that
 * was asked for -- a self-confirming error. */
static void split_words(uint32_t raw, inverter_word_order_t word_order, uint16_t *registers)
{
    uint16_t high = (uint16_t)(raw >> 16);
    uint16_t low = (uint16_t)raw;
    if (word_order == INVERTER_WORD_ORDER_AB) {
        registers[0] = high;
        registers[1] = low;
    } else {
        registers[0] = low;
        registers[1] = high;
    }
}

/* Reinterprets a 32-bit register pair as IEEE-754 binary32. memcpy, not a cast
 * through a pointer: type-punning through a float* is undefined behaviour and
 * -O2 is entitled to assume it never happens. */
static float bits_to_float(uint32_t raw)
{
    float value;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static uint32_t float_to_bits(float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return raw;
}

esp_err_t inverter_profile_decode_value(const uint16_t *registers,
                                        uint8_t register_count,
                                        inverter_value_type_t type,
                                        inverter_word_order_t word_order,
                                        float scale,
                                        float *value)
{
    if (!registers || !value || !isfinite(scale)) return ESP_ERR_INVALID_ARG;

    float decoded;
    switch (type) {
        case INVERTER_VALUE_U16:
            if (register_count < 1) return ESP_ERR_INVALID_SIZE;
            decoded = (float)registers[0];
            break;
        case INVERTER_VALUE_S16:
            if (register_count < 1) return ESP_ERR_INVALID_SIZE;
            decoded = (float)(int16_t)registers[0];
            break;
        case INVERTER_VALUE_U32:
        case INVERTER_VALUE_S32: {
            if (register_count < 2) return ESP_ERR_INVALID_SIZE;
            uint32_t raw = combine_words(registers, word_order);
            if (type == INVERTER_VALUE_S32) decoded = (float)(int32_t)raw;
            else decoded = (float)raw;
            break;
        }
        case INVERTER_VALUE_FLOAT32: {
            if (register_count < 2) return ESP_ERR_INVALID_SIZE;
            decoded = bits_to_float(combine_words(registers, word_order));
            break;
        }
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }

    /* NaN and +/-Inf are UNUSABLE SAMPLES, never values. Only a float register can
     * carry them out of the switch above, but the check is unconditional so that a
     * pathological scale cannot smuggle one past either.
     *
     * The caller treats an error as "no sample", so this is "keep waiting" rather
     * than "fault" -- the same choice modbus_decode_scaled() makes, and the same
     * choice inverter_write_confirmation_evaluate() makes when it refuses to count
     * a non-finite readback as a usable sample. A NaN readback must never be able
     * to declare a MISMATCH on a healthy machine, and must never be able to reach
     * the control loop as a number. */
    if (!isfinite(decoded)) return ESP_ERR_INVALID_RESPONSE;
    float scaled = decoded * scale;
    if (!isfinite(scaled)) return ESP_ERR_INVALID_RESPONSE;

    *value = scaled;
    return ESP_OK;
}

esp_err_t inverter_profile_encode_value(double raw_value,
                                        inverter_value_type_t type,
                                        inverter_word_order_t word_order,
                                        uint8_t register_count,
                                        uint16_t *registers)
{
    if (!registers || !isfinite(raw_value)) return ESP_ERR_INVALID_ARG;

    /* The width must match the type exactly, not merely be large enough. A
     * two-register write to a 16-bit register would overwrite the neighbouring
     * address, and in these manuals the neighbours are reactive-power limits and
     * mode selectors. */
    const uint8_t required = inverter_value_type_is_wide(type) ? 2U : 1U;
    if (register_count != required) return ESP_ERR_INVALID_SIZE;

    switch (type) {
        case INVERTER_VALUE_U16:
            if (raw_value < 0.0 || raw_value > (double)UINT16_MAX) return ESP_ERR_INVALID_ARG;
            registers[0] = (uint16_t)llround(raw_value);
            return ESP_OK;
        case INVERTER_VALUE_S16:
            if (raw_value < (double)INT16_MIN || raw_value > (double)INT16_MAX) {
                return ESP_ERR_INVALID_ARG;
            }
            registers[0] = (uint16_t)(int16_t)llround(raw_value);
            return ESP_OK;
        case INVERTER_VALUE_U32:
            if (raw_value < 0.0 || raw_value > (double)UINT32_MAX) return ESP_ERR_INVALID_ARG;
            split_words((uint32_t)llround(raw_value), word_order, registers);
            return ESP_OK;
        case INVERTER_VALUE_S32:
            if (raw_value < (double)INT32_MIN || raw_value > (double)INT32_MAX) {
                return ESP_ERR_INVALID_ARG;
            }
            split_words((uint32_t)(int32_t)llround(raw_value), word_order, registers);
            return ESP_OK;
        case INVERTER_VALUE_FLOAT32: {
            /* The double -> float narrowing is where a finite double can become an
             * infinity, so the result is re-checked rather than the input trusted.
             * Encoding an infinity would put the bit pattern 0x7F800000 into a
             * dispatch register, and this firmware would then read it back, refuse
             * it as non-finite, and never confirm -- but the inverter would already
             * have been handed it. Refusing here means nothing is sent at all. */
            float narrowed = (float)raw_value;
            if (!isfinite(narrowed)) return ESP_ERR_INVALID_ARG;
            split_words(float_to_bits(narrowed), word_order, registers);
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
