#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SOURCE_DETECTION_MODE_DISABLED = 0,
    SOURCE_DETECTION_MODE_SINGLE_INPUT = 1,
    SOURCE_DETECTION_MODE_DUAL_METER = 2
} source_detection_mode_t;

typedef enum {
    SOURCE_STATE_UNKNOWN = 0,
    SOURCE_STATE_GRID = 1,
    SOURCE_STATE_GENERATOR = 2,
    /* Both sources measured on the bus at once, on a plant that was commissioned
     * as able to run them in parallel.
     *
     * Distinguished from CONFLICT because the two demand opposite responses. On
     * a plant that cannot synchronise, two live sources is a dangerous
     * contradiction and PV must stop. On one that can, it is normal operation --
     * and reporting it as a conflict would inhibit control every time the site
     * ran the way it was built to.
     *
     * The controller still commands nothing in this state: holding the generator
     * above its floor AND respecting the grid export policy needs one PV
     * setpoint to satisfy two objectives, and that strategy does not exist yet.
     * The difference is that it is reported as an unimplemented mode rather than
     * as a fault, which sends an engineer somewhere useful. */
    SOURCE_STATE_SYNCHRONISED = 3
} source_state_t;

typedef enum {
    SOURCE_TARIFF_NONE = 0,
    SOURCE_TARIFF_1 = 1,
    SOURCE_TARIFF_2 = 2
} source_tariff_t;

typedef enum {
    SOURCE_REASON_NONE = 0,
    SOURCE_REASON_NOT_CONFIGURED,
    SOURCE_REASON_INVALID_CONFIG,
    SOURCE_REASON_EVIDENCE_UNAVAILABLE,
    SOURCE_REASON_EVIDENCE_STALE,
    SOURCE_REASON_NON_FINITE,
    SOURCE_REASON_UNKNOWN_INPUT_VALUE,
    SOURCE_REASON_CONFLICT,
    SOURCE_REASON_NO_SOURCE,
    SOURCE_REASON_DEBOUNCE_PENDING
} source_reason_t;

typedef struct {
    source_detection_mode_t mode;
    uint32_t debounce_ms;
    uint32_t stale_timeout_ms;
    uint16_t single_grid_value;
    uint16_t single_generator_value;
    /* THE METER FAMILY'S REGISTER SEMANTICS, CARRIED AS A POLICY INPUT.
     *
     * True only when the meter supplying single_raw_value has been COMMISSIONED
     * as an EM500/Lovato-derived instrument, on which register 0x2100 is the
     * documented "OR of all digital inputs" and is therefore a bitmask. It
     * licenses the "any non-zero word means generator" reading in
     * source_detection_observe(), and it licenses nothing else.
     *
     * FALSE IS THE DEFAULT AND MUST STAY THAT WAY. A zeroed policy -- which is
     * what a caller that has not been taught about meter models produces -- gets
     * strict equality against the commissioned values, which is the conservative
     * reading for a register nobody has interpreted. The alternative default
     * would apply one meter family's bitmask semantics to every instrument on
     * the market, turning any unexplained non-zero word into "generator" and,
     * through the tariff, into a control decision.
     *
     * It is a plain bool rather than a model enum on purpose: this module
     * depends on nothing and is compiled by the host toolchain for its unit
     * test, so it must not learn about config_types.h. The mapping from
     * commissioned model to this flag lives at the one call site that already
     * knows both. */
    bool single_bitmask_semantics;
    float grid_threshold_kw;
    float generator_threshold_kw;
    /* Can this plant run grid and generator in parallel?
     *
     * A commissioning question, not something to infer. Two loaded sources on a
     * plant with no synchroniser means somebody has changed the power network or
     * a changeover has failed -- and by the electrical grammar of the thing, if
     * they were not synchronised there would already have been a bang. The
     * controller cannot tell the two situations apart from power measurements
     * alone, so it is told.
     *
     * Defaults false, which is the fail-closed direction: an uncommissioned
     * plant treats two live sources as a fault and stops PV. */
    bool sync_capable;
} source_detection_policy_t;

typedef struct {
    bool single_has_sample;
    uint16_t single_raw_value;
    uint32_t single_age_ms;

    bool grid_has_sample;
    float grid_power_kw;
    uint32_t grid_age_ms;

    bool generator_has_sample;
    float generator_power_kw;
    uint32_t generator_age_ms;
} source_detection_evidence_t;

typedef struct {
    source_state_t candidate_state;
    source_reason_t reason;
    bool evidence_fresh;
    bool conflict;
} source_detection_observation_t;

typedef struct {
    source_state_t stable_state;
    source_state_t pending_state;
    uint32_t pending_since_ms;
    bool pending_active;
} source_detection_memory_t;

typedef struct {
    source_state_t state;
    source_state_t candidate_state;
    source_tariff_t tariff;
    source_reason_t reason;
    bool evidence_fresh;
    bool transition_pending;
    bool conflict;
    bool control_allowed;
    bool fail_closed;
} source_detection_result_t;

source_detection_observation_t source_detection_observe(
    const source_detection_policy_t *policy,
    const source_detection_evidence_t *evidence);

source_detection_result_t source_detection_step(
    source_detection_memory_t *memory,
    const source_detection_policy_t *policy,
    const source_detection_evidence_t *evidence,
    uint32_t now_ms);

void source_detection_reset(source_detection_memory_t *memory);
source_tariff_t source_detection_tariff(source_state_t state);
const char *source_detection_state_name(source_state_t state);
const char *source_detection_reason_name(source_reason_t reason);

#ifdef __cplusplus
}
#endif
