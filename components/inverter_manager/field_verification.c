#include "field_verification.h"

field_verification_t field_verification_evaluate(const field_verification_input_t *input)
{
    if (!input) return FIELD_VERIFICATION_PENDING;

    /* Checked first and unconditionally. A simulator answers exactly what it was
     * written to answer, so no amount of clean communication with one is
     * evidence about physical equipment -- and it must not be able to reach
     * FAILED either, because a deliberately misbehaving simulator scenario is a
     * test passing, not a machine disagreeing with its manual. */
    if (input->lab_target) return FIELD_VERIFICATION_SIMULATOR;

    /* POSITIVE CONTRADICTIONS BEAT ELAPSED TIME.
     *
     * An identity probe that ran and did not match means this profile's register
     * map is being read off a device it does not describe. A command that was
     * issued and did not read back means the map is wrong about the one register
     * that matters most. Neither improves by polling for longer, so neither is
     * allowed to sit in PENDING accumulating reads until it looks like progress.
     */
    if (input->identity_supported && !input->identity_verified) {
        return FIELD_VERIFICATION_FAILED;
    }
    if (input->profile_commands && input->write_attempted && !input->write_confirmed) {
        return FIELD_VERIFICATION_FAILED;
    }

    if (input->consecutive_read_failures > 0U) return FIELD_VERIFICATION_PENDING;
    if (!input->telemetry_valid || input->telemetry_stale) return FIELD_VERIFICATION_PENDING;
    if (input->read_successes < FIELD_VERIFICATION_MIN_READS) return FIELD_VERIFICATION_PENDING;

    /* A profile that commands is not verified on reads alone. Reading a machine
     * correctly says nothing about whether it accepts what this profile writes,
     * and that is the half that moves a plant. */
    if (input->profile_commands && !(input->write_attempted && input->write_confirmed)) {
        return FIELD_VERIFICATION_PENDING;
    }

    return FIELD_VERIFICATION_VERIFIED;
}

const char *field_verification_name(field_verification_t state)
{
    switch (state) {
        case FIELD_VERIFICATION_VERIFIED: return "verified";
        case FIELD_VERIFICATION_FAILED: return "failed";
        case FIELD_VERIFICATION_SIMULATOR: return "simulator";
        case FIELD_VERIFICATION_PENDING:
        default: return "pending";
    }
}
