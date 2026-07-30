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
    /* A meter is attributed to a generator slot that is not in service. Distinct
     * from GENERATOR_RATING_UNKNOWN: the rating is not merely missing, the site
     * has declared an engine the generator policy does not describe at all, so
     * neither the aggregate rating nor the aggregate minimum-loading floor can be
     * computed for the configuration the plant can actually run. */
    COMMISSIONING_REASON_GENERATOR_SLOT_NOT_CONFIGURED,
    /* Two or more engine slots are in service and no kW load-sharing mode has been
     * commissioned. Which engine binds the aggregate minimum-loading floor depends
     * on the sharing law, and no law may be assumed: see
     * COMMISSIONING_SHARING_* below. */
    COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET,
    /* The commissioned sharing mode is one the firmware refuses to model. Droop is
     * refused: no defensible floor can be computed from values a commissioning
     * engineer can actually obtain. */
    COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSUPPORTED,
    /* Base-load sharing, and an in-service engine has no declared role, or a
     * base-loaded engine has no fixed kW setpoint. */
    COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN,
    /* Base-load sharing, and a base-loaded engine's setpoint is below that engine's
     * own minimum loading. No plant load fixes this; the plant has been set up to
     * under-load an engine. */
    COMMISSIONING_REASON_GENERATOR_BASE_LOAD_BELOW_MINIMUM,
    /* Base-load sharing, and no in-service engine is a swing engine. Nothing would
     * absorb the load the controller shapes. */
    COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE,
    /* Base-load sharing with at least one base-loaded engine, and no tolerance has
     * been commissioned for how far that engine's measured power may sit from its
     * setpoint before the controller stops believing the setpoint.
     *
     * WHY THIS BLOCKS COMMISSIONING RATHER THAN BEING REPORTED AS AN UNAVAILABLE
     * CHECK. The base-load minimum-loading floor adds the base-loaded setpoints as kW
     * the generators are absorbing. That is an assumption about a governor, and a
     * governor that has left kW control -- lost load-sharing line, switched to droop,
     * reverted to isochronous, put in manual -- makes it false in the PERMISSIVE
     * direction: the controller credits load the engines are not carrying and permits
     * more PV than the bus can lose. So the choice is between a plant commissioned on
     * an assumption nothing can check, and a gate that stays closed until one number
     * is supplied. The second is recoverable in an afternoon; the first is
     * over-generating into a genset, which is not. The gate closes. */
    COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET,
    COMMISSIONING_REASON_COUNT
} commissioning_reason_t;

/*
 * The kW load-sharing modes, mirroring generator_sharing_mode_t and
 * solar_grid_load_sharing_t value for value. Declared independently because this
 * module depends on nothing; the control engine _Static_asserts that all three
 * agree numerically.
 *
 * UNSET is zero, so a zeroed input struct commissions no sharing mode. That is the
 * deliberate default and the safe one: base-load sharing can place the floor either
 * above or below the isochronous floor depending on the commissioned setpoints, so
 * no mode is conservative for every plant and none may be assumed. Only "no mode",
 * which keeps this gate closed, is safe when the answer is unknown.
 */
typedef enum {
    COMMISSIONING_SHARING_UNSET = 0,
    COMMISSIONING_SHARING_ISOCHRONOUS,
    COMMISSIONING_SHARING_BASE_LOAD,
    COMMISSIONING_SHARING_DROOP,
    COMMISSIONING_SHARING_COUNT
} commissioning_sharing_mode_t;

/* One engine's part in a base-load plant. Mirrors generator_engine_role_t. */
typedef enum {
    COMMISSIONING_ENGINE_ROLE_UNSET = 0,
    COMMISSIONING_ENGINE_ROLE_SWING,
    COMMISSIONING_ENGINE_ROLE_BASE_LOAD,
    COMMISSIONING_ENGINE_ROLE_COUNT
} commissioning_engine_role_t;

/* Engine slots the gate can describe. Must equal APP_MAX_GENERATORS and
 * SOLAR_GRID_MAX_GENERATORS; declared independently because this module depends
 * on nothing, and checked by a _Static_assert in the control engine. */
#define COMMISSIONING_MAX_GENERATORS 3u

