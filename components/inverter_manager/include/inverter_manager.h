#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "inverter_types.h"
#include "inverter_write_confirmation.h"

#define INVERTER_PROBE_MAX_REGISTERS 8

typedef struct {
    bool profile_read_allowed;
    bool connection_initialized;
    bool identity_attempted;
    bool identity_ok;
    uint8_t identity_count;
    uint16_t identity_registers[INVERTER_PROBE_MAX_REGISTERS];
    esp_err_t identity_error;
    bool active_power_attempted;
    bool active_power_ok;
    uint8_t active_power_count;
    uint16_t active_power_registers[INVERTER_PROBE_MAX_REGISTERS];
    esp_err_t active_power_error;
} inverter_probe_result_t;

/*
 * Configuration-derived facts about the inverter fleet that the commissioning
 * gate needs. These describe what has been COMMISSIONED, not what happens to be
 * online: runtime availability is a separate, cycle-by-cycle question.
 *
 * `known` is false until inverter_manager_init() has completed. A caller that
 * sees it false must treat the fleet as uncommissioned, never as empty.
 */
typedef struct {
    bool known;
    uint8_t enabled_count;
    /* Enabled inverters whose assigned profile passes the production write gate
     * (production-approved AND carrying a manual-verified readback register). */
    uint8_t write_qualified_count;
    /* Enabled inverters whose assigned profile carries a readback register. */
    uint8_t readback_capable_count;
    /* Enabled inverters commandable ONLY because an engineer declared their
     * endpoint a simulator. These are counted separately from
     * write_qualified_count and never added to it: a lab declaration is not a
     * qualification, and conflating the two is how a lab result gets mistaken
     * for evidence about physical equipment. */
    uint8_t lab_only_count;
    /* True when any commandable inverter is a declared lab target. Every layer
     * above -- the commissioning gate, the status API, the UI -- must surface
     * this so a lab run can never be read as production control. */
    bool lab_mode;
    /* Configured rated power summed over inverters this controller may actually
     * command, whether by production qualification or lab declaration. */
    float commissioned_capacity_kw;

    /* Prerequisite enable registers (Solis tag 3070, Sungrow tag 5007, Chint/CPS
     * 0x2602). Reported separately from every other count because "this inverter
     * is blocked because its enable register is not confirmed" and "this
     * inverter's setpoint read back the wrong value" are different faults. They
     * look identical from the outside -- both end with the inverter out of the
     * commandable fleet -- and they have nothing in common as remedies.
     *
     * Enabled inverters whose profile says the device needs one. */
    uint8_t prerequisite_required_count;
    /* Of those, the ones NOT currently confirmed by a read of the register, and
     * therefore excluded from the commandable fleet right now. Includes the
     * unverifiable ones below, and includes every inverter that simply has not
     * been read yet: unknown is not confirmed. */
    uint8_t prerequisite_unconfirmed_count;
    /* Of those, the ones whose profile cannot describe a WRITABLE and READABLE
     * prerequisite at all. These are refused write authority permanently rather
     * than transiently, and no amount of polling will change it -- the remedy is
     * a manual citation for the register and its readback. */
    uint8_t prerequisite_unverifiable_count;
} inverter_fleet_commissioning_t;

void inverter_manager_commissioning_summary(inverter_fleet_commissioning_t *out_summary);

esp_err_t inverter_manager_init(void);
uint8_t inverter_manager_get_count(void);
float inverter_manager_get_total_rated_kw(void);
/* Commissioned capacity, whether or not it may be commanded right now. Only the
 * preview may use it; see the definition. */
float inverter_manager_get_enabled_rated_kw(void);

/*
 * How many times an inverter has rejoined the commandable fleet.
 *
 * While a machine is out, the control engine goes on commanding the ones that
 * remain, so the setpoint the absent machine holds is the one it had before the
 * link dropped -- and nothing rewrites it, because commands are only issued when
 * the target changes. The plant then runs with one inverter enforcing a stale
 * limit while the controller believes the whole fleet is on the current one.
 *
 * Monotonic and never reset, so an observer that misses a cycle cannot miss a
 * rejoin.
 */
uint32_t inverter_manager_fleet_rejoins(void);

