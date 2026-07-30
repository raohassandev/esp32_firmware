/* Host-compiled unit test for the commissioning gate (P0-6).
 *
 * This executes the real evaluator; it does not grep source. The behaviour it
 * pins down is the safety property: unknown or unreadable state must never
 * evaluate to commissioned. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "commissioning_gate.h"

/* A fully commissioned plant. Every test below starts from this and breaks
 * exactly one thing, so a failure names the prerequisite that regressed. */
static commissioning_inputs_t good_inputs(void)
{
    commissioning_inputs_t in;
    memset(&in, 0, sizeof(in));
    in.state_readable = true;

    in.meter_roles_known = true;
    in.meter_roles_valid = true;
    in.grid_meter_count = 1;
    in.duplicate_generator_slot = false;

    in.inverter_fleet_known = true;
    in.enabled_inverter_count = 3;
    in.write_qualified_inverter_count = 3;
    in.readback_capable_inverter_count = 3;
    in.commissioned_capacity_kw = 300.0f;

    in.ramp_policy_known = true;
    in.generator_ramp_enabled = true;
    in.generator_ramp_up_percent_per_second = 5.0f;
    in.generator_ramp_down_percent_per_second = 20.0f;

    in.source_detection_known = true;
    in.source_detection_configured = false;
    in.grid_evidence_configured = true;

    in.grid_policy_known = true;
    in.grid_policy_valid = true;

    /* One engine slot in service, which is the single-generator plant every
     * commissioned unit shipped with. Slots 1 and 2 stay out of service. */
    in.generator_limits_known = true;
    in.generators[0].enabled = true;
    in.generators[0].rated_kw = 500.0f;
    in.generators[0].minimum_loading_percent = 30.0f;

    in.control_tuning_known = true;
    in.kp = 0.8f;
    in.ki = 0.1f;
    in.deadband_kw = 1.0f;
    in.interval_ms = 500;
    in.meter_stale_timeout_ms = 3000;
    return in;
}

static bool prereq_satisfied(const commissioning_status_t *status,
                             commissioning_prereq_t prereq)
{
    return status->results[prereq].satisfied;
}

static uint8_t prereq_reason(const commissioning_status_t *status,
                             commissioning_prereq_t prereq)
{
    return status->results[prereq].reason;
}

static void test_zeroed_input_is_never_commissioned(void)
{
    commissioning_inputs_t in;
    memset(&in, 0, sizeof(in));
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(status.unmet_count == COMMISSIONING_PREREQ_COUNT);
    assert(status.satisfied_count == 0);
    for (uint8_t i = 0; i < COMMISSIONING_PREREQ_COUNT; ++i) {
        assert(!status.results[i].satisfied);
        assert(status.results[i].reason == COMMISSIONING_REASON_STATE_UNREADABLE);
    }
    assert(commissioning_gate_summary(&status)[0] != '\0');
}

static void test_null_input_is_never_commissioned(void)
{
    commissioning_status_t status = commissioning_gate_evaluate(NULL);
    assert(!status.commissioned);
    assert(status.unmet_count == COMMISSIONING_PREREQ_COUNT);
    assert(commissioning_gate_summary(NULL)[0] != '\0');
}

static void test_fully_commissioned(void)
{
    commissioning_inputs_t in = good_inputs();
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.unmet_count == 0);
    assert(status.satisfied_count == COMMISSIONING_PREREQ_COUNT);
    assert(commissioning_gate_summary(&status)[0] == '\0');
}

/* Every single "known" flag, cleared on its own, must close the gate. This is
 * the fail-closed property stated as an exhaustive test rather than a comment. */
