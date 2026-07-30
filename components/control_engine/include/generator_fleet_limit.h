#pragma once

/*
 * Aggregate generator limit for one to GENERATOR_FLEET_MAX_ENGINES gensets in
 * parallel.
 *
 * WHY THIS MODULE EXISTS
 * ----------------------
 * The commissioned rating describes machines; which of them is on the bus right
 * now is a RUNTIME FACT, not configuration. A single rating was therefore only
 * ever correct for a single running configuration: with two engines online and a
 * rating for one, the minimum-loading floor is computed against the wrong
 * denominator and the controller permits far more PV than the plant can carry --
 * the reverse-power condition this product exists to prevent.
 *
 * This module takes the per-engine commissioned limits plus the runtime evidence
 * that says which engines are online, and produces the aggregate floor and the
 * largest safe PV command. It is a pure function of its inputs: no ESP-IDF, no
 * locks, no I/O, no allocation, no logging, so it can be host-compiled and
 * executed by a unit test and is safe to call from the 20 ms control loop.
 *
 * FAIL CLOSED
 * -----------
 * There is exactly one conservative interpretation of an unknown running set, and
 * it is not "assume the largest machine" or "assume the smallest": it is to issue
 * no PV at all. Anything else picks a denominator the evidence does not support.
 * So every uncertainty -- an unreadable engine, a stale generator meter, an
 * enabled slot with no rating, a slot a meter points at that the policy does not
 * describe, an uncommissioned load-sharing mode -- yields known == false and
 * safe_pv_kw == 0, with a reason saying which.
 *
 * LOAD SHARING IS CONFIGURATION, NOT AN ASSUMPTION
 * ------------------------------------------------
 * How two or more engines in parallel divide the plant load decides which engine
 * binds the minimum-loading floor, so the floor cannot be computed without it.
 * Earlier revisions of this module assumed proportional (isochronous) sharing
 * implicitly. That assumption was unverifiable from the configuration and wrong for
 * a base-loaded or droop-shared plant, and wrong in the permissive direction is the
 * reverse-power condition. The mode is now an explicit commissioned value; see
 * generator_sharing_mode_t for what each mode does and which one is refused.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must equal SOURCE_MAX_GENERATORS, APP_MAX_GENERATORS and
 * SOLAR_GRID_MAX_GENERATORS; checked by _Static_asserts in the control engine. */
#define GENERATOR_FLEET_MAX_ENGINES 3u

typedef enum {
    GENERATOR_FLEET_OK = 0,
    /* No engine slot is in service, so the generator policy is not commissioned. */
    GENERATOR_FLEET_NO_ENGINE_CONFIGURED,
    /* An enabled slot carries no usable rating or minimum-loading figure. */
    GENERATOR_FLEET_RATING_UNKNOWN,
    /* Which engines are on the bus could not be established: a generator-role
     * meter is missing, stale, offline or degraded, or a meter is attributed to a
     * slot the policy does not describe. */
    GENERATOR_FLEET_RUNNING_SET_UNKNOWN,
    /* The running set IS known and contains no engine. Reached only when the
     * caller asks for a generator limit while no engine is online, which is a
     * contradiction the caller must not resolve by commanding PV. */
    GENERATOR_FLEET_NO_ENGINE_ONLINE,
    /* The plant load measurement backing the limit is missing or non-finite. */
    GENERATOR_FLEET_LOAD_UNKNOWN,
    /* Two or more engines are on the bus and no kW load-sharing mode has been
     * commissioned, so which engine binds the floor is not established. */
    GENERATOR_FLEET_SHARING_MODE_UNSET,
    /* The commissioned sharing mode is one this module refuses to model, or is not
     * a value this build recognises. Droop is refused; see
     * generator_sharing_mode_t. */
    GENERATOR_FLEET_SHARING_MODE_UNSUPPORTED,
    /* Base-load sharing, and an online engine carries no explicit role, or a
     * base-loaded engine carries no usable fixed kW setpoint. */
    GENERATOR_FLEET_BASE_LOAD_UNKNOWN,
    /* Base-load sharing, and a base-loaded engine's fixed setpoint is below that
     * engine's own minimum loading. No total plant load fixes this, because the
     * engine's load does not follow the total. It is a commissioning fault. */
    GENERATOR_FLEET_BASE_LOAD_BELOW_MINIMUM,
    /* Base-load sharing, and every online engine is held at a fixed kW setpoint.
     * Nothing on the bus absorbs the swing, so the plant load the controller would
     * be shaping has nowhere to go and no floor describes it. */
    GENERATOR_FLEET_NO_SWING_ENGINE,
    GENERATOR_FLEET_REASON_COUNT
} generator_fleet_reason_t;

