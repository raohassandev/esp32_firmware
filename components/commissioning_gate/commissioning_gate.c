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

/* A generator rating of zero means "not commissioned". There is no safe default
 * rating and one must never be guessed: it would let PV be commanded against a
 * machine whose capacity is unknown. */
static commissioning_prereq_result_t evaluate_generator_limits(const commissioning_inputs_t *in)
{
    if (!in->generator_limits_known) return unmet(COMMISSIONING_REASON_STATE_UNREADABLE);
    if (!positive_finite(in->generator_rated_kw)) {
        return unmet(COMMISSIONING_REASON_GENERATOR_RATING_UNKNOWN);
    }
    if (!percent_in_range(in->generator_minimum_loading_percent)) {
        return unmet(COMMISSIONING_REASON_GENERATOR_LOADING_UNKNOWN);
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
    if (in->interval_ms < 100U || in->interval_ms > 10000U) {
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