static void test_each_unknown_group_closes_the_gate(void)
{
    struct {
        size_t offset;
        commissioning_prereq_t prereq;
    } const groups[] = {
        {offsetof(commissioning_inputs_t, meter_roles_known), COMMISSIONING_PREREQ_METER_ROLES},
        {offsetof(commissioning_inputs_t, inverter_fleet_known), COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED},
        {offsetof(commissioning_inputs_t, ramp_policy_known), COMMISSIONING_PREREQ_RAMP_POLICY},
        {offsetof(commissioning_inputs_t, source_detection_known), COMMISSIONING_PREREQ_SOURCE_DETECTION},
        {offsetof(commissioning_inputs_t, grid_policy_known), COMMISSIONING_PREREQ_GRID_POLICY},
        {offsetof(commissioning_inputs_t, generator_limits_known), COMMISSIONING_PREREQ_GENERATOR_LIMITS},
        {offsetof(commissioning_inputs_t, control_tuning_known), COMMISSIONING_PREREQ_CONTROL_TUNING},
    };

    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
        commissioning_inputs_t in = good_inputs();
        *(bool *)((char *)&in + groups[i].offset) = false;
        commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(!prereq_satisfied(&status, groups[i].prereq));
        assert(prereq_reason(&status, groups[i].prereq) == COMMISSIONING_REASON_STATE_UNREADABLE);
    }
}

static void test_meter_roles(void)
{
    commissioning_inputs_t in = good_inputs();
    in.grid_meter_count = 0;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_METER_ROLES) ==
           COMMISSIONING_REASON_GRID_METER_MISSING);

    in = good_inputs();
    in.grid_meter_count = 2;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_METER_ROLES) ==
           COMMISSIONING_REASON_GRID_METER_AMBIGUOUS);

    in = good_inputs();
    in.duplicate_generator_slot = true;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_METER_ROLES) ==
           COMMISSIONING_REASON_GENERATOR_SLOT_DUPLICATE);

    /* Count looks right but the resolver itself said the assignment is not
     * usable. The resolver wins. */
    in = good_inputs();
    in.meter_roles_valid = false;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_METER_ROLES));
}

static void test_partially_qualified_fleet_is_rejected(void)
{
    commissioning_inputs_t in = good_inputs();
    in.write_qualified_inverter_count = 2; /* one of three unqualified */
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED) ==
           COMMISSIONING_REASON_PROFILE_NOT_WRITE_QUALIFIED);

    in = good_inputs();
    in.enabled_inverter_count = 0;
    in.write_qualified_inverter_count = 0;
    in.readback_capable_inverter_count = 0;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED) ==
           COMMISSIONING_REASON_NO_ENABLED_INVERTER);
}

/* P0-9 tied into P0-6: a fleet whose commands can never be confirmed is not a
 * commissioned fleet. */
static void test_missing_readback_blocks_commissioning(void)
{
    commissioning_inputs_t in = good_inputs();
    in.readback_capable_inverter_count = 2;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_WRITE_READBACK) ==
           COMMISSIONING_REASON_READBACK_UNAVAILABLE);
}

static void test_capacity(void)
{
    const float bad[] = {0.0f, -10.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        commissioning_inputs_t in = good_inputs();
        in.commissioned_capacity_kw = bad[i];
        commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_FLEET_CAPACITY) ==
               COMMISSIONING_REASON_CAPACITY_NOT_COMMISSIONED);
    }
}

static void test_ramp_policy(void)
{
    /* A disabled generator ramp is a deliberate engineering decision and is not
     * inherited from a default: the gate refuses it. */
    commissioning_inputs_t in = good_inputs();
    in.generator_ramp_enabled = false;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_RAMP_POLICY) ==
           COMMISSIONING_REASON_RAMP_POLICY_INVALID);

    /* Down slower than up leaves a genset exposed in the direction that matters. */
    in = good_inputs();
    in.generator_ramp_down_percent_per_second = 1.0f;
    in.generator_ramp_up_percent_per_second = 5.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));

    in = good_inputs();
    in.generator_ramp_up_percent_per_second = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));

    in = good_inputs();
    in.generator_ramp_down_percent_per_second = NAN;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));

    /* Equal rates are acceptable. */
    in = good_inputs();
    in.generator_ramp_up_percent_per_second = 10.0f;
    in.generator_ramp_down_percent_per_second = 10.0f;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_RAMP_POLICY));
}

static void test_source_detection_accepts_either_evidence_path(void)
{
    commissioning_inputs_t in = good_inputs();
    in.grid_evidence_configured = false;
    in.source_detection_configured = false;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_SOURCE_DETECTION) ==
           COMMISSIONING_REASON_SOURCE_EVIDENCE_UNCONFIGURED);

    in.source_detection_configured = true;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_SOURCE_DETECTION));
    assert(status.commissioned);

    in.source_detection_configured = false;
    in.grid_evidence_configured = true;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_SOURCE_DETECTION));
}

