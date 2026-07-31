#include "commissioning_gate.h"

#include <math.h>

/* Local finiteness helpers. The gate is compiled by the host toolchain for its
 * unit test, so it depends on nothing beyond <math.h>. */
static bool positive_finite(float value)
{
    return isfinite(value) && value > 0.0f;
}

static bool percent_in_range(float value)
{
    return isfinite(value) && value > 0.0f && value <= 100.0f;
}

static commissioning_prereq_result_t met(void)
{
    return (commissioning_prereq_result_t){
        .satisfied = true,
        .reason = COMMISSIONING_REASON_SATISFIED,
    };
}

static commissioning_prereq_result_t unmet(commissioning_reason_t reason)
{
    return (commissioning_prereq_result_t){
        .satisfied = false,
        .reason = (uint8_t)reason,
    };
}

/* Exactly one enabled meter must claim the grid role and no two meters may
 * claim the same generator slot. Anything else means the control loop cannot
 * know which physical instrument it regulates against. */
static commissioning_prereq_result_t evaluate_meter_roles(const commissioning_inputs_t *in)
{
    if (!in->meter_roles_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (in->duplicate_generator_slot) {
        return unmet(COMMISSIONING_REASON_GENERATOR_SLOT_DUPLICATE);
    }
    if (in->grid_meter_count == 0U) return unmet(COMMISSIONING_REASON_GRID_METER_MISSING);
    if (in->grid_meter_count > 1U) return unmet(COMMISSIONING_REASON_GRID_METER_AMBIGUOUS);
    if (!in->meter_roles_valid) return unmet(COMMISSIONING_REASON_GRID_METER_MISSING);
    return met();
}

/* Every enabled inverter must carry a profile that passes the production write
 * gate. One unqualified inverter in the fleet blocks commissioning outright: a
 * partially qualified fleet would let the controller command some machines
 * while silently ignoring others, which is not a commissioned plant. */
static commissioning_prereq_result_t evaluate_profiles(const commissioning_inputs_t *in)
{
    if (!in->inverter_fleet_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (in->enabled_inverter_count == 0U) {
        return unmet(COMMISSIONING_REASON_NO_ENABLED_INVERTER);
    }
    /* Every enabled inverter must be commandable, by production qualification or
     * by an explicit lab-simulator declaration. Summing the two counts is the
     * only place they are combined, and it is done here rather than upstream so
     * that the distinction survives into the reported result: the caller still
     * sees how many are merely lab targets and reports LAB commissioning
     * accordingly. Overflow is impossible -- both counts are bounded by the
     * enabled count, which is itself bounded by APP_MAX_INVERTERS. */
    const unsigned commandable =
        (unsigned)in->write_qualified_inverter_count + (unsigned)in->lab_only_inverter_count;
    if (commandable != (unsigned)in->enabled_inverter_count) {
        return unmet(COMMISSIONING_REASON_PROFILE_NOT_WRITE_QUALIFIED);
    }
    return met();
}

/* P0-9 is a commissioning prerequisite, not only a runtime behaviour: a fleet
 * the controller cannot read back is a fleet whose commands can never be
 * confirmed, so it must never be commissioned for automatic control. */
static commissioning_prereq_result_t evaluate_readback(const commissioning_inputs_t *in)
{
    if (!in->inverter_fleet_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (in->enabled_inverter_count == 0U) {
        return unmet(COMMISSIONING_REASON_NO_ENABLED_INVERTER);
    }
    if (in->readback_capable_inverter_count != in->enabled_inverter_count) {
        return unmet(COMMISSIONING_REASON_READBACK_UNAVAILABLE);
    }
    return met();
}

static commissioning_prereq_result_t evaluate_capacity(const commissioning_inputs_t *in)
{
    if (!in->inverter_fleet_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (!positive_finite(in->commissioned_capacity_kw)) {
        return unmet(COMMISSIONING_REASON_CAPACITY_NOT_COMMISSIONED);
    }
    return met();
}

/*
 * The generator ramp must be explicitly commissioned. A disabled generator ramp
 * is NOT accepted here: on a genset the rate of change is what protects the
 * machine, and "no rate limit" is a decision an engineer must take deliberately
 * rather than inherit from a default. The down rate must be at least the up
 * rate, because reducing PV is the direction that protects the generator.
 */
static commissioning_prereq_result_t evaluate_ramp(const commissioning_inputs_t *in)
{
    if (!in->ramp_policy_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (!in->generator_ramp_enabled) return unmet(COMMISSIONING_REASON_RAMP_POLICY_INVALID);
    if (!percent_in_range(in->generator_ramp_up_percent_per_second) ||
        !percent_in_range(in->generator_ramp_down_percent_per_second)) {
        return unmet(COMMISSIONING_REASON_RAMP_POLICY_INVALID);
    }
    if (in->generator_ramp_down_percent_per_second < in->generator_ramp_up_percent_per_second) {
        return unmet(COMMISSIONING_REASON_RAMP_POLICY_INVALID);
    }
    return met();
}

/* At least one way of establishing which source is carrying the plant must be
 * configured. Explicit Modbus grid-availability and breaker evidence is the
 * stronger of the two; measured source detection is accepted as the fallback. */
static commissioning_prereq_result_t evaluate_source(const commissioning_inputs_t *in)
{
    if (!in->source_detection_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (!in->grid_evidence_configured && !in->source_detection_configured) {
        return unmet(COMMISSIONING_REASON_SOURCE_EVIDENCE_UNCONFIGURED);
    }
    return met();
}

static commissioning_prereq_result_t evaluate_grid_policy(const commissioning_inputs_t *in)
{
    if (!in->grid_policy_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (!in->grid_policy_valid) return unmet(COMMISSIONING_REASON_GRID_POLICY_INVALID);
    return met();
}

/*
 * Every engine the plant can run must be described.
 *
 * A generator rating of zero means "not commissioned". There is no safe default
 * rating, no safe default minimum loading, and neither may ever be guessed: it
 * would let PV be commanded against a machine whose capacity is unknown.
 *
 * With engines in parallel this is not merely a per-machine requirement, it is an
 * arithmetic one. The minimum-loading floor is computed against the aggregate
 * rating of the engines actually online, so one undescribed engine makes the
 * denominator wrong for every configuration that includes it -- and wrong in the
 * permissive direction, allowing more PV than the plant can carry. The gate
 * therefore refuses a partially described fleet outright rather than
 * commissioning the subset it happens to know about.
 *
 * The order of the checks below preserves the reasons the single-generator
 * configuration always reported: an uncommissioned rating still comes back as
 * GENERATOR_RATING_UNKNOWN, not as some new code.
 */
static commissioning_prereq_result_t evaluate_generator_limits(const commissioning_inputs_t *in)
{
    if (!in->generator_limits_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);

    uint8_t enabled = 0U;
    for (uint8_t slot = 0U; slot < (uint8_t)COMMISSIONING_MAX_GENERATORS; ++slot) {
        if (in->generators[slot].enabled) enabled++;
    }
    /* No engine described at all. This is the uncommissioned unit, and it reports
     * exactly what it reported before per-engine limits existed. */
    if (enabled == 0U) return unmet(COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);

    /* A meter attributed to a slot the policy does not describe. The site has
     * declared an engine that carries no rating, so the aggregate floor for the
     * running configuration that includes it cannot be computed. */
    for (uint8_t slot = 0U; slot < (uint8_t)COMMISSIONING_MAX_GENERATORS; ++slot) {
        if (in->generators[slot].referenced_by_meter && !in->generators[slot].enabled) {
            return unmet(COMMISSIONING_REASON_GENERATOR_SLOT_NOT_CONFIGURED);
        }
    }

    for (uint8_t slot = 0U; slot < (uint8_t)COMMISSIONING_MAX_GENERATORS; ++slot) {
        if (!in->generators[slot].enabled) continue;
        if (!positive_finite(in->generators[slot].rated_kw)) {
            return unmet(COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);
        }
    }
    for (uint8_t slot = 0U; slot < (uint8_t)COMMISSIONING_MAX_GENERATORS; ++slot) {
        if (!in->generators[slot].enabled) continue;
        if (!percent_in_range(in->generators[slot].minimum_loading_percent)) {
            return unmet(COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN);
        }
    }

    /*
     * THE kW LOAD-SHARING MODE MUST BE COMMISSIONED EXPLICITLY.
     *
     * How engines in parallel divide the load decides which of them binds the
     * aggregate minimum-loading floor, so the floor is not computable without it.
     * It used to be assumed to be isochronous, silently. It is now a value an
     * engineer states, and an unstated one keeps this gate closed.
     *
     * WITH EXACTLY ONE ENGINE SLOT IN SERVICE AN UNSET MODE IS ACCEPTED, and that is
     * the only exemption. One engine cannot share load with anything: every sharing
     * law hands it the whole plant load and the floor is rated * percent / 100 under
     * all of them. Requiring the mode there would demand a quantity with no referent,
     * and it would close the gate on every already-commissioned single-generator
     * site the moment it was upgraded -- losing behaviour those sites are running on
     * today, for no safety gain. The requirement therefore lands exactly where the
     * arithmetic actually depends on it. Note the count used is the number of slots
     * IN SERVICE, not the number online: a plant that CAN run two engines needs the
     * mode commissioned even at a moment when it happens to be running one.
     *
     * The exemption covers UNSET ONLY. A mode that IS stated is checked whatever the
     * engine count, because the aggregate limit module refuses a droop or unrecognised
     * mode at runtime whatever the engine count -- and a gate that reported
     * "commissioned" while the control loop silently held PV at zero would send an
     * engineer looking in the wrong place. The two must agree, so they do.
     */
    const uint8_t mode = in->generator_load_sharing_mode;
    if (mode == (uint8_t)COMMISSIONING_SHARING_UNSET) {
        if (enabled == 1U) return met();
        return unmet(COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSET);
    }
    if (mode == (uint8_t)COMMISSIONING_SHARING_DROOP ||
        mode >= (uint8_t)COMMISSIONING_SHARING_COUNT) {
        /* Droop is refused rather than approximated: its floor needs the per-governor
         * no-load speed trim and the running bus frequency, and the first is not a
         * number a commissioning engineer can reliably obtain. An out-of-range value
         * is refused for the same reason -- it is evidence of nothing. */
        return unmet(COMMISSIONING_REASON_GENERATOR_SHARING_MODE_UNSUPPORTED);
    }

    if (mode == (uint8_t)COMMISSIONING_SHARING_BASE_LOAD) {
        /* Base-load sharing needs one more commissioned fact per engine: whether it
         * is held at a fixed kW, and if so at what kW. Neither may be defaulted --
         * mis-declaring a swing engine as base-loaded, or the reverse, moves the
         * floor in either direction depending on the setpoints, so one of the two
         * guesses is permissive on any given plant and nothing here can tell which. */
        uint8_t swing = 0U;
        uint8_t base_loaded = 0U;
        for (uint8_t slot = 0U; slot < (uint8_t)COMMISSIONING_MAX_GENERATORS; ++slot) {
            if (!in->generators[slot].enabled) continue;
            const uint8_t role = in->generators[slot].role;
            if (role == (uint8_t)COMMISSIONING_ENGINE_ROLE_SWING) {
                swing++;
                continue;
            }
            if (role != (uint8_t)COMMISSIONING_ENGINE_ROLE_BASE_LOAD) {
                return unmet(COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN);
            }
            const float setpoint = in->generators[slot].base_load_kw;
            if (!positive_finite(setpoint) || setpoint > in->generators[slot].rated_kw) {
                return unmet(COMMISSIONING_REASON_GENERATOR_BASE_LOAD_UNKNOWN);
            }
            /* An engine held below its own minimum loading is a commissioning fault,
             * not a floor to compute around: its load does not follow the plant
             * total, so no amount of PV curtailment ever brings it up to its
             * minimum. The plant has been configured to wet-stack that engine. */
            const float own_minimum_kw = in->generators[slot].rated_kw *
                                         in->generators[slot].minimum_loading_percent / 100.0f;
            if (setpoint < own_minimum_kw) {
                return unmet(COMMISSIONING_REASON_GENERATOR_BASE_LOAD_BELOW_MINIMUM);
            }
            base_loaded++;
        }
        if (swing == 0U) return unmet(COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE);

        /*
         * A BASE-LOADED ENGINE NEEDS A TOLERANCE BEFORE ITS SETPOINT MAY BE BELIEVED.
         *
         * The setpoint checks above are checks on the CONFIGURATION. The floor the
         * control engine computes from those setpoints also asserts something about the
         * PLANT: that each base-loaded governor is holding the kW it was given. The
         * measurement to check that already exists per engine. The tolerance to compare
         * against does not, and none is invented -- so it is commissioned, with no
         * default, and until it is supplied the gate stays closed. The reason code
         * carries the whole argument for closing rather than reporting the check
         * unavailable.
         *
         * REQUIRED ONLY WHEN AN ENGINE IS ACTUALLY BASE-LOADED. Base-load sharing over
         * an all-swing fleet has no setpoint to check and reduces exactly to
         * isochronous, so demanding a tolerance there would be demanding a quantity
         * with no referent -- the same distinction the single-engine sharing-mode
         * exemption makes. This is placed AFTER the swing check so that a plant with
         * nothing absorbing the swing still reports that, its more fundamental fault.
         *
         * EITHER FIGURE ALONE IS ENOUGH. The percentage is of the engine's own rating,
         * so it means something across engines of different sizes; how the two combine
         * when both are given is the control engine's rule, not this gate's, because
         * the gate never computes a band -- it only asks whether one was commissioned.
         * Bounds are checked so a corrupt or non-finite stored value cannot present
         * itself as a commissioned tolerance.
         */
        if (base_loaded > 0U) {
            const bool absolute_commissioned =
                positive_finite(in->generator_base_load_tolerance_kw);
            const bool percent_commissioned =
                percent_in_range(in->generator_base_load_tolerance_percent_of_rating);
            if (!absolute_commissioned && !percent_commissioned) {
                return unmet(COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET);
            }
        }
    }
    return met();
}

/*
 * EVERY ENABLED METER MUST BE AN INSTRUMENT THIS PHASE SUPPORTS.
 *
 * The product owner scoped this release phase to the EM500. Other meter models
 * are parked rather than deleted, and parked means REFUSED AT COMMISSIONING with
 * a reason that names them as deferred -- the same treatment unqualified
 * inverter profiles already get, and for the same reason. A meter family whose
 * register semantics nobody has established is not a meter this controller can
 * regulate a plant against, and half-supporting one is worse than refusing it,
 * because the failure is silent.
 *
 * NOTE THE ORDER. Undeclared is reported ahead of deferred: a site that has
 * stated nothing needs to be told to state something, and it would be actively
 * misleading to tell them their meter is deferred when they have not yet said
 * what it is. A plant with both problems sees the more fundamental one first.
 *
 * ZERO ENABLED METERS IS REFUSED. It is caught by the meter-roles prerequisite
 * as a missing grid meter as well, but this one must not report "satisfied" on a
 * plant with no instruments -- a vacuous truth is exactly the shape of answer
 * this gate exists to avoid.
 */
static commissioning_prereq_result_t evaluate_meter_models(const commissioning_inputs_t *in)
{
    if (!in->meter_models_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (in->enabled_meter_count == 0U) {
        return unmet(COMMISSIONING_REASON_GRID_METER_MISSING);
    }
    if (in->undeclared_meter_count > 0U) {
        return unmet(COMMISSIONING_REASON_METER_MODEL_UNDECLARED);
    }
    if (in->in_scope_meter_count != in->enabled_meter_count) {
        return unmet(COMMISSIONING_REASON_METER_MODEL_DEFERRED);
    }
    return met();
}

static commissioning_prereq_result_t evaluate_tuning(const commissioning_inputs_t *in)
{
    if (!in->control_tuning_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (!isfinite(in->kp) || in->kp <= 0.0f) {
        return unmet(COMMISSIONING_REASON_CONTROL_TUNING_INVALID);
    }
    if (!isfinite(in->ki) || in->ki < 0.0f) {
        return unmet(COMMISSIONING_REASON_CONTROL_TUNING_INVALID);
    }
    if (!isfinite(in->deadband_kw) || in->deadband_kw < 0.0f) {
        return unmet(COMMISSIONING_REASON_CONTROL_TUNING_INVALID);
    }
    /* Lower bound is 10 ms, not 100 ms. The scheduler tick is 1 ms, and the
     * product requirement is that control reacts as fast as measurements arrive
     * -- a real meter answers in under 40 ms, so a 100 ms floor threw away most
     * of that. It stays bounded because a control period below the scheduler
     * quantum would be a configuration error rather than a faster loop. */
    if (in->interval_ms < 10U || in->interval_ms > 10000U) {
        return unmet(COMMISSIONING_REASON_CONTROL_TUNING_INVALID);
    }
    if (in->meter_stale_timeout_ms < in->interval_ms) {
        return unmet(COMMISSIONING_REASON_CONTROL_TUNING_INVALID);
    }
    return met();
}

commissioning_status_t commissioning_gate_evaluate(const commissioning_inputs_t *inputs)
{
    commissioning_status_t status = {
        .commissioned = false,
        .satisfied_count = 0U,
        .unmet_count = (uint8_t)COMMISSIONING_PREREQ_COUNT,
        .first_unmet = (uint8_t)COMMISSIONING_PREREQ_METER_ROLES,
    };

    /* A missing input struct, or one whose collector could not read the
     * controller's own state, is not commissioned. Every prerequisite reports
     * the same honest reason rather than an invented specific fault. */
    if (!inputs || !inputs->state_readable) {
        for (uint8_t i = 0; i < (uint8_t)COMMISSIONING_PREREQ_COUNT; ++i) {
            status.results[i] = unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
        }
        return status;
    }

    status.results[COMMISSIONING_PREREQ_METER_ROLES] = evaluate_meter_roles(inputs);
    status.results[COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED] = evaluate_profiles(inputs);
    status.results[COMMISSIONING_PREREQ_WRITE_READBACK] = evaluate_readback(inputs);
    status.results[COMMISSIONING_PREREQ_FLEET_CAPACITY] = evaluate_capacity(inputs);
    status.results[COMMISSIONING_PREREQ_RAMP_POLICY] = evaluate_ramp(inputs);
    status.results[COMMISSIONING_PREREQ_SOURCE_DETECTION] = evaluate_source(inputs);
    status.results[COMMISSIONING_PREREQ_GRID_POLICY] = evaluate_grid_policy(inputs);
    status.results[COMMISSIONING_PREREQ_GENERATOR_LIMITS] = evaluate_generator_limits(inputs);
    status.results[COMMISSIONING_PREREQ_CONTROL_TUNING] = evaluate_tuning(inputs);
    status.results[COMMISSIONING_PREREQ_METER_MODEL_IN_SCOPE] = evaluate_meter_models(inputs);

    status.satisfied_count = 0U;
    status.unmet_count = 0U;
    bool first_recorded = false;
    for (uint8_t i = 0; i < (uint8_t)COMMISSIONING_PREREQ_COUNT; ++i) {
        if (status.results[i].satisfied) {
            status.satisfied_count++;
            continue;
        }
        status.unmet_count++;
        if (!first_recorded) {
            status.first_unmet = i;
            first_recorded = true;
        }
    }
    status.commissioned = status.unmet_count == 0U;
    /* Scope is decided by the weakest link, and only ever after the gate is
     * otherwise satisfied. A single declared simulator anywhere in the commanded
     * fleet makes the whole verdict LAB, because the plant's behaviour has then
     * not been demonstrated against real equipment. */
    if (!status.commissioned) {
        status.scope = COMMISSIONING_SCOPE_NONE;
    } else if (inputs->lab_only_inverter_count > 0U) {
        status.scope = COMMISSIONING_SCOPE_LAB;
    } else {
        status.scope = COMMISSIONING_SCOPE_PRODUCTION;
    }
    return status;
}

const char *commissioning_scope_label(commissioning_scope_t scope)
{
    switch (scope) {
        case COMMISSIONING_SCOPE_PRODUCTION: return "production";
        case COMMISSIONING_SCOPE_LAB: return "lab_simulator_only";
        case COMMISSIONING_SCOPE_NONE: default: return "none";
    }
}

static const char *const PREREQ_IDS[COMMISSIONING_PREREQ_COUNT] = {
    "meter_roles",
    "inverter_profile_qualified",
    "write_readback",
    "fleet_capacity",
    "ramp_policy",
    "source_detection",
    "grid_policy",
    "generator_limits",
    "control_tuning",
    "meter_model_in_scope",
};

static const char *const PREREQ_TITLES[COMMISSIONING_PREREQ_COUNT] = {
    "Meter roles assigned",
    "Inverter profile qualified for writing",
    "Setpoint readback available",
    "Fleet capacity commissioned",
    "Generator ramp policy commissioned",
    "Source detection configured",
    "Grid policy valid",
    "Generator limits commissioned",
    "Control tuning valid",
    "Meter model supported in this phase",
};

static const char *const REASON_IDS[COMMISSIONING_REASON_COUNT] = {
    "satisfied",
    "state_unreadable",
    "grid_meter_missing",
    "grid_meter_ambiguous",
    "generator_slot_duplicate",
    "no_enabled_inverter",
    "profile_not_write_qualified",
    "readback_unavailable",
    "capacity_not_commissioned",
    "ramp_policy_invalid",
    "source_evidence_unconfigured",
    "grid_policy_invalid",
    "generator_rating_unknown",
    "generator_loading_unknown",
    "control_tuning_invalid",
    "generator_slot_not_configured",
    "generator_sharing_mode_unset",
    "generator_sharing_mode_unsupported",
    "generator_base_load_unknown",
    "generator_base_load_below_minimum",
    "generator_no_swing_engine",
    "generator_base_load_tolerance_unset",
    "meter_model_undeclared",
    "meter_model_deferred",
};

static const char *const REASON_MESSAGES[COMMISSIONING_REASON_COUNT] = {
    "",
    "The controller could not read this part of its own state, so it is treated as not commissioned.",
    "No enabled meter is assigned the grid role.",
    "More than one enabled meter is assigned the grid role.",
    "Two enabled meters claim the same generator slot.",
    "No inverter is enabled.",
    "An enabled inverter carries a profile that is not qualified for production writes.",
    "An enabled inverter has no manual-verified setpoint readback register, so a command to it could never be confirmed.",
    "No commissioned inverter rated capacity is configured.",
    "The generator ramp policy is disabled or its rates are not commissioned.",
    "Neither explicit grid evidence nor measured source detection is configured.",
    "The persisted Solar-Grid policy is missing or invalid.",
    "The generator rated power is not commissioned.",
    "The generator minimum loading percentage is not commissioned.",
    "The control tuning constants or loop timing are not within a commissioned range.",
    "A meter is assigned to a generator slot that the generator policy does not describe, so the aggregate minimum-loading floor cannot be computed.",
    "The plant runs more than one generator and no kW load-sharing mode has been commissioned, so which engine sets the minimum-loading floor is not established.",
    "The commissioned kW load-sharing mode is not one the controller can compute a defensible minimum-loading floor for. Droop sharing is refused rather than approximated.",
    "Base-load sharing is commissioned but an in-service engine has no declared role, or a base-loaded engine has no fixed kW setpoint.",
    "A base-loaded engine's fixed kW setpoint is below that engine's own minimum loading, so the plant is configured to under-load it and no PV limit can correct that.",
    "Base-load sharing is commissioned but no in-service engine is a swing engine, so nothing on the bus would absorb the load the controller shapes.",
    "A base-loaded engine is commissioned but no tolerance says how far its measured power may sit from its fixed kW setpoint. The minimum-loading floor credits that engine with the load its setpoint promises, so if its governor leaves kW control the controller would permit more PV than the plant can carry. Commission the tolerance in kW, as a percentage of that engine's rating, or both.",
    "An enabled meter has no declared model. Which instrument is wired decides how its registers are read, and it is not inferred from the register mapping, so it must be stated before the meter can be commissioned.",
    "An enabled meter's declared model is deferred for this release phase. Only the EM500 (Lovato-derived) meter is supported; other models are parked rather than removed, and each one's unpark criterion is recorded in docs/RELEASE_READINESS.md section 4c.",
};

const char *commissioning_prereq_id(uint8_t prereq)
{
    return prereq < (uint8_t)COMMISSIONING_PREREQ_COUNT ? PREREQ_IDS[prereq] : "unknown";
}

const char *commissioning_prereq_title(uint8_t prereq)
{
    return prereq < (uint8_t)COMMISSIONING_PREREQ_COUNT ? PREREQ_TITLES[prereq] : "Unknown prerequisite";
}

const char *commissioning_reason_id(uint8_t reason)
{
    return reason < (uint8_t)COMMISSIONING_REASON_COUNT ? REASON_IDS[reason] : "unknown";
}

const char *commissioning_reason_message(uint8_t reason)
{
    return reason < (uint8_t)COMMISSIONING_REASON_COUNT
               ? REASON_MESSAGES[reason]
               : "The controller could not read this part of its own state, so it is treated as not commissioned.";
}

const char *commissioning_gate_summary(const commissioning_status_t *status)
{
    if (!status) {
        return "Commissioning state is unavailable; automatic control stays disabled.";
    }
    if (status->commissioned) return "";
    return commissioning_reason_message(status->results[status->first_unmet].reason);
}
