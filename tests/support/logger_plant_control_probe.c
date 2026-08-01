/* Executes the real catalogue and the real confirmation evaluator, so that
 * tests/logger_plant_control_source_contract.py checks behaviour rather than
 * text.
 *
 * Two things are printed:
 *
 *   1. One line per profile describing how it confirms its command, taken from
 *      the compiled catalogue and from the header's own predicates. This is what
 *      catches a profile that asks for measured-power confirmation and describes
 *      it incompletely -- a state the confirmation evaluator refuses, but which
 *      would otherwise be invisible until a plant was commanded.
 *
 *   2. The verdict the evaluator actually returns for the ambiguity that decides
 *      whether measured-power confirmation is safe or dangerous: measured output
 *      below a commanded limit having ALREADY been below it beforehand. The rule
 *      is executed, never reimplemented, because a Python mirror of a safety rule
 *      is one more thing that can drift out of step with the firmware.
 *
 * Output format, tab-separated, one record per line:
 *   PROFILE  id  qualification  authority_with_lab_declared  passes_production
 *            measured_mode  measured_described  tolerance_kw  tolerance_pct
 *            authority_described  authority_function  authority_address
 *            authority_expected  command_interval_ms  settle_ms
 *   CASE     name  state  limit_demonstrated  proof  requires_safe_zero  settled
 */

#include <stdio.h>
#include <string.h>

#include "inverter_profiles.h"
#include "inverter_write_confirmation.h"

static const char *measured_mode_name(inverter_measured_confirm_mode_t mode)
{
    switch (mode) {
    case INVERTER_MEASURED_CONFIRM_CORROBORATING: return "corroborating";
    case INVERTER_MEASURED_CONFIRM_REQUIRED: return "required";
    case INVERTER_MEASURED_CONFIRM_NONE:
    default: return "none";
    }
}

/* The plant case the SmartLogger profile exists for: 100 kW of inverters
 * commanded to 60 %, so the limit is 60 kW. Every case below is this with one
 * thing changed, which is how the ambiguity is isolated. */
static inverter_write_evidence_t plant_evidence(void)
{
    inverter_write_evidence_t e;
    memset(&e, 0, sizeof(e));
    e.readback_supported = true;
    e.write_issued = true;
    e.write_accepted = true;
    e.readback_valid = true;
    e.readback_after_write = true;
    e.commanded_percent = 60.0f;
    e.readback_percent = 60.0f;
    e.tolerance_percent = 1.0f;
    e.age_since_write_ms = 2000;
    e.settle_ms = 1000;
    e.deadline_ms = 5000;
    e.measured_mode = INVERTER_MEASURED_CONFIRM_REQUIRED;
    e.capacity_kw = 100.0f;
    e.measured_tolerance_percent_of_capacity = 2.0f;
    e.measured_valid = true;
    e.measured_after_write = true;
    e.measured_kw = 55.0f;
    e.baseline_valid = true;
    e.baseline_before_write = true;
    e.baseline_kw = 90.0f;
    return e;
}

static void report(const char *name, const inverter_write_evidence_t *evidence)
{
    inverter_write_verdict_t v = inverter_write_confirmation_evaluate(evidence);
    printf("CASE\t%s\t%s\t%d\t%s\t%d\t%d\n", name,
           inverter_write_state_name(v.state), v.limit_demonstrated ? 1 : 0,
           inverter_write_proof_name(v.proof), v.requires_safe_zero ? 1 : 0,
           v.settled ? 1 : 0);
}

int main(void)
{
    for (size_t index = 0; index < inverter_profiles_count(); ++index) {
        const inverter_profile_t *p = inverter_profiles_get(index);
        if (!p) continue;
        printf("PROFILE\t%s\t%s\t%s\t%d\t%s\t%d\t%.3f\t%.3f\t%d\t%u\t%u\t%u\t%u\t%u\n",
               p->id,
               inverter_profile_qualification_label(p->qualification),
               inverter_write_permission_label(inverter_profile_write_permission(p)),
               inverter_profile_allows_write(p) ? 1 : 0,
               measured_mode_name(p->measured_power_confirm),
               inverter_profile_measured_confirmation_described(p) ? 1 : 0,
               (double)p->measured_tolerance_kw,
               (double)p->measured_tolerance_percent_of_capacity,
               inverter_profile_command_authority_described(p) ? 1 : 0,
               (unsigned)p->command_authority_function,
               (unsigned)p->command_authority_address,
               (unsigned)p->command_authority_expected,
               (unsigned)p->min_command_interval_ms,
               (unsigned)p->power_limit_settle_ms);
    }

    /* Demonstrated: above the limit before, at or below it after. */
    inverter_write_evidence_t e = plant_evidence();
    report("demonstrated", &e);

    /* THE CRUX. Falling irradiance: already below the limit before the command,
     * still below it after. Indistinguishable from an honoured limit, so it must
     * not be confirmed -- and it must not demand a safe zero either. */
    e = plant_evidence();
    e.baseline_kw = 30.0f;
    e.measured_kw = 21.0f;
    report("falling_irradiance", &e);

    /* No baseline at all: the state after a restart. */
    e = plant_evidence();
    e.baseline_valid = false;
    report("no_baseline", &e);

    /* A matching setpoint readback must not rescue the ambiguity in required
     * mode: the logger's readback is an echo of a stored command. */
    e = plant_evidence();
    e.baseline_kw = 30.0f;
    e.measured_kw = 21.0f;
    e.readback_percent = 60.0f;
    report("ambiguous_with_matching_echo", &e);

    /* A command of 100 % can never be demonstrated. */
    e = plant_evidence();
    e.commanded_percent = 100.0f;
    e.readback_percent = 100.0f;
    e.baseline_kw = 99.0f;
    e.measured_kw = 96.0f;
    report("full_output", &e);

    /* Output above the limit past the settle window: unambiguous, and the
     * direction that protects the generator. */
    e = plant_evidence();
    e.measured_kw = 85.0f;
    report("above_limit", &e);

    /* Another master owns plant scheduling after our own command. */
    e = plant_evidence();
    e.authority_checked = true;
    e.authority_valid = true;
    e.authority_after_write = true;
    e.authority_holds = false;
    report("contention", &e);

    /* Ours, and demonstrated. */
    e = plant_evidence();
    e.authority_checked = true;
    e.authority_valid = true;
    e.authority_after_write = true;
    e.authority_holds = true;
    report("authority_held", &e);

    /* Incompletely described measured evidence is refused, never reverted to
     * confirming on the echo. */
    e = plant_evidence();
    e.measured_tolerance_percent_of_capacity = 0.0f;
    report("no_tolerance_stated", &e);
    e = plant_evidence();
    e.capacity_kw = 0.0f;
    report("no_capacity", &e);

    /* A zeroed struct must never read as confirmed. */
    inverter_write_evidence_t zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    report("zeroed", &zeroed);

    return 0;
}