/* The single-generator behaviour, unchanged. A slot 0 with no rating is not in
 * service at all, and that reports GENERATOR_RATING_UNKNOWN exactly as it did
 * before per-engine limits existed. */
static void test_generator_limits(void)
{
    commissioning_inputs_t in = good_inputs();
    in.generators[0].enabled = false;
    in.generators[0].rated_kw = 0.0f;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);

    /* In service but unrated is the same missing number, reported the same way. */
    in = good_inputs();
    in.generators[0].rated_kw = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);

    in = good_inputs();
    in.generators[0].minimum_loading_percent = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN);

    in = good_inputs();
    in.generators[0].minimum_loading_percent = 140.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS));

    const float unusable[] = {NAN, INFINITY, -1.0f};
    for (size_t i = 0; i < sizeof(unusable) / sizeof(unusable[0]); ++i) {
        in = good_inputs();
        in.generators[0].rated_kw = unusable[i];
        status = commissioning_gate_evaluate(&in);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);

        in = good_inputs();
        in.generators[0].minimum_loading_percent = unusable[i];
        status = commissioning_gate_evaluate(&in);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN);
    }
}

/* Parallel engines. A site that can run up to three gensets must have EVERY
 * engine it can run described, because the minimum-loading floor is computed
 * against the aggregate rating of the engines online. A partially described fleet
 * would give a denominator that is wrong in the permissive direction. */
static void test_every_enabled_generator_slot_must_be_commissioned(void)
{
    /* Two and three fully described engines commission -- with the kW load-sharing
     * mode stated, which a multi-engine plant now has to state. */
    for (uint8_t engines = 2U; engines <= COMMISSIONING_MAX_GENERATORS; ++engines) {
        commissioning_inputs_t in = good_inputs();
        in.generator_load_sharing_mode = COMMISSIONING_SHARING_ISOCHRONOUS;
        for (uint8_t slot = 1U; slot < engines; ++slot) {
            in.generators[slot].enabled = true;
            in.generators[slot].rated_kw = 300.0f;
            in.generators[slot].minimum_loading_percent = 35.0f;
        }
        const commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(status.commissioned);
        assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS));
    }

    /* Any enabled slot missing its rating closes the gate, whichever slot it is. */
    for (uint8_t slot = 0U; slot < COMMISSIONING_MAX_GENERATORS; ++slot) {
        commissioning_inputs_t in = good_inputs();
        in.generator_load_sharing_mode = COMMISSIONING_SHARING_ISOCHRONOUS;
        for (uint8_t s = 0U; s < COMMISSIONING_MAX_GENERATORS; ++s) {
            in.generators[s].enabled = true;
            in.generators[s].rated_kw = 300.0f;
            in.generators[s].minimum_loading_percent = 35.0f;
        }
        in.generators[slot].rated_kw = 0.0f;
        commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);

        /* And the minimum-loading figure, independently. */
        in.generators[slot].rated_kw = 300.0f;
        in.generators[slot].minimum_loading_percent = 0.0f;
        status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN);
    }

    /* A slot that is out of service needs no numbers: an engine the site cannot
     * run is not a commissioning hole. */
    commissioning_inputs_t in = good_inputs();
    in.generators[2].enabled = false;
    in.generators[2].rated_kw = 0.0f;
    in.generators[2].minimum_loading_percent = 0.0f;
    const commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
}

/* A meter attributed to a generator slot the policy does not describe is a hole,
 * and it gets its own reason so an engineer is told which fact is missing rather
 * than being sent to look for a rating that was never asked for. */
static void test_metered_but_unconfigured_slot_fails_with_its_own_reason(void)
{
    for (uint8_t slot = 1U; slot < COMMISSIONING_MAX_GENERATORS; ++slot) {
        commissioning_inputs_t in = good_inputs();
        in.generators[slot].referenced_by_meter = true;
        const commissioning_status_t status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_SLOT_NOT_CONFIGURED);
        /* The operator-facing sentence must actually say something. */
        assert(commissioning_reason_message(COMMISSIONING_REASON_GENERATOR_SLOT_NOT_CONFIGURED)[0] != '\0');
    }

    /* A meter on a slot that IS described is not a fault. */
    commissioning_inputs_t in = good_inputs();
    in.generators[0].referenced_by_meter = true;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);

    in = good_inputs();
    in.generator_load_sharing_mode = COMMISSIONING_SHARING_ISOCHRONOUS;
    in.generators[1].enabled = true;
    in.generators[1].rated_kw = 300.0f;
    in.generators[1].minimum_loading_percent = 35.0f;
    in.generators[1].referenced_by_meter = true;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
}

