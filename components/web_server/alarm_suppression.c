#include "alarm_suppression.h"

/* See include/alarm_suppression.h. Pure functions only: this file is called from
 * inside the alarm module's critical section, where interrupts are disabled. */

alarm_suppression_t alarm_suppression_effective(alarm_suppression_flags_t flags)
{
    /* Strongest first. "Strongest" means hardest to undo, not most severe: an
     * out-of-service alarm needs a maintenance action, design suppression needs
     * the plant to recover, and a shelf needs only the clock. Reporting the
     * weakest of several concurrent states would understate how long the alarm
     * will stay quiet, which is the one thing a reader of this field needs. */
    if (flags.out_of_service) return ALARM_SUPPRESSION_OUT_OF_SERVICE;
    if (flags.by_design) return ALARM_SUPPRESSION_BY_DESIGN;
    if (flags.shelved) return ALARM_SUPPRESSION_SHELVED;
    return ALARM_SUPPRESSION_NONE;
}

uint8_t alarm_suppression_active_count(alarm_suppression_flags_t flags)
{
    return (uint8_t)((flags.shelved ? 1U : 0U) + (flags.by_design ? 1U : 0U) +
                     (flags.out_of_service ? 1U : 0U));
}

bool alarm_suppression_any(alarm_suppression_flags_t flags)
{
    return alarm_suppression_active_count(flags) > 0U;
}

const char *alarm_suppression_name(alarm_suppression_t state)
{
    switch (state) {
    case ALARM_SUPPRESSION_SHELVED:        return "shelved";
    case ALARM_SUPPRESSION_BY_DESIGN:      return "suppressed_by_design";
    case ALARM_SUPPRESSION_OUT_OF_SERVICE: return "out_of_service";
    case ALARM_SUPPRESSION_NONE:
    default:                               return "none";
    }
}

const char *alarm_suppression_authority(alarm_suppression_t state)
{
    switch (state) {
    /* The operator's own decision, time-limited and expiring. */
    case ALARM_SUPPRESSION_SHELVED:        return "operator";
    /* The controller's decision, driven by plant state; nobody chose it for this
     * alarm individually and nobody can lift it while the cause stands. */
    case ALARM_SUPPRESSION_BY_DESIGN:      return "system";
    /* A maintenance action under authorisation, with a recorded reason. */
    case ALARM_SUPPRESSION_OUT_OF_SERVICE: return "maintenance";
    case ALARM_SUPPRESSION_NONE:
    default:                               return "none";
    }
}

bool alarm_suppression_expires(alarm_suppression_t state)
{
    /* Only a shelf. Design suppression ends when the plant state that caused it
     * ends, which is a release rather than an expiry, and out of service ends
     * only when somebody returns the alarm to service - which is precisely why
     * it is a different state from shelving and not a longer shelf. */
    return state == ALARM_SUPPRESSION_SHELVED;
}

bool alarm_suppression_hidden_from_triage(alarm_suppression_t state)
{
    return state != ALARM_SUPPRESSION_NONE;
}

alarm_design_step_t alarm_design_suppression_step(bool suppressed_now, bool cause_present)
{
    /* Edge-triggered, so the caller journals a transition once rather than on
     * every observation tick: a journal full of "still suppressed" records is a
     * journal nobody can read an incident out of. */
    if (cause_present && !suppressed_now) return ALARM_DESIGN_STEP_ENGAGE;
    if (!cause_present && suppressed_now) return ALARM_DESIGN_STEP_RELEASE;
    return ALARM_DESIGN_STEP_NONE;
}

bool alarm_out_of_service_reason_valid(uint32_t reason)
{
    return reason <= (uint32_t)ALARM_OUT_OF_SERVICE_REASON_MAX;
}

const char *alarm_out_of_service_reason_name(uint8_t reason)
{
    switch (reason) {
    case ALARM_OUT_OF_SERVICE_REASON_MAINTENANCE:    return "field_device_maintenance";
    case ALARM_OUT_OF_SERVICE_REASON_REPLACEMENT:    return "field_device_replacement";
    case ALARM_OUT_OF_SERVICE_REASON_COMMISSIONING:  return "site_commissioning_work";
    case ALARM_OUT_OF_SERVICE_REASON_AWAITING_REPAIR:return "awaiting_repair";
    case ALARM_OUT_OF_SERVICE_REASON_PLANT_CHANGE:   return "plant_change_pending_rationalisation";
    default:                                         return "unknown";
    }
}

const char *alarm_out_of_service_reason_text(uint8_t reason)
{
    switch (reason) {
    case ALARM_OUT_OF_SERVICE_REASON_MAINTENANCE:
        return "The field device this condition watches is being worked on.";
    case ALARM_OUT_OF_SERVICE_REASON_REPLACEMENT:
        return "The field device this condition watches is being replaced.";
    case ALARM_OUT_OF_SERVICE_REASON_COMMISSIONING:
        return "Site commissioning work is expected to raise this condition repeatedly.";
    case ALARM_OUT_OF_SERVICE_REASON_AWAITING_REPAIR:
        return "A known defect is waiting on parts or a site visit.";
    case ALARM_OUT_OF_SERVICE_REASON_PLANT_CHANGE:
        return "The plant has changed and this condition needs rationalising rather than answering.";
    default:
        return "No recorded reason.";
    }
}
