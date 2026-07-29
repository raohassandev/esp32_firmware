#pragma once

/*
 * Commissioning gate (P0-6).
 *
 * Automatic control must be structurally incapable of engaging until an
 * enumerated set of commissioning prerequisites is satisfied. This module is
 * the single place that set is defined and evaluated.
 *
 * THREE PROPERTIES ARE DELIBERATE
 * -------------------------------
 *  1. FAIL CLOSED. Every prerequisite carries its own "known" input. If the
 *     firmware could not read the state a prerequisite depends on, that
 *     prerequisite is UNMET - never assumed satisfied. A zeroed input struct
 *     therefore evaluates to "not commissioned" with every prerequisite unmet,
 *     which is the correct answer for a controller that knows nothing.
 *  2. PURE. No ESP-IDF, no locks, no I/O, no allocation, no logging. It is a
 *     function of its inputs, so it can be compiled and executed by a host
 *     compiler and it is safe to call from any task.
 *  3. ENUMERATED AND EXPLAINABLE. Every prerequisite has a stable identifier
 *     for the API, a human title and a machine reason code saying exactly why
 *     it is unmet, so the interface never has to guess or paraphrase.
 *
 * WHAT THIS MODULE DOES NOT DO
 * ----------------------------
 * It does not decide that a plant is safe. It says only that the configuration
 * the controller needs in order to command anything is present, self-consistent
 * and qualified. Runtime evidence (meter freshness, grid-evidence gate, source
 * settling, active alarms) remains the control engine's separate cycle-by-cycle
 * responsibility, and both must hold before a command is issued.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The enumerated commissioning prerequisites. Values are exposed over the API
 * via commissioning_prereq_id(); append new ones, never renumber. */
typedef enum {
    COMMISSIONING_PREREQ_METER_ROLES = 0,
    COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED,
    COMMISSIONING_PREREQ_WRITE_READBACK,
    COMMISSIONING_PREREQ_FLEET_CAPACITY,
    COMMISSIONING_PREREQ_RAMP_POLICY,
    COMMISSIONING_PREREQ_SOURCE_DETECTION,
    COMMISSIONING_PREREQ_GRID_POLICY,
    COMMISSIONING_PREREQ_GENERATOR_LIMITS,
    COMMISSIONING_PREREQ_CONTROL_TUNING,
    COMMISSIONING_PREREQ_COUNT
} commissioning_prereq_t;

/* Why a prerequisite is unmet. Stable over the API; append, never renumber. */
typedef enum {
    COMMISSIONING_REASON_SATISFIED = 0,
    COMMISSIONING_REASON_STATE_UNREADABLE,
    COMMISSIONING_REASON_GRID_METER_MISSING,
    COMMISSIONING_REASON_GRID_METER_AMBIGUOUS,
    COMMISSIONING_REASON_GENERATOR_SLOT_DUPLICATE,
    COMMISSIONING_REASON_NO_ENABLED_INVERTER,
    COMMISSIONING_REASON_PROFILE_NOT_WRITE_QUALIFIED,
    COMMISSIONING_REASON_READBACK_UNAVAILABLE,
    COMMISSIONING_REASON_CAPACITY_NOT_COMMISSIONED,
    COMMISSIONING_REASON_RAMP_POLICY_INVALID,
    COMMISSIONING_REASON_SOURCE_EVIDENCE_UNCONFIGURED,
    COMMISSIONING_REASON_GRID_POLICY_INVALID,
    COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN,
    COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN,
    COMMISSIONING_REASON_CONTROL_TUNING_INVALID,
    COMMISSIONING_REASON_COUNT
} commissioning_reason_t;

/*
 * Collected commissioning evidence.
 *
 * Every group carries a `*_known` flag that the collector sets ONLY after it has
 * actually read that state successfully. Leaving a flag false is the honest way
 * to say "unreadable", and it keeps the gate closed.
 */
typedef struct {
    /* False whenever the collector could not obtain a coherent snapshot at all
     * (for example the persisted configuration could not be read). Forces every
     * prerequisite unmet regardless of the remaining fields. */
    bool state_readable;

    bool meter_roles_known;
    bool meter_roles_valid;
    uint8_t grid_meter_count;
    bool duplicate_generator_slot;

    bool inverter_fleet_known;
    uint8_t enabled_inverter_count;
    /* Enabled inverters whose assigned profile passes the production write gate
     * (production-approved AND carrying a manual-verified readback register). */
    uint8_t write_qualified_inverter_count;
    /* Enabled inverters whose assigned profile carries a readback register. */
    uint8_t readback_capable_inverter_count;
    /* Sum of the configured rated power of write-qualified enabled inverters.
     * Configuration, not runtime availability. */
    float commissioned_capacity_kw;

    bool ramp_policy_known;
    bool generator_ramp_enabled;
    float generator_ramp_up_percent_per_second;
    float generator_ramp_down_percent_per_second;

    bool source_detection_known;
    bool source_detection_configured;
    bool grid_evidence_configured;

    bool grid_policy_known;
    bool grid_policy_valid;

    bool generator_limits_known;
    float generator_rated_kw;
    float generator_minimum_loading_percent;

    bool control_tuning_known;
    float kp;
    float ki;
    float deadband_kw;
    uint32_t interval_ms;
    uint32_t meter_stale_timeout_ms;
} commissioning_inputs_t;

typedef struct {
    bool satisfied;
    uint8_t reason; /* commissioning_reason_t */
} commissioning_prereq_result_t;

typedef struct {
    /* True only when every prerequisite is satisfied. */
    bool commissioned;
    uint8_t satisfied_count;
    uint8_t unmet_count;
    /* Lowest-numbered unmet prerequisite; only meaningful when !commissioned. */
    uint8_t first_unmet; /* commissioning_prereq_t */
    commissioning_prereq_result_t results[COMMISSIONING_PREREQ_COUNT];
} commissioning_status_t;

/* Evaluates the gate. A NULL input yields the fully fail-closed result. */
commissioning_status_t commissioning_gate_evaluate(const commissioning_inputs_t *inputs);

/* Stable lowercase slug used as the API key for a prerequisite. */
const char *commissioning_prereq_id(uint8_t prereq);

/* Short human title for the interface. */
const char *commissioning_prereq_title(uint8_t prereq);

/* Stable lowercase slug for a reason code. */
const char *commissioning_reason_id(uint8_t reason);

/* One sentence, in the firmware's own words, explaining an unmet prerequisite.
 * Returns the empty string for COMMISSIONING_REASON_SATISFIED. */
const char *commissioning_reason_message(uint8_t reason);

/* The single sentence the control engine publishes as its inhibit reason when
 * the gate is closed. Never empty when !commissioned. */
const char *commissioning_gate_summary(const commissioning_status_t *status);

#ifdef __cplusplus
}
#endif