/* ------------------------------------------------- kW load-sharing commissioning */

/* Two engines described, no sharing mode stated. The floor is not computable
 * without knowing which engine binds it, so the gate stays closed -- and with its
 * own reason, so an engineer is told what is missing rather than being sent to
 * re-check ratings that are already present. */
static void test_multi_engine_plant_must_commission_a_sharing_mode(void)
{
    commissioning_inputs_t in = good_inputs();
    in.generators[1].enabled = true;
    in.generators[1].rated_kw = 300.0f;
    in.generators[1].minimum_loading_percent = 35.0f;
    /* generator_load_sharing_mode deliberately left at zero: UNSET. */
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET);
    assert(commissioning_reason_message(COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET)[0] != '\0');

    /* Stating it opens the gate. */
    in.generator_load_sharing_mode = COMMISSIONING_SHARING_ISOCHRONOUS;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
}

/* A single-engine plant is exempt, because one engine shares load with nothing.
 * This is what keeps an already-commissioned single-generator unit working after
 * an upgrade that leaves the new field at zero. */
static void test_single_engine_plant_needs_no_sharing_mode(void)
{
    commissioning_inputs_t in = good_inputs();
    assert(in.generator_load_sharing_mode == COMMISSIONING_SHARING_UNSET);
    const commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS));
}

/* Droop is refused, and so is any value this build does not recognise. Both get the
 * "unsupported" reason rather than "unset": the engineer HAS stated something, and
 * telling them it is missing would send them to the wrong screen. */
static void test_droop_and_unknown_modes_are_refused(void)
{
    const uint8_t refused[] = {COMMISSIONING_SHARING_DROOP,
                               COMMISSIONING_SHARING_COUNT,
                               200U};
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        /* Refused whatever the engine count. The single-engine exemption covers an
         * UNSET mode only: the aggregate limit module refuses a stated droop mode at
         * runtime however many engines are online, and a gate that said
         * "commissioned" while the control loop held PV at zero would send an
         * engineer looking in the wrong place. */
        for (uint8_t engines = 1U; engines <= 2U; ++engines) {
            commissioning_inputs_t in = good_inputs();
            if (engines == 2U) {
                in.generators[1].enabled = true;
                in.generators[1].rated_kw = 300.0f;
                in.generators[1].minimum_loading_percent = 35.0f;
            }
            in.generator_load_sharing_mode = refused[i];
            const commissioning_status_t status = commissioning_gate_evaluate(&in);
            assert(!status.commissioned);
            assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
                   COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSUPPORTED);
        }
    }
}

/* Base-load sharing needs a role for every in-service engine, a setpoint for every
 * base-loaded one, that setpoint at or above the engine's own minimum, at least one
 * swing engine, and a tolerance saying how far a base-loaded engine's measured power may
 * sit from its setpoint. Each of those is a separate reason. */