/*
 * WHAT WOULD BE WRITTEN, WITHOUT WRITING IT.
 *
 * Computes the command this inverter would receive for a given fleet target,
 * through the same encoder the write path uses, and reports it whether or not
 * writing is permitted.
 *
 * This is the proof an engineer needs before a profile is ever allowed to
 * command real equipment. A wrong scale is the one error nothing downstream can
 * catch: 45% on a x10 register is the word 450, and sending 45 commands 4.5%
 * while the readback echoes 45, decodes with the same wrong scale, agrees with
 * the request and reports CONFIRMED. Seeing the word first is what makes it
 * visible to a person.
 *
 * Reads cached state only. No Modbus transaction, no write, no side effect.
 */
typedef struct {
    bool available;          /* False when the profile cannot describe a command. */
    float share_kw;          /* This machine's share of the fleet target. */
    float percent;           /* After the profile's own minimum/maximum clamp. */
    uint16_t address;        /* The register that would be written. */
    uint8_t function;        /* 6 or 16. */
    uint8_t word_count;
    uint16_t words[2];       /* Exactly what would go on the wire. */
    float raw_units_per_percent;
    bool would_write;        /* Whether the gates would actually let it through. */
    const char *blocked_by;  /* When they would not, in the firmware's own words. */
} inverter_command_preview_t;

bool inverter_manager_preview_command(uint8_t index, float fleet_target_kw,
                                      inverter_command_preview_t *out);
/*
 * Issues a fleet power setpoint.
 *
 * ESP_OK means the writes were ISSUED and the transport accepted them. It does
 * NOT mean the setpoints took effect: confirmation is deferred to the background
 * acquisition task so that neither this call nor any HTTP handler performs a
 * second blocking Modbus transaction or sleeps on the control period. Read
 * inverter_manager_fleet_write_confirmation() for whether they were confirmed.
 *
 * On a write failure every inverter already written in this plan is driven to
 * its safe fallback before returning - one write each, no sleeps.
 */
esp_err_t inverter_manager_set_total_power_kw(float target_kw);

/*
 * Worst confirmation state across every enabled, write-allowed inverter that has
 * been written to: mismatched beats unverified beats pending beats confirmed. A
 * fleet that has never been written to reports confirmed, because there is no
 * outstanding write to doubt.
 *
 * Reads already-acquired state only; performs no Modbus I/O and is safe to call
 * from the control task or an HTTP handler.
 */
inverter_write_state_t inverter_manager_fleet_write_confirmation(void);

/* True while any enabled inverter is latched out of the commandable fleet
 * because one of its writes could not be confirmed. Cleared per inverter only
 * when a later write is genuinely confirmed by readback. No Modbus I/O. */
bool inverter_manager_write_confirmation_fault(void);

/*
 * True while any enabled inverter that NEEDS a prerequisite enable register is
 * not currently confirmed to have it.
 *
 * Deliberately distinct from inverter_manager_write_confirmation_fault(). Both
 * remove an inverter from the commandable fleet, but they mean different things
 * and an engineer must be able to tell them apart:
 *
 *   confirmation fault  - the SETPOINT register did not read back the commanded
 *                         value, or could not be read at all.
 *   prerequisite fault  - the ENABLE register is not confirmed to hold, so a
 *                         setpoint would be accepted, echoed back and ignored.
 *                         The setpoint readback would look perfect.
 *
 * The second is the more dangerous of the two precisely because it is invisible
 * from the setpoint readback, which is why it gets its own report rather than
 * being folded into the existing one. Per-inverter detail, including whether the
 * register is unverifiable rather than merely unconfirmed, is in
 * inverter_data_t. Reads already-acquired state; no Modbus I/O.
 */
bool inverter_manager_prerequisite_enable_fault(void);
esp_err_t inverter_manager_probe_read_only(uint8_t inverter_index,
                                           inverter_probe_result_t *result);
bool inverter_manager_get_data(uint8_t inverter_index, inverter_data_t *out_data);

/*
 * True only when every enabled inverter reports INVERTER_STATE_ON_GRID from a
 * fresh sample. Missing status configuration, a failed read, an unmapped raw
 * value or a stale sample all yield INVERTER_STATE_UNKNOWN and make this false.
 *
 * Intended consumer: the control engine, which may command the grid power
 * limit directly (no ramp) only when this is true. It is NOT wired into the
 * control engine here - that file is owned elsewhere.
 */
bool inverter_manager_fleet_synchronised(void);