/* One engine slot's commissioning evidence. */
typedef struct {
    /* The generator policy declares this engine slot in service at this site. */
    bool enabled;
    /* An enabled meter carries this slot in its generator_index, so the plant has
     * declared the engine exists whether or not the policy describes it. */
    bool referenced_by_meter;
    float rated_kw;
    float minimum_loading_percent;
    /* commissioning_engine_role_t. Required only under base-load sharing. */
    uint8_t role;
    /* The fixed kW setpoint a base-loaded engine's governor holds. Zero means "not
     * commissioned". Required only for an engine whose role is BASE_LOAD. */
    float base_load_kw;
} commissioning_generator_slot_t;

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
    /* Enabled inverters commandable ONLY because an engineer declared their
     * endpoint a Modbus simulator. Counted separately from the qualified count
     * and never merged into it: this satisfies the gate for LAB commissioning
     * only, and can never produce a production-commissioned verdict. */
    uint8_t lab_only_inverter_count;
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

    /* Per-engine generator policy. A site can run one to
     * COMMISSIONING_MAX_GENERATORS gensets in parallel, and the aggregate
     * minimum-loading floor is only computable if EVERY engine the plant can run
     * is described. So every enabled slot needs both a rating and a
     * minimum-loading figure, and a slot a meter points at but the policy does not
     * describe is a hole, not a detail.
     *
     * A zeroed struct leaves every slot disabled, which yields "no generator slot
     * is commissioned" -- the same unmet verdict a zero rating always gave. */
    bool generator_limits_known;
    /* commissioning_sharing_mode_t. Zero is UNSET, which keeps the gate closed for
     * any plant that can run two or more engines. */
    uint8_t generator_load_sharing_mode;
    /* The commissioned base-load setpoint-agreement tolerance, absolute kW and percent
     * of the engine's own rating. Either may be stated, or both; ZERO IN BOTH MEANS NOT
     * COMMISSIONED, so a zeroed struct commissions no tolerance and a base-loaded plant
     * stays out of commissioning. Read ONLY when the sharing mode is BASE_LOAD and at
     * least one in-service engine is base-loaded -- an isochronous plant, a
     * single-engine plant and a base-load plant whose engines are all swing engines
     * have no setpoint to check and are entirely unaffected.
     *
     * No default is supplied anywhere in this firmware, because no manual, nameplate or
     * site document in this repository states one. */
    float generator_base_load_tolerance_kw;
    float generator_base_load_tolerance_percent_of_rating;
    commissioning_generator_slot_t generators[COMMISSIONING_MAX_GENERATORS];

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

/* What a satisfied gate actually authorises. NONE is zero so a zeroed status
 * authorises nothing. */
typedef enum {
    COMMISSIONING_SCOPE_NONE = 0,
    /* Commandable only against declared Modbus simulators. Valid for lab
     * validation; not evidence about physical equipment and never a production
     * release. */
    COMMISSIONING_SCOPE_LAB,
    /* Every commanded inverter passed production write qualification. */
    COMMISSIONING_SCOPE_PRODUCTION
} commissioning_scope_t;

typedef struct {
    /* True only when every prerequisite is satisfied. Says nothing about whether
     * the target is real equipment -- read `scope` for that. */
    bool commissioned;
    /* NONE unless commissioned. LAB whenever any commanded inverter is a
     * declared simulator, even if every other inverter is production-qualified:
     * the weakest link decides, because one simulated machine means the fleet's
     * behaviour has not been demonstrated on real equipment. */
    commissioning_scope_t scope;
    uint8_t satisfied_count;
    uint8_t unmet_count;
    /* Lowest-numbered unmet prerequisite; only meaningful when !commissioned. */
    uint8_t first_unmet; /* commissioning_prereq_t */
    commissioning_prereq_result_t results[COMMISSIONING_PREREQ_COUNT];
} commissioning_status_t;

/* Evaluates the gate. A NULL input yields the fully fail-closed result. */
commissioning_status_t commissioning_gate_evaluate(const commissioning_inputs_t *inputs);

/* Stable lowercase slug for a commissioning scope, for the API and logs. */
const char *commissioning_scope_label(commissioning_scope_t scope);

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