static void test_base_load_requires_every_value_it_uses(void)
{
    /* A complete base-load plant commissions. Engine 0 base-loaded at 200 kW, which
     * is above its own minimum of 500 x 30 % = 150 kW; engine 1 swings. */
    commissioning_inputs_t complete = good_inputs();
    complete.generator_load_sharing_mode = COMMISSIONING_SHARING_BASE_LOAD;
    /* A TEST FIXTURE, NOT A PRODUCT DEFAULT. The firmware ships no tolerance because no
     * manual or nameplate in this repository states one; a value has to be stated here to
     * get past the requirement at all, and
     * test_base_load_requires_a_setpoint_agreement_tolerance() pins down that the
     * firmware supplies none of its own. */
    complete.generator_base_load_tolerance_kw = 5.0f;
    complete.generators[0].role = COMMISSIONING_ENGINE_ROLE_BASE_LOAD;
    complete.generators[0].base_load_kw = 200.0f;
    complete.generators[1].enabled = true;
    complete.generators[1].rated_kw = 300.0f;
    complete.generators[1].minimum_loading_percent = 35.0f;
    complete.generators[1].role = COMMISSIONING_ENGINE_ROLE_SWING;
    commissioning_status_t status = commissioning_gate_evaluate(&complete);
    assert(status.commissioned);

    /* No role on an in-service engine. */
    commissioning_inputs_t in = complete;
    in.generators[1].role = COMMISSIONING_ENGINE_ROLE_UNSET;
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN);

    /* An unrecognised role value is the same hole, never a silent "swing". */
    in = complete;
    in.generators[1].role = 200U;
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN);

    /* Base-loaded with no setpoint, or an impossible one, or one above the machine's
     * own rating. */
    const float unusable[] = {0.0f, -1.0f, NAN, INFINITY, 600.0f};
    for (size_t i = 0; i < sizeof(unusable) / sizeof(unusable[0]); ++i) {
        in = complete;
        in.generators[0].base_load_kw = unusable[i];
        status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN);
    }

    /* A setpoint below the engine's own minimum loading. 149 kW against a 150 kW
     * minimum: no plant load and no PV limit ever corrects it, because the engine's
     * load does not follow the total. It is a commissioning fault with its own
     * reason. */
    in = complete;
    in.generators[0].base_load_kw = 149.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_BELOW_MINIMUM);
    /* Exactly at its own minimum is acceptable. */
    in.generators[0].base_load_kw = 150.0f;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);

    /* Every in-service engine base-loaded: nothing absorbs the swing. */
    in = complete;
    in.generators[1].role = COMMISSIONING_ENGINE_ROLE_BASE_LOAD;
    in.generators[1].base_load_kw = 200.0f; /* above 300 x 35 % = 105 kW */
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE);

    /* Roles on OUT-OF-SERVICE slots are irrelevant: an engine the site cannot run
     * is not a commissioning hole, in this mode as in every other. */
    in = complete;
    in.generators[2].role = COMMISSIONING_ENGINE_ROLE_UNSET;
    in.generators[2].base_load_kw = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);

    /* Under ISOCHRONOUS sharing the roles and setpoints are not read at all: every
     * engine is a swing engine by definition, so leaving them unset is not a hole. */
    in = complete;
    in.generator_load_sharing_mode = COMMISSIONING_SHARING_ISOCHRONOUS;
    in.generators[0].role = COMMISSIONING_ENGINE_ROLE_UNSET;
    in.generators[0].base_load_kw = 0.0f;
    in.generators[1].role = COMMISSIONING_ENGINE_ROLE_UNSET;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);

    /* A SINGLE in-service engine declared base-loaded is refused, not exempted:
     * nothing else is on the bus to absorb the swing. This matches what the limit
     * module does at runtime. */
    in = good_inputs();
    in.generator_load_sharing_mode = COMMISSIONING_SHARING_BASE_LOAD;
    in.generators[0].role = COMMISSIONING_ENGINE_ROLE_BASE_LOAD;
    in.generators[0].base_load_kw = 200.0f;
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE);
}

/*
 * A BASE-LOADED ENGINE NEEDS A SETPOINT AGREEMENT TOLERANCE BEFORE IT CAN BE
 * COMMISSIONED.
 *
 * This is the decision the change turns on, so it is executed rather than described. The
 * aggregate minimum-loading floor adds a base-loaded engine's setpoint as kW the
 * generators are absorbing. That is an assertion about a governor, not about the
 * configuration, and a governor that has left kW control -- lost load-sharing line,
 * switched to droop, reverted to isochronous, put in manual -- makes it false in the
 * PERMISSIVE direction. The controller already measures each engine; what it cannot
 * supply is how much disagreement is normal, and this repository contains no document
 * stating one.
 *
 * Of the two available positions -- report the check unavailable and keep base-load
 * usable, or refuse to commission base-load without the tolerance -- the gate takes the
 * second. An unavailable check leaves a running plant on the unverifiable assumption; a
 * closed gate is one number away from resolution. Recoverable beats unrecoverable.
 */
