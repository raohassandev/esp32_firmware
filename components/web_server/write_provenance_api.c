#include "write_provenance_api.h"

#include <math.h>

#include "esp_err.h"
#include "inverter_manager.h"
#include "inverter_write_confirmation.h"

/* The one sentence the interface must not have to invent. It is published
 * unconditionally, whatever the verdict, because it is true whatever the verdict:
 * the two kinds of evidence are not equally strong, and a reader who cannot tell
 * them apart cannot read "confirmed" safely. Cited from
 * inverter_write_confirmation.h, which cites the manuals. */
static const char *const LIMIT_EVIDENCE_NOTICE =
    "A confirmed write is not one thing. write_proof says what the verdict rests "
    "on. measured_power means measured output was ABOVE the new limit before the "
    "command and at or below it after, so the limit is demonstrated. "
    "setpoint_readback means the setpoint register read back matching: on some "
    "devices that is an applied value, and on a plant-level logger it is an echo "
    "of a stored command and proves acceptance only. ambiguous_headroom means "
    "output is at or below the limit but was ALREADY at or below it, which is "
    "equally consistent with the limit being honoured and with falling "
    "irradiance; it proves nothing and is reported unverified. Only "
    "limit_demonstrated says a limit was actually shown to be in force.";

static void add_finite_value(cJSON *object, const char *name, float value)
{
    if (isfinite(value)) cJSON_AddNumberToObject(object, name, value);
    else cJSON_AddNullToObject(object, name);
}

void write_provenance_collect(write_provenance_rollup_t *rollup)
{
    if (!rollup) return;
    write_provenance_reset(rollup);
    const uint8_t count = inverter_manager_get_count();
    for (uint8_t i = 0; i < count; ++i) {
        inverter_data_t data = {0};
        /* Already-acquired state. inverter_manager_get_data() copies the
         * background task's last snapshot and performs no Modbus transaction. */
        if (!inverter_manager_get_data(i, &data)) continue;
        write_provenance_accumulate(rollup, &data);
    }
}

void write_provenance_add_fleet(cJSON *root, const write_provenance_rollup_t *rollup)
{
    if (!root) return;
    write_provenance_rollup_t empty;
    write_provenance_reset(&empty);
    const write_provenance_rollup_t *roll = rollup ? rollup : &empty;

    /* What the fleet verdict rests on: the WEAKEST proof held by any inverter
     * that has been written to, so a fleet is only ever as well evidenced as its
     * least well evidenced member. */
    cJSON_AddStringToObject(root, "write_proof",
                            inverter_write_proof_name(roll->weakest_proof));
    /* True only when every written inverter demonstrated its limit by
     * measurement. One echo is enough to make this false. */
    cJSON_AddBoolToObject(root, "limit_demonstrated",
                          write_provenance_limit_demonstrated(roll));
    /* The condition under which the word "confirmed" must never appear on its
     * own: at least one inverter is confirmed on a readback that may be an echo
     * of a stored command. */
    cJSON_AddBoolToObject(root, "setpoint_echo_only",
                          write_provenance_echo_only(roll));
    cJSON_AddNumberToObject(root, "written_count", roll->written_count);
    /* Three separate figures. Summing them would erase the distinction between a
     * limit that was shown to be in force and one that was merely accepted. */
    cJSON_AddNumberToObject(root, "limit_demonstrated_count",
                            roll->limit_demonstrated_count);
    cJSON_AddNumberToObject(root, "setpoint_echo_count", roll->setpoint_echo_count);
    cJSON_AddNumberToObject(root, "ambiguous_now_count", roll->ambiguous_now_count);
    /* Cumulative. Not a fault and it does not demand the safe fallback, but it
     * must be visible: it means the limit in force is not known. */
    cJSON_AddNumberToObject(root, "ambiguous_count", roll->ambiguous_total);
    /* Non-zero means another master took a command target over after this
     * controller commanded it. Reported as events and as machines, because one
     * inverter losing authority forty times and forty losing it once are
     * different findings. */
    cJSON_AddNumberToObject(root, "authority_lost_count", roll->authority_lost_total);
    cJSON_AddNumberToObject(root, "authority_lost_inverters",
                            roll->authority_lost_inverters);
    cJSON_AddStringToObject(root, "limit_evidence_notice", LIMIT_EVIDENCE_NOTICE);
}

void write_provenance_add_inverter(cJSON *item, const inverter_data_t *data)
{
    if (!item) return;
    if (!data) {
        /* Fail closed rather than omit: an absent key reads as not applicable,
         * and "we could not read this inverter" is not "nothing to report". */
        cJSON_AddStringToObject(item, "write_proof",
                                inverter_write_proof_name(INVERTER_WRITE_PROOF_NONE));
        cJSON_AddBoolToObject(item, "limit_demonstrated", false);
        cJSON_AddNullToObject(item, "ambiguous_count");
        cJSON_AddNullToObject(item, "authority_lost_count");
        return;
    }

    cJSON_AddStringToObject(item, "write_proof",
                            inverter_write_proof_name(
                                (inverter_write_proof_t)data->write_proof));
    /* Never true from a setpoint readback, however exactly it matched. */
    cJSON_AddBoolToObject(item, "limit_demonstrated", data->limit_demonstrated);
    cJSON_AddNumberToObject(item, "ambiguous_count", data->ambiguous_count);
    cJSON_AddNumberToObject(item, "authority_lost_count", data->authority_lost_count);

    /* The measurement the verdict was made from, and the pre-command baseline
     * without which a limit can never be demonstrated. Both are reported so the
     * ambiguous verdict can be READ rather than taken on trust: output below the
     * limit with a baseline that was already below it is the ambiguous case, and
     * these two figures are what shows that. */
    add_finite_value(item, "measured_power_kw", data->measured_power_kw);
    add_finite_value(item, "baseline_power_kw", data->baseline_power_kw);
    cJSON_AddBoolToObject(item, "baseline_valid", data->baseline_valid);

    /* Post-command scheduling authority. A read-only contention detector; nothing
     * here is ever written. Its own object so it cannot be mistaken for one of
     * the confirmation states. */
    cJSON *authority = cJSON_AddObjectToObject(item, "authority");
    if (authority) {
        cJSON_AddBoolToObject(authority, "supported", data->authority_supported);
        cJSON_AddBoolToObject(authority, "read_valid", data->authority_read_valid);
        cJSON_AddBoolToObject(authority, "holds", data->authority_holds);
        cJSON_AddNumberToObject(authority, "raw", data->authority_raw);
        cJSON_AddNumberToObject(authority, "lost_count", data->authority_lost_count);
        cJSON_AddNumberToObject(authority, "last_read_ms", data->last_authority_read_ms);
        cJSON_AddNumberToObject(authority, "last_error", data->authority_last_error);
        cJSON_AddStringToObject(authority, "last_error_name",
                                esp_err_to_name((esp_err_t)data->authority_last_error));
    }
}
