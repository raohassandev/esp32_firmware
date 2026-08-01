#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "inverter_prerequisite.h"
#include "inverter_status.h"
#include "inverter_telemetry_block.h"
#include "inverter_write_confirmation.h"

typedef struct {
    float rated_power_kw;
    float measured_power_kw;
    /* Last CONFIRMED command. Only a readback that matched advances these. */
    float commanded_percent;
    float commanded_power_kw;
    /* Last value WRITTEN, confirmed or not. This is what the deferred
     * confirmation judges the next readback against; it is deliberately kept
     * apart from commanded_percent so an unconfirmed write can never be
     * mistaken for a confirmed one. */
    float requested_percent;
    float readback_percent;
    bool online;
    bool connection_initialized;
    bool identity_supported;
    bool identity_verified;
    bool telemetry_supported;
    bool telemetry_valid;
    bool telemetry_stale;
    bool has_command;
    bool has_readback;
    bool command_mismatch;
    uint32_t last_telemetry_ms;
    uint32_t last_command_ms;   /* last CONFIRMED command */
    uint32_t last_write_ms;     /* last write ISSUED, confirmed or not */
    uint32_t last_readback_ms;
    uint32_t read_successes;
    uint32_t read_errors;
    uint32_t consecutive_read_failures;
    uint32_t write_successes;
    uint32_t write_errors;
    uint32_t mismatch_count;
    int32_t last_error;

    /* Deferred write confirmation (P0-9). The write path never sleeps and never
     * issues a second Modbus transaction; the background telemetry task
     * resolves these from the readback it already polls.
     *
     * write_confirmation is an inverter_write_state_t: unverified, pending,
     * confirmed or mismatched. It is never "assumed success".
     * confirmation_fault latches when a write could not be confirmed, drops the
     * inverter out of the commandable fleet, and clears only when a later write
     * is genuinely confirmed. */
    uint8_t write_confirmation;
    bool write_issued;
    bool last_write_accepted;
    bool confirmation_fault;
    bool safe_zero_issued;
    uint32_t confirmed_count;
    uint32_t unverified_count;

    /* Measured-power confirmation.
     *
     * baseline_power_kw is the plant's measured output IMMEDIATELY BEFORE the last
     * write. It is captured at write time because after the command there is no
     * way to recover it, and without it a measured value below the commanded limit
     * cannot be distinguished from the sun going in -- which is the whole
     * difference between a demonstrated limit and a guess.
     *
     * write_proof is an inverter_write_proof_t saying what the verdict rests on,
     * so "confirmed" is never read without knowing what confirmed it, and an
     * unverified verdict can be told apart from an unknown one.
     * limit_demonstrated is true only for a limit proved by measurement. */
    float baseline_power_kw;
    bool baseline_valid;
    uint32_t baseline_sample_ms;
    uint8_t write_proof;
    bool limit_demonstrated;
    /* Verdicts that were UNVERIFIED because the measurement could not distinguish
     * an honoured limit from falling irradiance. Counted separately: it is not a
     * fault and it does not demand the safe fallback, but it must be visible,
     * because it means the limit in force is not known. */
    uint32_t ambiguous_count;

    /* Post-command scheduling-authority assertion (contention detector). A read
     * of a read-only register naming which authority owns scheduling of this
     * target; nothing here is ever written.
     *
     * authority_lost_count is the one to look at: non-zero means another master
     * took the target over after this controller commanded it. */
    bool authority_supported;
    bool authority_read_valid;
    bool authority_holds;
    uint16_t authority_raw;
    uint32_t last_authority_read_ms;
    uint32_t authority_lost_count;
    int32_t authority_last_error;

    /* Prerequisite enable register (Solis tag 3070, Sungrow tag 5007, Chint/CPS
     * 0x2602). Reported separately from the setpoint confirmation above, because
     * "the enable register is not confirmed" and "the setpoint read back the
     * wrong value" are different faults with different remedies, and an engineer
     * who cannot tell them apart will chase the wrong one.
     *
     * prerequisite_satisfied is false when this struct is zeroed, which is the
     * required polarity: unknown is not satisfied, and an inverter whose
     * prerequisite is not satisfied is excluded from the commandable fleet. It
     * is set ONLY by a successful read of the enable register - never by an
     * accepted write. */
    bool prerequisite_required;
    bool prerequisite_describable;
    bool prerequisite_satisfied;
    bool prerequisite_unverifiable;
    bool prerequisite_write_issued;
    bool prerequisite_read_valid;
    bool prerequisite_holds;
    uint16_t prerequisite_raw;
    uint32_t last_prerequisite_read_ms;
    uint32_t last_prerequisite_write_ms;
    uint32_t prerequisite_confirmed_count;
    uint32_t prerequisite_write_count;
    /* Transitions from satisfied back to not satisfied. Non-zero means somebody
     * or something switched the limit off underneath this controller, which for
     * Solis returns the machine to 100 %. */
    uint32_t prerequisite_lost_count;
    int32_t prerequisite_last_error;

    /* Operational status, read-only and independent of the command path.
     * status_state defaults to INVERTER_STATE_UNKNOWN (0). */
    inverter_state_t status_state;
    bool status_supported;
    bool status_raw_valid;
    bool status_stale;
    uint32_t status_raw;
    uint32_t last_status_ms;
    uint32_t status_read_successes;
    uint32_t status_read_errors;
    int32_t status_last_error;

    /* Everything the machine measures: DC strings, AC per phase, yield,
     * temperature, device status.
     *
     * NOT a control input. measured_power_kw above is the one number the
     * confirmation evaluator and the fleet cap use, and it keeps its own
     * acquisition at the control rate. This block is for a person to look at,
     * is polled on a slower cadence, and its absence never blocks or biases a
     * control decision -- so a family with no transcribed block loses a page and
     * nothing else.
     *
     * measurements.valid stays false until a block read succeeds, so a page can
     * say "not read" rather than draw an inverter at 0 V. */
    inverter_measurements_t measurements;
    uint32_t measurements_updated_ms;
} inverter_data_t;