static void test_base_load_requires_a_setpoint_agreement_tolerance(void)
{
    commissioning_inputs_t complete = good_inputs();
    complete.generator_load_sharing_mode = COMMISSIONING_SHARING_BASE_LOAD;
    complete.generators[0].role = COMMISSIONING_ENGINE_ROLE_BASE_LOAD;
    complete.generators[0].base_load_kw = 200.0f;
    complete.generators[1].enabled = true;
    complete.generators[1].rated_kw = 300.0f;
    complete.generators[1].minimum_loading_percent = 35.0f;
    complete.generators[1].role = COMMISSIONING_ENGINE_ROLE_SWING;

    /* Everything else about this plant is described, and it is still not commissioned:
     * both tolerance figures are zero, which is what an upgraded unit holds. */
    assert(complete.generator_base_load_tolerance_kw == 0.0f);
    assert(complete.generator_base_load_tolerance_percent_of_rating == 0.0f);
    commissioning_status_t status = commissioning_gate_evaluate(&complete);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET);

    /* EITHER figure alone commissions the check. */
    commissioning_inputs_t absolute = complete;
    absolute.generator_base_load_tolerance_kw = 5.0f;
    assert(commissioning_gate_evaluate(&absolute).commissioned);

    commissioning_inputs_t percent = complete;
    percent.generator_base_load_tolerance_percent_of_rating = 2.0f;
    assert(commissioning_gate_evaluate(&percent).commissioned);

    commissioning_inputs_t both = complete;
    both.generator_base_load_tolerance_kw = 5.0f;
    both.generator_base_load_tolerance_percent_of_rating = 2.0f;
    assert(commissioning_gate_evaluate(&both).commissioned);

    /* A stored value that is not a usable tolerance is not one. Each of these leaves the
     * gate exactly as closed as zero does, rather than being clamped into a band. */
    const float rubbish[] = {-1.0f, NAN, INFINITY};
    for (size_t i = 0; i < sizeof(rubbish) / sizeof(rubbish[0]); ++i) {
        commissioning_inputs_t in = complete;
        in.generator_base_load_tolerance_kw = rubbish[i];
        status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET);

        in = complete;
        in.generator_base_load_tolerance_percent_of_rating = rubbish[i];
        status = commissioning_gate_evaluate(&in);
        assert(!status.commissioned);
        assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
               COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET);
    }
    /* A percentage above the engine's whole rating is refused, not clamped to 100 %. */
    commissioning_inputs_t over = complete;
    over.generator_base_load_tolerance_percent_of_rating = 150.0f;
    status = commissioning_gate_evaluate(&over);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET);

    /* THE REQUIREMENT LANDS ONLY WHERE IT HAS A REFERENT, which is what keeps every
     * other plant untouched.
     *
     * Isochronous sharing: no engine is held at a fixed kW, so there is no setpoint to
     * check and no tolerance is asked for. */
    commissioning_inputs_t isochronous = complete;
    isochronous.generator_load_sharing_mode = COMMISSIONING_SHARING_ISOCHRONOUS;
    assert(commissioning_gate_evaluate(&isochronous).commissioned);

    /* Base-load sharing over an ALL-SWING fleet: the mode is stated but no engine is
     * base-loaded, so again there is no setpoint to check. This is the case that keeps
     * base-load-with-no-base-engine identical to isochronous. */
    commissioning_inputs_t all_swing = complete;
    all_swing.generators[0].role = COMMISSIONING_ENGINE_ROLE_SWING;
    all_swing.generators[0].base_load_kw = 0.0f;
    assert(commissioning_gate_evaluate(&all_swing).commissioned);

    /* A single-engine site is unaffected: it commissions with no sharing mode at all,
     * so it never reaches the base-load branch. */
    commissioning_inputs_t single = good_inputs();
    assert(single.generator_load_sharing_mode == COMMISSIONING_SHARING_UNSET);
    assert(single.generator_base_load_tolerance_kw == 0.0f);
    assert(commissioning_gate_evaluate(&single).commissioned);

    /* A MORE FUNDAMENTAL FAULT IS STILL REPORTED FIRST, so an engineer is never sent
     * after a tolerance while the plant has nothing absorbing the swing, or an engine
     * with no role, or a setpoint under its own minimum. */
    commissioning_inputs_t no_swing = complete;
    no_swing.generators[1].role = COMMISSIONING_ENGINE_ROLE_BASE_LOAD;
    no_swing.generators[1].base_load_kw = 200.0f;
    status = commissioning_gate_evaluate(&no_swing);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE);

    commissioning_inputs_t no_role = complete;
    no_role.generators[1].role = COMMISSIONING_ENGINE_ROLE_UNSET;
    status = commissioning_gate_evaluate(&no_role);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN);

    commissioning_inputs_t under_minimum = complete;
    under_minimum.generators[0].base_load_kw = 149.0f; /* its own minimum is 150 kW */
    status = commissioning_gate_evaluate(&under_minimum);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_BASE_LOAD_BELOW_MINIMUM);
}

