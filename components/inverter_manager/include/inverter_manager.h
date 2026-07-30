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
} inverter_fleet_commissioning_t;

void inverter_manager_commissioning_summary(inverter_fleet_commissioning_t *out_summary);

esp_err_t inverter_manager_init(void);
uint8_t inverter_manager_get_count(void);
float inverter_manager_get_total_rated_kw(void);
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