/*
 * How the engines on the bus divide the plant's kW load.
 *
 * WHY UNSET IS THE DEFAULT, rather than the isochronous mode that was assumed
 * before. The choice is not between modes, it is between acting on a commissioned
 * fact and acting on a guess. There is no mode that is conservative for every
 * plant: base-load can produce a HIGHER floor than isochronous (a base-loaded
 * engine's setpoint is added on top of the swing engines' floor) and it can produce
 * a LOWER one (only the swing engines' ratings enter the proportional term), and
 * which way it goes depends on the setpoints. So "default to the mode with the
 * largest floor" is not available -- no mode dominates. What is available is to
 * default to no mode at all, which yields no PV command and keeps the
 * commissioning gate closed until an engineer states how the plant is configured.
 * Zero is therefore UNSET, so a zeroed input struct commissions nothing.
 *
 * The one exemption is stated where it is implemented: with exactly ONE engine on
 * the bus there is no load to share, every sharing law gives that engine the whole
 * plant load, and requiring a mode would be requiring a quantity that has no
 * meaning. That exemption is what keeps a single-generator site behaving exactly as
 * it did before this field existed.
 */
typedef enum {
    /* Not commissioned. Fails closed whenever more than one engine is on the bus. */
    GENERATOR_SHARING_UNSET = 0,
    /* Isochronous / proportional kW sharing: every engine sits at the same
     * percentage of its own rating. The binding constraint is the engine with the
     * highest minimum-loading percentage. */
    GENERATOR_SHARING_ISOCHRONOUS,
    /* Base load: engines with role GENERATOR_ENGINE_ROLE_BASE_LOAD are held at a
     * commissioned fixed kW setpoint; the engines with role
     * GENERATOR_ENGINE_ROLE_SWING absorb the remainder and share it with each other
     * isochronously. */
    GENERATOR_SHARING_BASE_LOAD,
    /*
     * Droop. REFUSED, deliberately and permanently until someone can supply a
     * defensible model.
     *
     * Under droop each engine's share follows its speed-droop characteristic:
     *     P_i = rated_i * (f_noload_i - f_bus) / (droop_i * f_nominal)
     * f_bus is a runtime variable that depends on the total load and on every other
     * engine, and f_noload_i is a per-governor speed-trim setting. The floor would
     * require both, and f_noload_i is not a number a commissioning engineer can
     * reliably obtain: it is trimmed in the field, it is often not displayed, it
     * drifts, and on most real plants a load-sharing line or a master controller
     * overrides the characteristic anyway, changing the law again.
     *
     * The honest conservative bound for an unknown sharing law is "any engine may
     * receive an arbitrarily small share", which makes the floor unbounded and the
     * permitted PV zero. That is identical in effect to refusing the mode. So
     * refusing is not a missing capability dressed up as safety -- it IS the
     * conservative answer, named. Fitting a plausible formula to values nobody can
     * supply would replace it with a wrong number that permits PV.
     */
    GENERATOR_SHARING_DROOP,
    GENERATOR_SHARING_MODE_COUNT
} generator_sharing_mode_t;

/* One engine's part in a base-load plant. Ignored by every other sharing mode:
 * under isochronous sharing every engine is a swing engine by definition. */
typedef enum {
    /* Not commissioned. Fails closed under base-load sharing. Guessing is not
     * available here: declaring a base-loaded engine "swing", or the reverse, moves
     * the floor in either direction depending on the setpoints, so a silent default
     * would sometimes be permissive. */
    GENERATOR_ENGINE_ROLE_UNSET = 0,
    /* Absorbs the load the base-loaded engines do not carry. */
    GENERATOR_ENGINE_ROLE_SWING,
    /* Held at base_load_kw by its own governor, independent of the total. */
    GENERATOR_ENGINE_ROLE_BASE_LOAD,
    GENERATOR_ENGINE_ROLE_COUNT
} generator_engine_role_t;

