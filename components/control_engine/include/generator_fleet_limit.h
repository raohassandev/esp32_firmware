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
 * describe -- yields known == false and safe_pv_kw == 0, with a reason saying
 * which.
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
    GENERATOR_FLEET_REASON_COUNT
} generator_fleet_reason_t;

/* One engine slot: what was commissioned, and what the meters say right now. */
typedef struct {
    /* The generator policy declares this slot in service at this site. */
    bool configured;
    float rated_kw;
    float minimum_loading_percent;
    float reserve_kw;
    float reverse_power_margin_kw;

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
    /* The aggregate minimum-loading floor in kW, before reserve and margin. */
    float minimum_loading_kw;
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

#ifdef __cplusplus
}
#endif
