#include "meter_profiles.h"

#include <stddef.h>

#include "modbus_types.h"

/*
 * EM-500 — the only family in this catalogue with a transcribed map.
 *
 * Source: "EM-500 Three-Phase Energy Meter — MEASURES SUPPLIED BY SERIAL
 * COMMUNICATION PROTOCOL", Table 1, supplied by the product owner. The table
 * header states "To be used with functions 03 and 04", hence function code 3.
 *
 * Addresses are given in the document in hex with an H suffix and are raw
 * on-the-wire PDU addresses:
 *
 *   0014H = 20   L1 Active Power   2 words   W/100   Signed long
 *   0016H = 22   L2 Active Power   2 words   W/100   Signed long
 *   0018H = 24   L3 Active Power   2 words   W/100   Signed long
 *   003AH = 58   Eqv. Active Power 2 words   W/100   Signed long
 *
 * "Eqv. Active Power" is the total, and it is what the control loop regulates
 * against.
 *
 * SCALE. The manual's unit is W/100: the raw integer is hundredths of a watt.
 * This firmware works in kW, so
 *
 *   kW = raw / 100 / 1000 = raw * 0.00001
 *
 * That is derived from the document, and it independently reproduces the value
 * an engineer arrived at by measurement on the installed meters (0.00001 against
 * a 372.58 kW reading). Two routes to the same number is the reason to trust it.
 *
 * "Signed long" is a 32-bit signed integer, so INT32 — and signedness is not
 * cosmetic here: active power is negative when the site exports, and reading it
 * unsigned would turn a modest export into roughly 42.9 million kW of import.
 *
 * WORD ORDER. The document gives addresses and formats but does not state word
 * order. ABCD (high word first) is what the installed meters were verified
 * against on 2026-07-29, so it is recorded as MEASURED rather than as
 * documented. A clone that differs will read as a wildly wrong magnitude
 * immediately, which is a loud failure rather than a quiet one.
 *
 * SOURCE INPUT. 0x2100 is the digital-input word this firmware reads for source
 * detection, and it is treated as an OR of all inputs. That semantic is recorded
 * in components/source_detection/ with its own provenance, and it was verified on
 * the installed meters on 2026-07-29 by energising the tariff input and observing
 * 0 -> 1. It is repeated here so a profile carries the whole description of the
 * instrument, not most of it.
 *
 * NOT transcribed here: voltages, currents, power factor, energy counters, max
 * demand, hour counters, limits, wiring test. All are in the same document and
 * none is needed by the control loop. Adding one means transcribing it, not
 * guessing the pattern from the four above.
 */
static const meter_profile_t PROFILES[] = {
    {
        .model = METER_MODEL_EM500_LOVATO,
        .id = "em500",
        .manufacturer = "Automatrix",
        .model_family = "EM-500 three-phase energy meter",
        .has_register_map = true,
        .function_code = 3,
        .active_power_address = 0x003A,          /* 58, "Eqv. Active Power" */
        .active_power_type = MODBUS_DATA_INT32,  /* "Signed long" */
        .active_power_order = MODBUS_ORDER_ABCD, /* measured, not documented */
        .active_power_scale = 0.00001f,          /* W/100 -> kW */
        .has_phase_power = true,
        .phase_power_address = { 0x0014, 0x0016, 0x0018 },  /* 20, 22, 24 */
        .has_source_input = true,
        .source_input_address = 0x2100,
        .source_input_is_bitmask = true,
        .manual_reference =
            "EM-500 Three-Phase Energy Meter, serial communication protocol, Table 1 "
            "(functions 03 and 04). Total = 003AH \"Eqv. Active Power\", 2 words, "
            "W/100, Signed long; phases = 0014H / 0016H / 0018H. Word order ABCD "
            "measured on the installed meters 2026-07-29, not stated by the document.",
    },
    /*
     * PRESENT WITHOUT A MAP, DELIBERATELY.
     *
     * Leaving a family out of the catalogue reads as "this product does not
     * support it" and sends an engineer looking for a different controller.
     * Giving it invented addresses is worse. So it is listed, commissions as a
     * manual endpoint, and the interface says the registers are the engineer's
     * and not a manufacturer's.
     */
    {
        .model = METER_MODEL_GENERIC_MODBUS,
        .id = "generic",
        .manufacturer = "Other",
        .model_family = "Generic Modbus meter",
        .has_register_map = false,
        .manual_reference =
            "No manufacturer map transcribed. Every register is supplied by the "
            "commissioning engineer and this firmware makes no claim about them.",
    },
};

const meter_profile_t *meter_profiles_all(uint8_t *out_count)
{
    if (out_count) *out_count = (uint8_t)(sizeof(PROFILES) / sizeof(PROFILES[0]));
    return PROFILES;
}

const meter_profile_t *meter_profile_for_model(uint32_t model)
{
    /* UNDECLARED is refused here rather than falling through to a first entry.
     * "We do not know what is wired" must never resolve to the one family whose
     * register semantics this firmware claims to know. */
    if (model == METER_MODEL_UNDECLARED) return NULL;
    for (size_t i = 0; i < sizeof(PROFILES) / sizeof(PROFILES[0]); ++i) {
        if (PROFILES[i].model == model) return &PROFILES[i];
    }
    return NULL;
}
