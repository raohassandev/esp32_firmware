#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register data types. Appended to, never reordered: profiles use designated
 * initialisers, but INVERTER_VALUE_U16 must stay 0 so a zeroed profile describes
 * the narrowest, most conservative register rather than a 32-bit one.
 *
 * INVERTER_VALUE_FLOAT32 is IEEE-754 binary32 carried in two registers. It
 * exists because some manufacturers document their dispatch register as a float
 * -- SolarEdge's Dynamic Active Power Limit is one -- and until this type existed
 * the only way to represent such a device was to encode a percentage as an
 * INTEGER into a float register. That is not a cosmetic mistake: 50 written as
 * the integer 0x00000032 is the float 7e-44, i.e. effectively zero percent, and
 * the readback would decode the same bytes the same wrong way and agree with
 * itself. The command would be reported CONFIRMED while the inverter had been
 * told to stop. A false confirmation is the worst thing this module can produce,
 * so the type is modelled rather than approximated.
 */
typedef enum {
    INVERTER_VALUE_U16 = 0,
    INVERTER_VALUE_S16,
    INVERTER_VALUE_U32,
    INVERTER_VALUE_S32,
    INVERTER_VALUE_FLOAT32
} inverter_value_type_t;

/*
 * Word order of a 32-bit value spanning two registers. AB puts the most
 * significant word at the LOWER address (big-endian word order); BA puts the
 * least significant word at the lower address (little-endian word order, which
 * SolarEdge documents for its dynamic power control map).
 *
 * AB stays 0 so a zeroed description means the conventional Modbus order, which
 * is what the command path assumed unconditionally before this field reached it.
 */
typedef enum {
    INVERTER_WORD_ORDER_AB = 0,
    INVERTER_WORD_ORDER_BA
} inverter_word_order_t;

/* True for a type that occupies two registers. */
bool inverter_value_type_is_wide(inverter_value_type_t type);

/*
 * Decodes one register block into an engineering value.
 *
 * NON-FINITE INPUT IS REFUSED, not returned. A float register can legally hold
 * NaN or +/-Inf -- several manufacturers use NaN as their "not available"
 * marker -- and a NaN percentage compares unequal to everything, including
 * itself. Returning it would let a garbage sample flow into the confirmation
 * comparison and into the control loop. ESP_ERR_INVALID_RESPONSE is returned and
 * *value is left untouched, which matches modbus_decode_scaled() in the
 * modbus_tcp component and means the caller treats the sample as absent: the
 * write-confirmation evaluator then holds at PENDING until its deadline rather
 * than declaring a MISMATCH, i.e. "keep waiting", not "fault".
 */
esp_err_t inverter_profile_decode_value(const uint16_t *registers,
                                        uint8_t register_count,
                                        inverter_value_type_t type,
                                        inverter_word_order_t word_order,
                                        float scale,
                                        float *value);

/*
 * Encodes a raw register value into the words that go on the wire, honouring the
 * same word-order concept as the decoder above. The exact inverse of it, which is
 * the point: before this existed the read path modelled word order and the write
 * path hardcoded AB, and that asymmetry is a silent wrong-value bug on any device
 * documenting the other order.
 *
 * `raw_value` is the value in the DEVICE's own units -- already multiplied by the
 * profile scale by the caller, which is where the range and clamping policy
 * lives. For integer types it is rounded to nearest and range-checked against the
 * type; for INVERTER_VALUE_FLOAT32 it becomes the IEEE-754 binary32 bit pattern.
 *
 * A non-finite `raw_value`, or one that does not fit the type, is refused with
 * ESP_ERR_INVALID_ARG and nothing is written to `registers`. Refusing to encode
 * means no Modbus frame is issued at all, which is the correct outcome: a command
 * that cannot be represented must not be approximated.
 */
esp_err_t inverter_profile_encode_value(double raw_value,
                                        inverter_value_type_t type,
                                        inverter_word_order_t word_order,
                                        uint8_t register_count,
                                        uint16_t *registers);

bool inverter_profile_readback_matches(float requested_percent,
                                       float readback_percent,
                                       float tolerance_percent);

#ifdef __cplusplus
}
#endif
