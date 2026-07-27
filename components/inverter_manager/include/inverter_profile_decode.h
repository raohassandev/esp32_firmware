#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INVERTER_VALUE_U16 = 0,
    INVERTER_VALUE_S16,
    INVERTER_VALUE_U32,
    INVERTER_VALUE_S32
} inverter_value_type_t;

typedef enum {
    INVERTER_WORD_ORDER_AB = 0,
    INVERTER_WORD_ORDER_BA
} inverter_word_order_t;

esp_err_t inverter_profile_decode_value(const uint16_t *registers,
                                        uint8_t register_count,
                                        inverter_value_type_t type,
                                        inverter_word_order_t word_order,
                                        float scale,
                                        float *value);

bool inverter_profile_readback_matches(float requested_percent,
                                       float readback_percent,
                                       float tolerance_percent);

#ifdef __cplusplus
}
#endif
