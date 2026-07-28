#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "inverter_command_policy.h"

static inverter_command_evidence_t base(void)
{
    return (inverter_command_evidence_t){
        .write_succeeded = true,
        .readback_supported = true,
        .readback_succeeded = true,
        .requested_percent = 42.0f,
        .readback_percent = 42.2f,
        .tolerance_percent = 0.5f,
        .attempts_completed = 1,
        .maximum_attempts = 3,
        .safe_fallback_percent = 0.0f,
    };
}

int main(void)
{
    inverter_command_evidence_t e = base();
    inverter_command_decision_t d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_CONFIRMED && d.confirmed);

    e = base();
    e.write_succeeded = false;
    d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_RETRY);

    e = base();
    e.readback_supported = false;
    d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_ROLLBACK && !d.confirmed);

    e = base();
    e.readback_percent = 50.0f;
    d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_RETRY && d.mismatch);

    e = base();
    e.readback_percent = 50.0f;
    e.attempts_completed = 3;
    d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_ROLLBACK && d.mismatch);

    e = base();
    e.readback_percent = NAN;
    e.attempts_completed = 3;
    d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_ROLLBACK && !d.confirmed);

    e = base();
    e.requested_percent = INFINITY;
    d = inverter_command_decide(&e);
    assert(d.action == INVERTER_COMMAND_FAILED_SAFE && d.next_percent == 0.0f);

    puts("inverter command policy tests passed");
    return 0;
}