/* No engine slot in service at all is the uncommissioned unit, and it must keep
 * reporting the reason it always reported rather than passing because "no enabled
 * slot is missing anything". */
static void test_no_generator_slot_in_service_is_never_commissioned(void)
{
    commissioning_inputs_t in = good_inputs();
    for (uint8_t slot = 0U; slot < COMMISSIONING_MAX_GENERATORS; ++slot) {
        in.generators[slot].enabled = false;
    }
    const commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_GENERATOR_LIMITS) ==
           COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);
}

static void test_control_tuning(void)
{
    commissioning_inputs_t in = good_inputs();
    in.kp = 0.0f;
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(prereq_reason(&status, COMMISSIONING_PREREQ_CONTROL_TUNING) ==
           COMMISSIONING_REASON_CONTROL_TUNING_INVALID);

    /* The control period bound is 10 ms, matching the 1 ms scheduler tick. A fast
     * loop is the product requirement -- a real meter answers in under 40 ms, so a
     * 100 ms floor discarded most of that responsiveness. Below the bound is a
     * configuration error, not a faster loop. */
    in = good_inputs();
    in.interval_ms = 9;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    in = good_inputs();
    in.interval_ms = 0; /* not a period at all */
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    /* 10 ms and a genuinely fast 20 ms must both be accepted. */
    in = good_inputs();
    in.interval_ms = 10;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    in = good_inputs();
    in.interval_ms = 20;
    status = commissioning_gate_evaluate(&in);
    assert(prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    /* Still bounded above. */
    in = good_inputs();
    in.interval_ms = 10001;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    /* A stale timeout shorter than the control period can never be met. */
    in = good_inputs();
    in.meter_stale_timeout_ms = 100;
    in.interval_ms = 500;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));

    in = good_inputs();
    in.ki = NAN;
    status = commissioning_gate_evaluate(&in);
    assert(!prereq_satisfied(&status, COMMISSIONING_PREREQ_CONTROL_TUNING));
}

/* first_unmet must be the lowest-numbered failure so the interface always
 * points the engineer at one actionable item rather than a list. */
static void test_first_unmet_is_lowest_index(void)
{
    commissioning_inputs_t in = good_inputs();
    in.grid_meter_count = 0;              /* prerequisite 0 */
    in.generators[0].rated_kw = 0.0f;     /* prerequisite 7 */
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.first_unmet == COMMISSIONING_PREREQ_METER_ROLES);
    assert(status.unmet_count == 2);

    in = good_inputs();
    in.generators[0].rated_kw = 0.0f;
    status = commissioning_gate_evaluate(&in);
    assert(status.first_unmet == COMMISSIONING_PREREQ_GENERATOR_LIMITS);
    assert(status.unmet_count == 1);
    assert(status.satisfied_count == COMMISSIONING_PREREQ_COUNT - 1);
}

/* Every enumerated value must carry API text; a silent "unknown" in the UI is a
 * defect, and out-of-range values must not read out of bounds. */
static void test_labels_are_complete_and_bounded(void)
{
    for (uint8_t i = 0; i < COMMISSIONING_PREREQ_COUNT; ++i) {
        assert(commissioning_prereq_id(i)[0] != '\0');
        assert(strcmp(commissioning_prereq_id(i), "unknown") != 0);
        assert(commissioning_prereq_title(i)[0] != '\0');
    }
    assert(strcmp(commissioning_prereq_id(COMMISSIONING_PREREQ_COUNT), "unknown") == 0);
    assert(strcmp(commissioning_prereq_id(200), "unknown") == 0);

    for (uint8_t i = 1; i < COMMISSIONING_REASON_COUNT; ++i) {
        assert(commissioning_reason_id(i)[0] != '\0');
        assert(commissioning_reason_message(i)[0] != '\0');
    }
    assert(commissioning_reason_message(COMMISSIONING_REASON_SATISFIED)[0] == '\0');
    assert(strcmp(commissioning_reason_id(COMMISSIONING_REASON_COUNT), "unknown") == 0);
    assert(commissioning_reason_message(250)[0] != '\0');

    /* Prerequisite slugs must be unique: they are the API keys. */
    for (uint8_t a = 0; a < COMMISSIONING_PREREQ_COUNT; ++a) {
        for (uint8_t b = (uint8_t)(a + 1); b < COMMISSIONING_PREREQ_COUNT; ++b) {
            assert(strcmp(commissioning_prereq_id(a), commissioning_prereq_id(b)) != 0);
        }
    }
}

