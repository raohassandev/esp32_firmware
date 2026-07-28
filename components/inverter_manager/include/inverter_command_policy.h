#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INVERTER_COMMAND_PENDING = 0,
    INVERTER_COMMAND_CONFIRMED,
    INVERTER_COMMAND_RETRY,
    INVERTER_COMMAND_ROLLBACK,
    INVERTER_COMMAND_FAILED_SAFE
} inverter_command_action_t;

typedef struct {
    bool write_succeeded;
    bool readback_supported;
    bool readback_succeeded;
    float requested_percent;
    float readback_percent;
    float tolerance_percent;
    uint8_t attempts_completed;
    uint8_t maximum_attempts;
    bool rollback_write_succeeded;
    bool rollback_readback_succeeded;
    float safe_fallback_percent;
} inverter_command_evidence_t;

typedef struct {
    inverter_command_action_t action;
    bool confirmed;
    bool mismatch;
    float next_percent;
} inverter_command_decision_t;

inverter_command_decision_t inverter_command_decide(const inverter_command_evidence_t *evidence);

#ifdef __cplusplus
}
#endif