/* One engine slot: what was commissioned, and what the meters say right now. */
typedef struct {
    /* The generator policy declares this slot in service at this site. */
    bool configured;
    float rated_kw;
    float minimum_loading_percent;
    float reserve_kw;
    float reverse_power_margin_kw;

    /* generator_engine_role_t. Read only under GENERATOR_SHARING_BASE_LOAD. */
    uint8_t role;
    /* The fixed kW setpoint this engine's governor holds, when role is
     * GENERATOR_ENGINE_ROLE_BASE_LOAD. Must be positive, finite, no greater than
     * the rating, and at least the engine's own minimum loading -- a setpoint below
     * its own minimum is a commissioning fault, not a floor to compute around,
     * because the engine's load does not follow the plant total. Ignored for a
     * swing engine and under every other sharing mode. */
    float base_load_kw;

    /* A generator-role meter is attributed to this slot by its generator_index. */
    bool metered;
    /* That meter's latest sample is online, not degraded, finite and inside the
     * configured staleness window. Set from the SAME freshness rule the control
     * loop applies to the grid meter, so one definition of "fresh" governs both.
     *
     * A fresh sample counts the engine as ON THE BUS. This module deliberately
     * does not infer "stopped" from a small measured power: doing so would need an
     * invented threshold, and getting it wrong drops an engine out of the
     * denominator, shrinking the floor and allowing MORE PV. Counting a
     * fresh-metered engine as online can only enlarge the floor, which is the
     * direction that protects the machines. */
    bool sample_fresh;
    float measured_kw;
} generator_engine_input_t;

typedef struct {
    /* The plant load measurement is fresh. False fails the whole evaluation. */
    bool evidence_fresh;
    /* Load the generators are carrying, kW, non-negative. */
    float facility_load_kw;
    /* No generator-role meter is configured ANYWHERE, so per-engine attribution
     * does not exist on this site. With exactly one engine slot in service the
     * running set is then not ambiguous -- the one commissioned engine is the only
     * machine the policy describes -- and the limit is computed from it, which is
     * bit-for-bit the behaviour of the single-generator configuration that shipped
     * before per-engine limits. With two or more slots in service and no meters,
     * the running set is genuinely unknown and fails closed. */
    bool allow_unmetered_single_engine;
    /* generator_sharing_mode_t. Zero is GENERATOR_SHARING_UNSET, so a zeroed struct
     * commissions no sharing mode and yields no PV for a multi-engine bus. */
    uint8_t sharing_mode;
    uint8_t engine_count;
    generator_engine_input_t engines[GENERATOR_FLEET_MAX_ENGINES];
} generator_fleet_input_t;

typedef struct {
    /* True only when the running set was established and every online engine is
     * fully described. False means safe_pv_kw is zero and every aggregate is zero. */
    bool known;
    uint8_t online_count;
    /* Aggregate rating of the engines judged online: the denominator the
     * minimum-loading floor is computed against. */
    float online_rated_kw;
    /* The aggregate minimum-loading floor in kW, before reserve and margin: the
     * smallest TOTAL generator load at which every online engine independently
     * satisfies its own minimum loading under the commissioned sharing mode. */
    float minimum_loading_kw;
    /* The sharing mode the floor above was computed under, echoed back so the
     * caller and the API never have to infer it. GENERATOR_SHARING_UNSET whenever
     * known is false. */
    uint8_t sharing_mode;
    /* Online engines held at a fixed kW setpoint, and the sum of those setpoints.
     * Both zero under isochronous sharing. */
    uint8_t base_loaded_count;
    float base_load_total_kw;
    /* The floor plus the summed reserves and reverse-power margins: the load the
     * generators must keep. */
    float required_generator_kw;
    /* The largest safe aggregate PV command, kW, never negative. */
    float safe_pv_kw;
    uint8_t reason; /* generator_fleet_reason_t */
} generator_fleet_limit_t;

/* Evaluates the aggregate limit. A NULL input yields the fully fail-closed
 * result. */
generator_fleet_limit_t generator_fleet_limit_evaluate(const generator_fleet_input_t *input);

/* Stable lowercase slug for a reason code, for the API and logs. */
const char *generator_fleet_reason_id(uint8_t reason);

/* Stable lowercase slug for a sharing mode. An unrecognised value reads as
 * "unset", never as a supported mode. */
const char *generator_sharing_mode_id(uint8_t mode);

/* Stable lowercase slug for an engine role. An unrecognised value reads as
 * "unset". */
const char *generator_engine_role_id(uint8_t role);

/* True only for a sharing mode this build can compute a defensible floor for.
 * False for UNSET, for DROOP and for anything out of range. */
bool generator_sharing_mode_supported(uint8_t mode);

#ifdef __cplusplus
}
#endif