/* Lab-target declarations satisfy the profile prerequisite, but must never be
 * reported as production commissioning. These execute the real evaluator. */
static void test_lab_scope(void)
{
    /* A fully production-qualified fleet is PRODUCTION scope. */
    commissioning_inputs_t in = good_inputs();
    commissioning_status_t status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_PRODUCTION);

    /* An entirely simulated fleet commissions, but only for the lab. */
    in = good_inputs();
    in.write_qualified_inverter_count = 0;
    in.lab_only_inverter_count = in.enabled_inverter_count;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_LAB);

    /* One simulator among production-qualified machines drags the whole verdict
     * down to LAB. The weakest link decides. */
    in = good_inputs();
    in.write_qualified_inverter_count = (uint8_t)(in.enabled_inverter_count - 1);
    in.lab_only_inverter_count = 1;
    status = commissioning_gate_evaluate(&in);
    assert(status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_LAB);

    /* Counts that do not cover every enabled inverter still fail, whichever
     * kind is short. */
    in = good_inputs();
    in.write_qualified_inverter_count = 1;
    in.lab_only_inverter_count = 1; /* 2 of 3 */
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);
    assert(status.scope == COMMISSIONING_SCOPE_NONE);

    /* A lab declaration cannot substitute for a readback register: an
     * unconfirmable command stays blocked even in the lab. */
    in = good_inputs();
    in.write_qualified_inverter_count = 0;
    in.lab_only_inverter_count = in.enabled_inverter_count;
    in.readback_capable_inverter_count = 0;
    status = commissioning_gate_evaluate(&in);
    assert(!status.commissioned);

    /* An uncommissioned gate never authorises anything, and a zeroed status
     * authorises nothing. */
    commissioning_status_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    assert(zeroed.scope == COMMISSIONING_SCOPE_NONE);
    assert(commissioning_gate_evaluate(NULL).scope == COMMISSIONING_SCOPE_NONE);

    assert(strcmp(commissioning_scope_label(COMMISSIONING_SCOPE_NONE), "none") == 0);
    assert(strcmp(commissioning_scope_label(COMMISSIONING_SCOPE_LAB), "lab_simulator_only") == 0);
    assert(strcmp(commissioning_scope_label(COMMISSIONING_SCOPE_PRODUCTION), "production") == 0);
    /* Out-of-range must not report production. */
    assert(strcmp(commissioning_scope_label((commissioning_scope_t)99), "none") == 0);
}

int main(void)
{
    test_zeroed_input_is_never_commissioned();
    test_null_input_is_never_commissioned();
    test_fully_commissioned();
    test_each_unknown_group_closes_the_gate();
    test_meter_roles();
    test_partially_qualified_fleet_is_rejected();
    test_lab_scope();
    test_missing_readback_blocks_commissioning();
    test_capacity();
    test_ramp_policy();
    test_source_detection_accepts_either_evidence_path();
    test_generator_limits();
    test_every_enabled_generator_slot_must_be_commissioned();
    test_metered_but_unconfigured_slot_fails_with_its_own_reason();
    test_multi_engine_plant_must_commission_a_sharing_mode();
    test_single_engine_plant_needs_no_sharing_mode();
    test_droop_and_unknown_modes_are_refused();
    test_base_load_requires_every_value_it_uses();
    test_base_load_requires_a_setpoint_agreement_tolerance();
    test_no_generator_slot_in_service_is_never_commissioned();
    test_control_tuning();
    test_first_unmet_is_lowest_index();
    test_labels_are_complete_and_bounded();
    printf("commissioning gate unit tests passed\n");
    return 0;
}
