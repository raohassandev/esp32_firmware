#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "inverter_status.h"
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
} inverter_data_t;
