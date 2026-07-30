#include "inverter_profiles.h"

#include <string.h>

#define LEGACY_SAFE_DEFAULT_PROFILE_ID "custom-advanced-modbus"
#define SAFE_DEFAULT_PROFILE_ID "custom.modbus-percent-v1"

/*
 * Manufacturer profiles remain deliberately write-locked until exact manuals
 * and physical command/readback evidence exist. The SolTrix entries below are
 * simulator-only contracts. Their addresses must never be reused as production
 * manufacturer evidence.
 *
 * No profile below configures an operating-status register. Those addresses are
 * NOT guessed. Several manuals do name a status register, but the code tables
 * they refer to are either in a separate document that is not available or are
 * per-model, and a plausible-looking wrong mapping could report "on grid" while
 * an inverter is still checking or faulted, which would let the controller
 * command full output directly. Every profile therefore leaves that description
 * unconfigured and every inverter reports INVERTER_STATE_UNKNOWN until a
 * commissioning engineer supplies a verified one.
 *
 * The manufacturer profiles that DO carry addresses (Huawei, Solis, Growatt,
 * Sungrow, Chint/CPS) are manual transcriptions with per-value citations, kept at
 * DOCUMENTED. See docs/BRAND_REGISTER_EVIDENCE.md for the extracted evidence and
 * for the disagreements found against the lab simulator.
 */
static const inverter_profile_t PROFILES[] = {
    {
        .id = SAFE_DEFAULT_PROFILE_ID,
        .manufacturer = "Custom",
        .model_family = "Advanced Modbus percentage control",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Operator-supplied verified register map required",
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    /*
     * Contract for the SolTrix ESP firmware lab simulator (huawei-v3 profile),
     * which models the Huawei SUN2000 register layout behind a closed-loop plant
     * model. Every field below was measured against the running simulator, not
     * copied from a manual and not copied from the older v1 entry, whose probe
     * addresses (40000, 40010) this simulator does not implement at all.
     *
     * Measured 2026-07-30 against config.esp-firmware-lab.json, unit 1:
     *   FC03 30000  -> "SUN2000-SIM"; first word 0x5355 ("SU") is the identity
     *   FC03 32080  -> I32 watts, high word first (85102 = 85.102 kW)
     *   FC03 32089  -> operating state; the simulator reports 0x0200 on grid
     *   FC06 40125  -> percentage limit, raw = percent x 10
     *   FC03 40125  -> readback of the ACTIVE limit, percent x 10
     *
     * IMPORTANT, and the reason this profile exists separately from any
     * manufacturer entry: the simulator applies a 40125 write ~1500 ms LATER,
     * and until then the readback still reports the previous active limit. A
     * correct controller must therefore treat that window as pending rather than
     * as a mismatch. Whether real SUN2000 hardware behaves the same way is NOT
     * established here and must be confirmed on site.
     *
     * simulator_only stays true: this validates the controller's logic against a
     * model, which is not evidence about physical equipment. It must never be
     * promoted, and must never be used as a manufacturer register reference.
     */
    {
        .id = "soltrix.sim.huawei.v3",
        .manufacturer = "SolTrix Simulator",
        .model_family = "Huawei SUN2000 layout, closed-loop plant model",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "Measured against SolTrix inverter-simulator huawei-v3; "
                            "not a manufacturer manual",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 30000,
        .identity_words = 1,
        /* First two characters of the model string, "SU". */
        .identity_expected = 0x5355,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 32080,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 40125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 40125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_S16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        /* Measured: this simulator defers a 40125 write by ~1500 ms and reports
         * the previous active limit until then. 2500 ms leaves margin without
         * approaching the 5000 ms confirmation deadline. */
        .power_limit_settle_ms = 2500,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "soltrix.sim.huawei.v1",
        .manufacturer = "SolTrix Simulator",
        .model_family = "Huawei SUN2000 contract",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "SolTrix commit fe84696 simulator contract; synthetic Modbus map",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 40000,
        .identity_words = 1,
        .identity_expected = 0xA021,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 40010,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 40125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 40125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "soltrix.sim.goodwe.v1",
        .manufacturer = "SolTrix Simulator",
        .model_family = "GoodWe commercial contract",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "SolTrix ESP32 integration simulator; synthetic map",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 41000,
        .identity_words = 1,
        .identity_expected = 0xA022,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 41010,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 41125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 41125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "soltrix.sim.solis.v1",
        .manufacturer = "SolTrix Simulator",
        .model_family = "Solis commercial contract",
        .protocol = "Modbus TCP simulator",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        .qualification = INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED,
        .manual_reference = "SolTrix ESP32 integration simulator; synthetic map",
        .simulator_only = true,
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 42000,
        .identity_words = 1,
        .identity_expected = 0xA023,
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 42010,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 42125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 42125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "huawei.sun2000.pending",
        .manufacturer = "Huawei",
        .model_family = "SUN2000 family",
        .protocol = "Modbus",
        .connection = INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY,
        /* DOCUMENTED, not qualified. Every field below is transcribed from the
         * manufacturer manual and NONE of it has been exercised against physical
         * hardware, so the production write gate still refuses this profile. It
         * becomes commandable only against an endpoint an engineer has declared a
         * simulator, which is how the map gets validated before anyone travels.
         * Raising this profile to production approval requires physical readback
         * evidence from the machine itself.
         *
         * Source: "Huawei Inverter Modbus Interface Definitions (V3.0)",
         * Issue 01 (2023-01-17).
         *
         * Addressing: the manual's decimal addresses are the raw on-the-wire
         * register addresses, not 1-based tag numbers. It states a command
         * "register address: 40200/0X9D08", and 0x9D08 == 40200, so the decimal
         * values are used directly with no offset. This is the trap that has bitten
         * this project before, and it is settled by that line.
         *
         * Register 1 "Model", RO STR, 30000, 15 registers ("Nameplate name of
         * machine"). The identity probe reads the first word only: "SU" = 0x5355,
         * the start of every SUN2000 nameplate.
         *
         * Signal 171 "active power", RO I32, unit kW, gain 1000, 32080, 2
         * registers. Gain 1000 means raw watts, hence scale 0.001.
         *
         * Signal 409 "Active Power Percentage Derating [Low Precision]", RW I16,
         * unit %, gain 10, 40125, 1 register, described as the "Active fine
         * adjustment interface". Used here as the command register because it is
         * the conventional third-party derating interface.
         *
         * NOT YET DECIDED, and a site-verification item: signal 419 "Active Power
         * Percentage Control [Low Precision]", RW I16, %, gain 10, 40199, is
         * described by Huawei as the interface "used in distributed mode ... in
         * anti-backcurrent control to control the upper limit of the output active
         * power". Anti-backcurrent is exactly this product's application, so 40199
         * may be the more correct register. Both encode percent x10. The lab
         * simulator applies 40125 with a delay and 40199 immediately, which hints
         * that 40199 is the control-rate interface, but a hint from a model is not
         * evidence. Confirm on the physical machine before promoting either.
         *
         * Signal 432 "active power gradient", RW U32, unit %/s, gain 1000, 42017,
         * 2 registers, "Limiting the speed of power change". The inverter has its
         * own ramp limiter; this firmware ramps in the control engine and does not
         * write it. Worth reconciling on site so the two do not fight.
         *
         * Signal 178 "Device Status", RO E16, at 32089, 1 register. The manual
         * defers the code table to a separate "Inverter Key Signal Extension
         * Description" that is NOT among the manuals available, so the meaning of
         * the codes is unknown and no operating-state description is set here.
         * Every inverter therefore reports INVERTER_STATE_UNKNOWN rather than a
         * guessed state.
         *
         * No settle or response time for a percentage command is documented
         * anywhere in this manual, so power_limit_settle_ms is left at the
         * firmware default and MUST be measured during commissioning.
         *
         * COMMISSIONING PREREQUISITE, and the reason this profile is not marked
         * requires_prerequisite_enable: when a SmartLogger sits in the Modbus path
         * -- which this profile's connection type assumes -- each inverter needs
         * "Remote power schedule" set to Enable before a percentage command has any
         * effect (SmartLogger 3000A manual pp.102/128, see
         * docs/SMARTLOGGER_PATH_ANALYSIS.md). That is a logger MENU setting, not a
         * register this firmware writes, so it is a one-time human commissioning
         * step rather than a write the controller must sequence. It is therefore
         * not the same case as Solis, Sungrow or Chint/CPS, whose setpoints need a
         * register written and which are refused outright.
         *
         * The hazard is identical though: until it is enabled the setpoint
         * register still accepts a write and still echoes it back, so a command
         * reads as confirmed while the inverter ignores it. The controller cannot
         * detect this. It is an explicit step in
         * docs/SITE_COMMISSIONING_RUNBOOK.md 1.4, along with checking register
         * 42019 (a non-zero schedule-validity period makes a commanded limit
         * self-expire) and 40737 (anything other than remote scheduling means
         * something else owns the plant). */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Huawei Inverter Modbus Interface Definitions (V3.0), "
                            "Issue 01 (2023-01-17); not qualified on hardware",
        .has_identity_probe = true,
        .identity_function = 3,
        .identity_address = 30000,
        .identity_words = 1,
        .identity_expected = 0x5355, /* "SU" */
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 32080,
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 40125,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 40125,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_S16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 500,
        .telemetry_stale_timeout_ms = 3000,
    },
    {
        .id = "goodwe.commercial.pending",
        .manufacturer = "GoodWe",
        .model_family = "GT series three-phase grid-connected string inverter",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* DOCUMENTED, not qualified. Transcribed from the manufacturer manual;
         * nothing below has been exercised against physical hardware or against
         * the lab simulator.
         *
         * Source: "GoodWe Modbus Protocol of Inverter -- GT Series (Customer
         * Version)", V1.0, 2023-08-25 (PDF
         * GoodWe_grid-tied_GT-series_Modubus_Protocol(8).pdf). Title p.1, change
         * record p.3, scope p.4: "applies to the communication between GoodWe PV
         * grid-connected inverters -- GT series and the monitoring device".
         *
         * ADDRESSING -- PROVEN BY A WORKED FRAME, which is why this is the most
         * trustworthy map in this file. The register table prints plain decimal
         * addresses and section 9 "Example" p.57 exercises one of them:
         *   Example 1, "03 Function Code to Read Grid Frequency":
         *     "Host sends: F7 03 7D 55 00 01 98 E0"
         *     "Inverter responds: F7 03 02 13 88 7D 07 ... The frequency value
         *      received is 0x1388 (decimal 5000). After dividing by a gain factor
         *      of 100, the frequency value is 50.00 Hz."
         *   The address on the wire is 0x7D55 = 32085 decimal, and the register
         *   table row for grid frequency reads "32085 grid frequency RO U16 1 100
         *   Hz". Table decimal == wire PDU, with NO offset and NO -1. There is no
         *   ambiguity to resolve at commissioning for this brand.
         * That same example also fixes the GAIN CONVENTION for the whole table:
         * real value = raw / Gain ("after dividing by a gain factor of 100").
         *
         * WORD ORDER: documented, p.4 section 2: "Long Integer Data 2 4 Two
         * separate forwarding, from the most significant bit to the least
         * significant bit" -- most significant word first, i.e. AB.
         *
         * ACTIVE POWER: register table, "32080 active Power RO S32 2 1000 kW".
         * Two registers, S32, gain 1000, so real kW = raw / 1000 -> scale 0.001.
         *
         * COMMAND: register table p.12, "42407 Active power percentage
         * derating(0.1%) RW U16 1 10 %Pn [0,1100]". FC 0x10 (section 3.2 "Set the
         * Content of Register (Function code: 10H)"); this firmware writes a
         * single register with FC 0x06, which the map's RW access permits and
         * which the two 0x10 examples show is the documented write path -- the
         * function code is the one item below to confirm on the first lab write,
         * because the manual only ever demonstrates 0x10.
         * SCALE: gain 10 and the name's own "(0.1%)" both give raw 10 per percent.
         * RANGE: the manual's range is [0,1100], i.e. 0-110%. maximum_percent is
         * held at 100 anyway: nothing in this document says a GT-series machine
         * may be scheduled into overload, and 110% is not a number to hand a
         * 100 kW inverter on the strength of a range column.
         *
         * READBACK: the same register 42407 read with FC 0x03 (section 3.1 "Read
         * the Content of Register (Function code: 03H)"). RW in the access column,
         * and the address space is unified -- Example 1 reads a 32xxx register
         * with 0x03 -- so a 42xxx RW register is readable the same way. This is a
         * setpoint echo; no separate applied-limit register is documented.
         *
         * *** EEPROM WEAR -- A HARD BLOCKER FOR PRODUCTION, NOT A STYLE NOTE ***
         * The remark column on 42407 (and on every register in the 424xx block)
         * reads: "Storage, does not support high-frequency write operations".
         * 42407 is flash-backed. This controller's whole purpose is to move a
         * setpoint continuously against a moving generator load, and its default
         * control period is 250 ms. Writing a flash-backed register at that rate
         * destroys the inverter's memory -- a permanent hardware failure on a
         * customer's machine, caused by the controller working as designed.
         * The manual states the prohibition but gives NO permitted write rate and
         * NO write-cycle budget, so no min_command_interval_ms is asserted here:
         * inventing a timing number is exactly what must not happen. GoodWe must
         * supply the permitted rate, or a rate-of-change/deadband strategy must be
         * agreed, BEFORE this profile is considered for production. Recorded here
         * because it is invisible in the register numbers themselves.
         * This is NOT the accept-and-echo hazard, so requires_prerequisite_enable
         * is not set -- a wrong flag would misreport the reason for a refusal.
         *
         * PREREQUISITE ENABLE: none documented. There is no enable, unlock,
         * dispatch-mode or password register for 42407 anywhere in this map.
         * 42430 "Active power compensation response mode" (0 Off / 1 Slope /
         * 2 Low-pass filter) shapes the RESPONSE to a dispatch value; the document
         * does not make it a precondition for the value taking effect, so it is
         * not treated as one and is not written.
         *
         * RAMP: 42433 "Active power gradient, RW, U32, 2, gain 10, %Pn/min,
         * [1,6000000]". A real documented gradient, in %Pn/min. This firmware
         * ramps in the control engine and does not write it; reconcile on site so
         * the two limiters do not fight.
         *
         * COMMS-LOSS FAIL-SAFE: none documented for the Modbus control link. No
         * watchdog, timeout or fallback-limit register for a third-party
         * controller appears in this map. If the link drops the last written
         * limit simply stands, which for a PV-DG plant means the inverter holds
         * its limit rather than reverting -- state this to the owner explicitly,
         * because a generator-protection scheme usually wants the opposite.
         *
         * IDENTITY: unset. 35502 "Device serial" is a string, and no register in
         * this map holds a numeric model/family constant with a stated value that
         * an integer identity probe could compare against.
         *
         * SETTLE TIME: not documented, so it stays at the firmware default and
         * MUST be measured during commissioning.
         *
         * POLL RATE: a controller choice, not a manual value. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "GoodWe Modbus Protocol of Inverter GT Series (Customer Version) "
                            "V1.0 2023-08-25 register table and section 9 example 1; "
                            "not qualified on hardware",
        .has_active_power = true,
        .active_power_function = 3,
        .active_power_address = 32080, /* PDU as printed -- proven by the 0x7D55/32085 frame */
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_S32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB, /* p.4: most significant word first */
        .active_power_scale = 0.001f, /* gain 1000 -> kW */
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 42407,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f, /* gain 10 and the name's own "(0.1%)" */
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 42407,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    {
        .id = "solis.commercial.pending",
        /* Manual tag 3070 must be set to 0xAA before the setpoint at 3051 does
         * anything. This firmware cannot sequence that write, and 3051 echoes
         * the value back regardless, so commanding it would report CONFIRMED
         * for a limit the inverter ignores. Refused until the prerequisite can
         * be written and verified. */
        .requires_prerequisite_enable = true,
        .manufacturer = "Solis",
        .model_family = "Three-phase commercial string inverter (RS485 MODBUS protocol)",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* DOCUMENTED, not qualified. Transcribed from the manufacturer manual;
         * nothing below has been exercised against physical hardware or even
         * against the lab simulator, so the production write gate still refuses
         * this profile. Raising it further requires physical readback evidence.
         *
         * Source: Solis "RS485_MODBUS Communication Protocol", translated
         * 2021-05-06 (PDF Solis.pdf in SolTrix/Manuals/Inverter).
         *
         * ADDRESSING -- the trap, and this manual is explicit and NOT uniform:
         *   Section 5.2 (inverter type, FC 0x04), p.10: "The following table has
         *     the same address with the actual address of the message frame. No
         *     need extra offset or transform."  -> PDU = tag.
         *   Section 5.3 (operation info, FC 0x04), p.10: "the register address
         *     needs to offset one bit. Example: register address: 3000, the send
         *     address is 2999."  -> PDU = tag - 1.
         *   Section 5.6 (settings, FC 0x03/0x06/0x10), p.24: "the register
         *     address needs to offset one bit. Example: register address: 3007,
         *     the send address is 3006."  -> PDU = tag - 1.
         * Every address below is therefore stated as the PDU value that goes on
         * the wire, with the manual tag named beside it.
         *
         * ACTIVE POWER: section 5.3, p.11, tag "3005 - 3006 Active power, U32,
         * 1W". PDU 3004, 2 registers, scale 0.001 for kW.
         * Word order is NOT stated for numeric U32 anywhere in this manual. The
         * only ordering statement in the document is the serial-number example at
         * p.13 (tag 3061 "SN High 4" ... tag 3064 "SN LOW 4"), i.e. most
         * significant word at the lower address, and the lab simulator agrees.
         * AB is used on that basis and MUST be checked on the first read: a
         * reversed word order turns 100 kW into a nonsense number.
         *
         * COMMAND: section 5.6, p.26, tag "3052 Power limitation, U16, Range
         * (0-100%), 100% = rated". PDU 3051, FC 0x06.
         * SCALE: the remark column says "10000 <--> 100%", i.e. 100 raw units per
         * percent. The Unit column on the same row says "1%", which contradicts
         * it. The remark is the one that is internally consistent, confirmed
         * three independent ways in the same document:
         *   p.26 tag 3051 reactive limitation, same "10000 <--> 100%", range
         *     (-6000 - +6000) -- which is +/-60%, not +/-6000%;
         *   p.32 tag 3142 MaxLeadingVar%, same notation, "Range: 0---60%;
         *     Default:30%" -- 30% is raw 3000;
         *   p.32 tag 3149 power ramp up rate, "3000 <--> 30%/min".
         * raw_units_per_percent = 100 follows from those. It is still the single
         * most important value to prove with one write-then-read in the lab,
         * because a scale error here is SYMMETRIC: the readback would decode with
         * the same wrong scale and confirm a command that did the wrong thing.
         *
         * READBACK: the same tag 3052 / PDU 3051, read with FC 0x03, which
         * section 5.6 explicitly supports ("The function code is 0x03, 0x06 and
         * 0X10"). Tag 3050 "Power limit actual" (section 5.3, FC 0x04, PDU 3049)
         * is the separate APPLIED value and is a better commissioning check, but
         * it is not the setpoint echo, so the setpoint register is used here.
         *
         * ENABLE REGISTER, and a real gap in this firmware's model: p.27 tag
         * 3070 "Power limitation switch, 0xAA ON, 0x55 OFF(Power to 100%) (for
         * 3052 and 3081 Reg)". Writing 3052 has NO effect while 3070 is 0x55.
         * The profile struct has no field for a prerequisite enable register, so
         * this firmware cannot set it and it must be set once during
         * commissioning (or the struct must gain the field). Flagged, not hidden.
         *
         * RAMP: p.32 tag 3148 "Power ramp rate (Wgra), general", 10000 <--> 100%,
         * range 5%---600%, default 16.67%, noted "Start up ramp rate"; tags 3149
         * / 3150 power ramp up / down rate, "3000 <--> 30%/min", marked "Only for
         * AUS". This firmware ramps in the control engine and writes none of
         * them; reconcile on site so the two limiters do not fight.
         *
         * COMMS-LOSS FAIL-SAFE: none documented for the Modbus control link. Tag
         * 3153 "Internal EPM failsafe switch" (p.33) concerns the EPM export
         * meter, not the third-party control link, and is not used here.
         *
         * IDENTITY: deliberately unset. Section 5.2 gives tag/PDU 35000 "SOLIS
         * inverter type definition, U16", with "1020 --- 3 phase inverter". The
         * document does not settle whether the register holds decimal 1020 or
         * 0x1020: it also says "high 8 bit means protocol version, low 8 bit
         * means inverter model", which only reads correctly as 0x10 / 0x20. One
         * read of that register in the lab resolves it; until then no expected
         * value is asserted rather than a guessed one.
         *
         * OPERATING STATE: tag 3044 / 3072 ("Inverter status" / "Working status",
         * Appendix 2 / Appendix 6) exist, but no state description is configured
         * here -- see the note at the top of this file. Reports UNKNOWN.
         *
         * SETTLE TIME: no response or apply time for a percentage command is
         * documented, so power_limit_settle_ms stays at the firmware default and
         * MUST be measured during commissioning.
         *
         * POLL RATE: a controller choice, not a manual value. 1000/5000 ms is
         * conservative for a 9600-baud RS-485 segment behind a gateway. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Solis RS485_MODBUS Communication Protocol (translated 2021-05-06) "
                            "sections 5.2/5.3/5.6; not qualified on hardware",
        .has_active_power = true,
        .active_power_function = 4,
        .active_power_address = 3004, /* manual tag 3005-3006, section 5.3 offset -1 */
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_U32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 3051, /* manual tag 3052, section 5.6 offset -1 */
        .power_limit_words = 1,
        .raw_units_per_percent = 100.0f, /* 10000 <--> 100% */
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 3051,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.01f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    /*
     * ----------------------------------------------------------------------
     * Growatt. ONE manual, TWO incompatible telemetry maps.
     *
     * Source: "Growatt Inverter Modbus RTU Protocol", document TH-276-00,
     * V1.20, effective 2020-05-12 (PDF GROWATT.pdf).
     *
     * ADDRESSING: 0-based PDU, used directly with no offset. Both register
     * tables start at index 00 ("4.1 Holding Reg ... 00 OnOff", "4.2 Input Reg
     * ... 0. Inverter Status") and p.3 states the ranges as "03 register
     * range:0~124". A range that starts at 0 cannot be a 1-based tag number.
     * The manual's frame examples on p.5-7 are images and carry no extractable
     * address bytes, so there is no worked frame confirming this; it is the one
     * addressing item to prove with the first read in the lab.
     *
     * The COMMAND register is common to both maps: p.9, holding register 03
     * "Active P Rate -- Inverter Max output active power percent", W, value
     * 0-100 or 255, unit %, initial 255, note "255: power is not be limited".
     * Integer percent, so raw_units_per_percent = 1 and the readback tolerance
     * has to allow the 1% quantisation (a request of 47.4% is written as 47).
     * Readback is the same register with FC 0x03 ("Function 3 Read holding
     * register", p.5).
     *
     * What DIFFERS is where output power is reported, and it differs by product
     * family, which is why there are two profiles rather than one:
     *   MAX / MID / MAC (TL3-X), p.3: "04 register range:0~124,125~249".
     *     Output power is input registers 35/36 "Pac H/L", 0.1W (p.34), and the
     *     applied percentage is input register 113 "real Power Percent 0-100 %
     *     MAX" (p.37).
     *   MIN / TL-X / TL-XH, p.3: "04 register range:3000~3124,3125~3249".
     *     Output power is input registers 3023/3024 "Pac H, Pac L Output power
     *     0.1W" (p.50, table headed "Use for TL-X and TL-XH"), and the applied
     *     percentage is input register 3101 "RealOPPercent, Real Output power
     *     Percent, 1%, 1~100" (p.53).
     * A 100 kW commercial unit is a MAX-class TL3-X machine, so that is the
     * profile to expect on site. The lab simulator implements the TL-X map, so
     * the TL-X profile is the one that can be exercised today. WHICH ONE THE
     * SITE HAS IS THE OWNER'S CALL, not a value to interpolate.
     *
     * Word order is documented for both: the registers are named "H" then "L"
     * with H at the lower address, i.e. most significant word first -> AB.
     *
     * TIMING, and this one is a documented constraint rather than a guess, p.8:
     * "Minimum CMD period (RS485 Time out): 850ms. Wait for minimum850ms to send
     * a new CMD after last CMD. Suggestion is 1s". telemetry_poll_ms is 1000 for
     * that reason. Note the firmware issues more than one transaction per poll,
     * so an RS-485 segment may still need the poll period raised.
     *
     * RAMP: p.10 holding 20 "wPowerStartSlope, Power start slope, 1-1000, 0.1%"
     * and 21 "wPowerRestartSlopeEE, Power restart slope". The manual does not
     * give the time base for either, so no rate can be stated. Not written.
     *
     * COMMS-LOSS FAIL-SAFE: p.13 holding 42 "bfailsafeEn; G100 fail safe,
     * Enable:1 Disable:0". G100 is the export-limitation fail-safe, tied to the
     * export meter, not to loss of this controller. No watchdog on the
     * third-party control link is documented. Not used.
     *
     * WRITE LOCK -- UNRESOLVED AND POTENTIALLY BLOCKING: note &*7 on p.61 says
     * "Grid network power control command password: Inverter is in lock state
     * after power on; change the power control by network command should unlock
     * inverter first; default pw is XXXXXX; Unlock: send 0 to 3-135, then send
     * password to 3-136~138; inverter will auto lock in 5min after unlocked".
     * The password is redacted in the manual, the "3-135" notation is not
     * defined anywhere in the document, and register 135 in the holding table is
     * "BLVersion3 ... Reserved" -- so the notation cannot be resolved from this
     * document. Nothing in the extractable text says which registers the note
     * applies to. If it applies to holding 03, writes will be silently rejected
     * (or accepted and auto-relocked after 5 minutes) until the sequence is
     * performed. Growatt must be asked, and this is a hard blocker for Growatt
     * commanding on real equipment. No unlock sequence is attempted here.
     *
     * IDENTITY: unset for both. The identity register is holding 43 "DTC Device
     * Type Code" (p.11) whose table (note &*6, p.60) is per-model and elided
     * with "......" for the commercial types; holding 28/29 "Inverter Module
     * H/L" (note &*5, p.60) is a packed field, not a family constant. No
     * expected value can be stated without the site's model.
     *
     * No settle time and no operating-status code mapping usable by this
     * firmware are documented; both stay unset.
     * ----------------------------------------------------------------------
     */
    {
        .id = "growatt.tl3x.documented",
        /* The inverter is locked to network power control after power-on and
         * must be unlocked first; the manual's unlock password is redacted, and
         * it auto-relocks after 5 minutes. Control would therefore stop
         * silently mid-run even if the unlock were known. Refused. */
        .requires_prerequisite_enable = true,
        .manufacturer = "Growatt",
        .model_family = "MAX / MID / MAC three-phase (TL3-X), input registers 0-249",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* Citations, all from TH-276-00 V1.20; see the block comment above for
         * the full reasoning and for the unresolved write lock.
         *   p.9  holding 03 "Active P Rate", W, "0-100 or 255", % , 255 = not limited
         *   p.5  "Function 3 Read holding register" -> the same register reads back
         *   p.34 input 35 "Pac H Output power (high) 0.1W" / 36 "Pac L"
         *   p.3  "TL3-X(MAX、MID、MAC Type)：04 register range：0~124,125~249"
         *   p.8  "Minimum CMD period (RS485 Time out): 850ms ... Suggestion is 1s" */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Growatt Inverter Modbus RTU Protocol TH-276-00 V1.20 (2020-05-12) "
                            "s4.1 holding 03 / s4.2 input 35-36; not qualified on hardware",
        .has_active_power = true,
        .active_power_function = 4,
        .active_power_address = 35, /* input 35/36 "Pac H/L", 0.1 W */
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_U32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.0001f, /* 0.1 W -> kW */
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 3, /* holding 03 "Active P Rate", integer percent */
        .power_limit_words = 1,
        .raw_units_per_percent = 1.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 3,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 1.0f,
        /* 1% command quantisation: half a step is 0.5, so a tighter tolerance
         * would fault a perfectly accepted fractional setpoint. */
        .readback_tolerance_percent = 0.6f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    {
        .id = "growatt.tlx.documented",
        /* Same power-on write lock and 5-minute auto-relock as the TL3-X entry,
         * with the unlock password redacted in the manual. Refused. */
        .requires_prerequisite_enable = true,
        .manufacturer = "Growatt",
        .model_family = "MIN / TL-X / TL-XH, input registers 3000-3249",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* Same manual (TH-276-00 V1.20), same command register, the TL-X
         * telemetry map. See the block comment above for the full reasoning.
         *   p.9  holding 03 "Active P Rate", W, "0-100 or 255", % , 255 = not limited
         *   p.50 table "Use for TL-X and TL-XH": 3023 "Pac H Output power 0.1W" / 3024 "Pac L"
         *   p.3  "TL-X（MIN Type）：04 register range：3000~3124,3125~3249"
         *   p.8  "Minimum CMD period (RS485 Time out): 850ms ... Suggestion is 1s"
         * This is the map the lab simulator implements. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Growatt Inverter Modbus RTU Protocol TH-276-00 V1.20 (2020-05-12) "
                            "s4.1 holding 03 / input 3023-3024 (TL-X table); "
                            "not qualified on hardware",
        .has_active_power = true,
        .active_power_function = 4,
        .active_power_address = 3023, /* input 3023/3024 "Pac H/L", 0.1 W */
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_U32,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.0001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 3,
        .power_limit_words = 1,
        .raw_units_per_percent = 1.0f,
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 3,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 1.0f,
        .readback_tolerance_percent = 0.6f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    {
        .id = "sungrow.string.documented",
        /* Manual tag 5007 must be set to 0xAA before the setpoint at 5007+1
         * takes effect. Not sequenceable by this firmware, and the setpoint
         * echoes regardless, so a command would falsely confirm. Refused. */
        .requires_prerequisite_enable = true,
        .manufacturer = "Sungrow",
        .model_family = "PV grid-connected string inverter (SG-series)",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* DOCUMENTED, not qualified. Manual transcription, no hardware evidence.
         *
         * Source: Sungrow "Communication Protocol of PV Grid-Connected String
         * Inverters", V1.1.36, 2021-02-07 (PDF "Sungrow .pdf").
         *
         * ADDRESSING: settled explicitly, p.5, section "3. Address type":
         * "Visit all registers by subtracting 1 from the register address.
         * Example: if the address is 5000-5001, visit it using address
         * 4999-5000. Entering '01 04 1387 00 02 + CRC' to check the data of
         * address 5000-5001."  0x1387 == 4999, so PDU = tag - 1, confirmed by a
         * worked frame. Same page: 3X registers are read-only via 0x04, 4X are
         * holding registers via 0x03 with writes by 0x06 / 0x10.
         *
         * ACTIVE POWER: p.7, 3X tag "31 Total active power 5031 - 5032, U32, W".
         * PDU 5030, 2 registers, scale 0.001 for kW.
         * WORD ORDER: BA, and this is documented rather than inferred, p.4:
         * "U32: 32-bit unsigned integer; little-endian for double-word data.
         * Big-endian for byte data. Example: transmission order of U32 data
         * 0x01020304 is 03, 04, 01, 02." Least significant word first.
         * This is the single most important cross-check on this brand -- see
         * docs/BRAND_REGISTER_EVIDENCE.md; the lab simulator emits the opposite
         * order, which would misread 100 kW by three orders of magnitude.
         *
         * COMMAND: p.16, 4X tag "9 Power limitation setting 5008, U16, See
         * Appendix 6, 0.1%", noted "Available when the power limitation switch
         * (5007) is enabled". PDU 5007, FC 0x06, 10 raw units per percent.
         * READBACK: the same tag/PDU read with FC 0x03, which p.5 states is
         * supported for 4X registers.
         *
         * RANGE: Appendix 6 (p.28-29) gives the power-limited range per model in
         * 0.1%: 0-1000, 0-1100, 0-1110 or 0-1250 depending on model. 0-100% is
         * inside every one of them, so it is the safe common subset and is what
         * is set here. Overload scheduling above 100% exists (change log V1.1.35
         * "Add 100% Scheduling to Achieve Active Overload") but is model-specific
         * and deliberately not enabled.
         *
         * ENABLE REGISTER, same gap as Solis: p.16, 4X tag "8 Power limitation
         * switch 5007, 0xAA: Enable; 0x55: Disable" (PDU 5006). The setpoint at
         * 5008 does nothing while this is 0x55, and the profile struct cannot
         * express a prerequisite write, so it must be set at commissioning.
         *
         * RAMP / GRADIENT: none. No ramp, gradient or rate-of-change register
         * appears anywhere in this document.
         * COMMS-LOSS FAIL-SAFE: none documented.
         * SETTLE TIME: not documented.
         *
         * IDENTITY: unset. The register exists -- 3X tag "7 Device type code
         * 5000, U16, See Appendix 6" (p.5, PDU 4999) -- but Appendix 6 assigns a
         * distinct code per model (SG110CX 0x2C06, SG250HX 0x2C0C, SG100CX
         * 0x2C12, ...). There is no family-wide constant, so no expected value
         * can be asserted without knowing the site's model.
         *
         * OPERATING STATE: 3X tag 5038 "Work state ... See Appendix 1" exists;
         * no state description is configured here, per the note at the top of
         * this file. Reports UNKNOWN. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "Sungrow Communication Protocol of PV Grid-Connected String Inverters "
                            "V1.1.36 (2021-02-07) s3.1 tag 5031 / s3.2 tags 5007-5008; "
                            "not qualified on hardware",
        .has_active_power = true,
        .active_power_function = 4,
        .active_power_address = 5030, /* manual tag 5031-5032, minus 1 */
        .active_power_words = 2,
        .active_power_type = INVERTER_VALUE_U32,
        .active_power_word_order = INVERTER_WORD_ORDER_BA, /* documented little-endian words */
        .active_power_scale = 0.001f,
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 5007, /* manual tag 5008, minus 1 */
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f, /* unit 0.1% */
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 5007,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    {
        .id = "chint.cps.sch100_125ktl.documented",
        /* 0x2602 must be set to 1 before 0x1001 has any effect. Not sequenceable
         * by this firmware, and 0x1001 echoes regardless, so a command would
         * falsely confirm. Refused. */
        .requires_prerequisite_enable = true,
        .manufacturer = "Chint Power Systems (CPS)",
        .model_family = "SCH100KTL / SCH125KTL-DO 100(125) kW 1500 V",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* DOCUMENTED, not qualified. Manual transcription, no hardware evidence.
         *
         * Source: "CPS Inverter Model Data Mapping Specification For 403X,
         * Applicable Models 100kW(125kW)_1500V Inverter", V9.03, 2023-01-11.
         * The Chint/ and CPS/ manual folders hold the SAME document: the
         * extracted text of CPS_100_125kW-UL-Modbus-Map-Spec-FW-V12.0.pdf and of
         * Chint/CPS_100_125kW-UL-Modbus-Map-Spec-FW-V120_240817_221331.pdf is
         * byte-identical. There is one CPS/Chint protocol here, not two.
         *
         * ADDRESSING: p.8 states "(5).Basic register address is 0x0000." The
         * hexadecimal addresses in the tables are 0-based PDU addresses and are
         * used directly, with no offset. This is the cleanest of the four brands.
         *
         * IDENTITY: p.10, input register (FC 0x04) 0x0000 "Device, uint16 ...
         * This register value represents the type of device. 0x4035: 100(125)
         * kW_1500V inverter". That is an exact family constant for precisely the
         * machine class this document covers, so it is used as the identity
         * probe. (The model string is also available at 0x000A-0x0013,
         * "e.g. SCH125KTL-DO/US-600".)
         *
         * ACTIVE POWER: p.11, input register 0x001D "Pac, uint16, 0.1kW, AC
         * active power". One register, scale 0.1 for kW -- no word order to get
         * wrong on this brand.
         *
         * COMMAND: p.16, holding register (FC 0x03 / 0x06) 0x1001 "PSet, uint16,
         * 0.1%, min 0, max 1000, Remote electric dispatch Active Power setting
         * value", in the section headed "2. Holding Registers Data Mapping /
         * 1). Power dispatching". PDU 0x1001, 10 raw units per percent, 0-100%.
         * READBACK: the same register with FC 0x03; the section header names
         * 0x03 explicitly and the row is marked RW.
         *
         * NOT DECIDED, and an owner/site item exactly like Huawei 40125 vs
         * 40199: p.38 register 0x2708 "PSetPercentRemote, uint16, 0.1%, 0..1100,
         * Remote electric dispatch Active Power setting value" carries the same
         * description with a 110% ceiling. Two registers, same words, different
         * range. 0x1001 is used here because it sits in the dedicated power
         * dispatching block and its documented maximum is exactly 100%. Confirm
         * which one the firmware version on site honours before promoting.
         *
         * ENABLE REGISTER, same gap as Solis and Sungrow: p.32 register 0x2602
         * "CtrModeActivePw ... The control mode of active power. 0: Disable
         * dispatch mode. 1: Remote dispatch mode. 2: Local control." Remote
         * dispatch has to be selected (1) or the setpoint is ignored. Not
         * writable by this firmware; a commissioning step. Note also 0x250E
         * "Percentage ... Local electric dispatch Active Power setting value"
         * (p.28) is the LOCAL path and must not be confused with 0x1001.
         *
         * RAMP / GRADIENT: no dispatch ramp-rate register is documented. The
         * nearest items are p.27 0x2505 "NormSoftStartT, 1s, Normal time in soft
         * startup", 0x2504 "NormSoftStopT" and 0x2506 "NormDeratingStep, 0.01%,
         * Normal power derating step" -- a step size and start/stop times, not a
         * rate limit on a dispatch setpoint. None are written.
         * COMMS-LOSS FAIL-SAFE: none documented.
         * SETTLE TIME: not documented.
         *
         * OPERATING STATE: 0x002F and the 0x8400 bitfields exist; no state
         * description is configured here, per the note at the top of this file. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "CPS Inverter Model Data Mapping Specification For 403X V9.03 "
                            "(2023-01-11) input 0x0000/0x001D and holding 0x1001; "
                            "not qualified on hardware",
        .has_identity_probe = true,
        .identity_function = 4,
        .identity_address = 0x0000,
        .identity_words = 1,
        .identity_expected = 0x4035, /* "100(125) kW_1500V inverter" */
        .identity_mask = 0xFFFF,
        .has_active_power = true,
        .active_power_function = 4,
        .active_power_address = 0x001D,
        .active_power_words = 1,
        .active_power_type = INVERTER_VALUE_U16,
        .active_power_word_order = INVERTER_WORD_ORDER_AB,
        .active_power_scale = 0.1f, /* 0.1 kW */
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 0x1001,
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f, /* unit 0.1% */
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 0x1001,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    {
        .id = "foxess.commercial.pending",
        .manufacturer = "FoxESS",
        .model_family = "FOX commercial inverter (native Modbus map, not the R-Series SunSpec map)",
        .protocol = "Modbus RTU / Modbus TCP",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_TCP,
        /* DOCUMENTED, not qualified. Transcribed from the manufacturer manual;
         * nothing below has been exercised against physical hardware or against
         * the lab simulator.
         *
         * Source: FoxESS Co. Ltd, "FOX commercial inverter Modbus interface
         * definition description", document version V1.05.03.00, release date
         * 2025-01-15 (PDF FoxESS-Modbus-Protocol-V1.05.03.00.pdf). Title page
         * p.1. NOTE the internal running header of this PDF still reads
         * "Modbus definition (V1.02.00.00)" on every page; the title page and
         * change record are the authority for the version.
         *
         * THE BRAND CORRECTION THAT MATTERS: this profile previously carried the
         * manufacturer string "FoxESS / Knox". Those are two different companies.
         * Knox equipment sold in this market is rebadged AISWEI Technology ASW
         * hardware and uses a COMPLETELY different register map -- its percentage
         * setpoint is at a different address with a different scale (see the
         * separate knox.aiswei.asw.documented profile). Selecting one brand and
         * getting the other brand's addresses is precisely the failure this
         * catalogue exists to prevent, so the two are now separate entries.
         *
         * ADDRESSING -- and this is the one item this manual does NOT nail down.
         * The register tables have a bare "Address" column (e.g. section 3,
         * table 3-11 row 335 gives 49007) and the document never states an
         * offset or prints a byte-level frame for a data register. Two things in
         * the document argue that the column is already the 0-based PDU address:
         *   - Section 5.3.2, p.44, exception 0x02: "For a controller with 100
         *     registers, the first PDU address is 0, and the last one is 99."
         *     The document reasons in PDU terms throughout.
         *   - The first row of the map, section 3 row 1, is "Model name ... 30000"
         *     -- address 30000, not 30001. A 1-based 3x/4x reference convention
         *     cannot produce 30000; it starts at 30001.
         * That is a deduction from the document, NOT a quoted rule and NOT a
         * worked frame. It is the single most important thing to prove with the
         * first read, and it is why nothing here may be commanded on real
         * equipment yet.
         *
         * COMMAND: section 3, table row 335, p.30: "[Power grid dispatch] Active
         * power percentage derating (0.1%)", RW, I16, Unit %, Gain 10,
         * Address 49007, Number 1, "Range:[0, 100.0]", remark "Active power fine
         * adjustment interface". FC 0x06 (section 5.3.4).
         * SCALE: raw_units_per_percent = 10, stated twice independently in the
         * document -- the signal name itself says "(0.1%)", and Gain 10 with a
         * range of [0, 100.0] means raw 1000 is 100.0%. Not interpolated.
         *
         * READBACK: the same address 49007 read with FC 0x03, which section 5.3.1
         * lists as "Read register -- Supports single and multiple register
         * sequential reads". The register is RW, so it is readable by the
         * document's own access definition. Note this is a setpoint echo, not a
         * measurement of the applied limit; no separate applied-limit register is
         * documented in this map.
         *
         * ACTIVE POWER: deliberately UNSET, and this is a manual gap rather than
         * a choice. Section 3 row 158, p.16 gives "Active power, RO, I32, kW,
         * Gain 1000, Address 39134, Number 2" -- address, type and scale are all
         * stated. What is NOT stated anywhere in this document is the WORD ORDER
         * of a 32-bit value. There is no endianness statement, no High/Low word
         * naming, and no worked multi-register example. A reversed word order
         * turns 100 kW into a nonsense number, so no order is asserted here. One
         * read of 39134 against a known output resolves it, and active power can
         * then be added. The command path is unaffected: 49007 is a single
         * 16-bit register.
         *
         * IDENTITY: unset. Section 3 row 1 gives "Model name, RO, STR, Address
         * 30000, Number 16" -- a 16-register ASCII string, not a numeric family
         * constant, so there is no expected value this firmware's identity probe
         * (a masked integer compare) can assert.
         *
         * PREREQUISITE ENABLE: none is documented for 49007. The "[Power grid
         * dispatch]" group at 49005-49010 has no enable or unlock register in the
         * map, unlike the separate "Remote Control" group whose bitfield at
         * address 46001 does carry "Bit0: Remote Control enable" (row 271, p.23)
         * and which commands in WATTS at 46003, not in percent.
         * There IS a residual risk worth naming: section 5.3.2 p.44 defines
         * exception code 0x80 "No permission -- Authentication fails or
         * permissions expire after timeout, and operation is prohibited". The
         * document never says what authentication that is, nor which registers it
         * guards. Crucially, if it applies, the write is REJECTED WITH AN
         * EXCEPTION -- it fails loudly, which this firmware surfaces as a write
         * error. It does not produce the silent accept-and-echo failure that
         * requires_prerequisite_enable exists to refuse, so that flag is not set.
         * If a lab write returns 0x80, FoxESS must be asked what unlocks it.
         *
         * RAMP / GRADIENT: no active-power ramp or gradient register is
         * documented in this map. 49008 is "[Power grid dispatch] Fixed Active
         * power derated (W)" -- an absolute-watt setpoint, not a rate -- and
         * 49136 "Grid point power limit" is likewise a watt limit. Nothing is
         * written here.
         *
         * COMMS-LOSS FAIL-SAFE: none for the percentage path. Address 46002
         * "Remote Timeout_Set" (U16, s) with 46007 "Remote Timeout Countdown"
         * (RO, s) belongs to the Remote Control watt group, not to the percentage
         * dispatch group, and is not used here.
         *
         * SETTLE TIME and any minimum command interval: not documented anywhere
         * in this manual, so neither is asserted. Both must be measured at
         * commissioning.
         *
         * POLL RATE: a controller choice, not a manual value. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "FoxESS FOX commercial inverter Modbus interface definition "
                            "V1.05.03.00 (2025-01-15) section 3 row 335 and section 5.3; "
                            "not qualified on hardware",
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 49007, /* table row 335; PDU as printed -- see the note above */
        .power_limit_words = 1,
        .raw_units_per_percent = 10.0f, /* signal name "(0.1%)", Gain 10, range [0, 100.0] */
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 49007,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_S16, /* manual Type: I16 */
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.1f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
    {
        .id = "knox.aiswei.asw.documented",
        /* Manual address 44001 "Active power control function: 0 = Disable,
         * 1 = Enable" must be Enable before the setpoint at 45403 does anything.
         * This firmware cannot sequence that write, and 45403 is an RW holding
         * register that will echo the value back regardless, so commanding it
         * would report CONFIRMED for a limit the inverter ignores. Refused until
         * the prerequisite can be written and verified. */
        .requires_prerequisite_enable = true,
        .manufacturer = "AISWEI (Knox / Solplanet ASW)",
        .model_family = "ASW three-phase string inverter (ASW xxK-LT G2 class)",
        .protocol = "Modbus RTU",
        .connection = INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY,
        /* DOCUMENTED, not qualified. Transcribed from the manufacturer manual;
         * nothing below has been exercised against physical hardware or against
         * the lab simulator.
         *
         * Source: AISWEI Technology Co. Ltd, "Technical Information -- AISWEI
         * Interface (Based On Modbus Standard Protocol)", document
         * MB001_ASW GEN-Modbus-en_V2.1.5, (c) 2022 (PDF
         * MB001_ASW GEN-Modbus-en_V2.1.5(2).pdf).
         *
         * WHY THIS IS A SEPARATE PROFILE FROM FoxESS: "Knox" is a rebadge of
         * AISWEI ASW hardware -- the register map is AISWEI's, the manufacturer's
         * name register 31057 returns "AISWEI", and it shares nothing with the
         * FoxESS map. The catalogue previously carried a single "FoxESS / Knox"
         * entry, which would have offered one brand's addresses under the other
         * brand's name. Both are now transcribed separately from their own
         * manuals.
         *
         * ADDRESSING -- STATED AS A RULE AND WITH A WORKED CONVERSION, section
         * 3.1 "Information on the Assignment Tables", p.3:
         *   "ADR (DEC)  Decimal Modbus address, you need to remove 3x or 4x and
         *    subtract 1, then convert to hexadecimal and use it in the
         *    communication frame. Such as 31001 (decimal) -> 1000 (decimal) ->
         *    0x03e8 (hexadecimal)"
         * So PDU = (printed address with its leading 3 or 4 stripped) - 1. The
         * worked example is unambiguous: 31001 -> 1001 -> 1000 -> 0x03E8. Every
         * address below is the PDU value, with the manual's printed address named
         * beside it. 3xxxx are input registers, 4xxxx are holding registers.
         *
         * FUNCTION CODES, section 3.6 p.35-39: 0x03 Read Holding Register, 0x04
         * Read Input Register, 0x06 Write Single Holding Register, 0x10 Write
         * Multiple Holding Registers. All four are implemented by this firmware's
         * transport.
         *
         * COMMAND: section 3.3 register overview, p.20: printed address "45403
         * Active Power Set, U16, %Pn, Gain 0.01, RW". PDU = 5403 - 1 = 5402,
         * FC 0x06.
         * SCALE: section 3.1 defines Gain as "Real value = Gain * output value",
         * so real percent = 0.01 * raw and raw = percent * 100 ->
         * raw_units_per_percent = 100. This is the value to prove with one
         * write-then-read, because a scale error here is SYMMETRIC: the readback
         * decodes with the same wrong scale and would confirm a command that did
         * the wrong thing.
         *
         * READBACK: the same printed address 45403 / PDU 5402 read with FC 0x03.
         * It is a holding register with RW access ("RW: Read and Write",
         * section 3.1), so it is readable by the document's own definition. This
         * is a setpoint echo. Note that printed 31390 "Power limitation
         * master-slave status (0 = Not available (Power limitation disable),
         * 1 = Master (Power limitation enable), 2 = Slave (Power limitation
         * enable))" p.9 is a better commissioning check of whether limiting is
         * actually active, but it is not the setpoint echo and is not used here.
         *
         * ACTIVE POWER: deliberately UNSET, and this is a manual gap. Section 3.3
         * p.9 gives "31371~31372 Active power, S32, W, Gain 1.0, RO" -- printed
         * address, type and scale are all stated, PDU would be 1370 with FC 0x04.
         * What this manual never states is the WORD ORDER of a 32-bit value.
         * Section 3.2 "AISWEI Data Types and NaN Values" p.4 lists S32/U32 widths
         * and NaN values but says nothing about word order, the only ordering
         * statement in the document concerns the two ASCII bytes inside a String
         * register ("the high 8-bit is the first ASCII character"), and there is
         * no worked multi-register example. A reversed word order turns 100 kW
         * into a nonsense number, so no order is asserted. One read of 1370
         * against a known output resolves it and active power can then be added.
         * The command path is unaffected: PDU 5402 is a single 16-bit register.
         *
         * PREREQUISITE ENABLE -- the reason this profile cannot be commanded:
         * p.16 printed address "44001 Active power control function: 0 = Disable,
         * 1 = Enable, E16, RW". The setpoint at 45403 is subordinate to it.
         * See the requires_prerequisite_enable note at the top of this entry.
         *
         * RAMP: documented but not written. p.20 printed "45404 Increase rate of
         * active power, U16, %Pn/min, Gain 0.01, RW" and "45405 Decrease rate of
         * active power, U16, %Pn/min, Gain 0.01, RW"; also "45401 Load rate of
         * first connection to grid" and "45402 Load rate of re-connection to
         * grid", both U16 %Pn/min Gain 1.0. This firmware ramps in the control
         * engine; reconcile on site so the two limiters do not fight.
         *
         * COMMS-LOSS FAIL-SAFE: none documented for a third-party control link.
         * The change record for V2.1.5 (p.2) adds printed 44029 "the
         * enable/disable of communication checking for G100" and 33059 the
         * matching read-back of that enable. G100 is the UK export-limitation
         * scheme, tied to the export meter, not to loss of this controller, and
         * the register overview gives no timeout or fallback-limit value for it.
         * Not used.
         *
         * IDENTITY: unset. p.5-6 gives several candidates, none of which is a
         * numeric family constant an integer identity probe can assert:
         * printed 31001 "Device Type: 1=Single phase / 3=Three phase" is typed
         * String in this manual, not U16; 31019~31026 "Machine type" and
         * 31065~31072 "Brand name" are Strings; 31073 "Inverter model" is an E16
         * enumeration whose commercial code is "5-PV Three phase50-60kW", which
         * does not cover a 100 kW machine at all -- another reason the site's
         * actual model must be confirmed rather than assumed.
         *
         * SETTLE TIME and minimum command interval: neither is documented
         * anywhere in this manual, so neither is asserted.
         *
         * POLL RATE: a controller choice, not a manual value. 1000/5000 ms is
         * conservative for an RS-485 segment behind a gateway. */
        .qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED,
        .manual_reference = "AISWEI MB001_ASW GEN-Modbus-en_V2.1.5 sections 3.1/3.3/3.6; "
                            "not qualified on hardware",
        .has_power_limit = true,
        .power_limit_function = 6,
        .power_limit_address = 5402, /* printed 45403; section 3.1 rule: 5403 - 1 */
        .power_limit_words = 1,
        .raw_units_per_percent = 100.0f, /* Gain 0.01, real = 0.01 * raw */
        .minimum_percent = 0.0f,
        .maximum_percent = 100.0f,
        .has_power_limit_readback = true,
        .power_limit_readback_function = 3,
        .power_limit_readback_address = 5402,
        .power_limit_readback_words = 1,
        .power_limit_readback_type = INVERTER_VALUE_U16,
        .power_limit_readback_word_order = INVERTER_WORD_ORDER_AB,
        .power_limit_readback_scale = 0.01f,
        .readback_tolerance_percent = 0.2f,
        .telemetry_poll_ms = 1000,
        .telemetry_stale_timeout_ms = 5000,
    },
};

size_t inverter_profiles_count(void)
{
    return sizeof(PROFILES) / sizeof(PROFILES[0]);
}

const inverter_profile_t *inverter_profiles_get(size_t index)
{
    return index < inverter_profiles_count() ? &PROFILES[index] : NULL;
}

const inverter_profile_t *inverter_profiles_find(const char *id)
{
    if (!id || !id[0]) return NULL;
    if (strcmp(id, LEGACY_SAFE_DEFAULT_PROFILE_ID) == 0) id = SAFE_DEFAULT_PROFILE_ID;
    for (size_t index = 0; index < inverter_profiles_count(); ++index) {
        if (strcmp(PROFILES[index].id, id) == 0) return &PROFILES[index];
    }
    return NULL;
}

bool inverter_profile_allows_read(const inverter_profile_t *profile)
{
    if (!profile) return false;
    if (profile->simulator_only) {
        return profile->qualification >= INVERTER_PROFILE_QUALIFICATION_SIMULATOR_VERIFIED;
    }
    return profile->qualification >= INVERTER_PROFILE_QUALIFICATION_READ_ONLY_QUALIFIED;
}

bool inverter_profile_allows_write(const inverter_profile_t *profile)
{
    /* A device whose setpoint needs a prerequisite enable write this firmware
     * cannot perform is refused here too, not only in the permission gate. Other
     * callers read this predicate directly, and every one of them must get the
     * same answer: commanding such a device produces a CONFIRMED verdict for a
     * limit the inverter is ignoring. */
    return profile && !profile->simulator_only && !profile->requires_prerequisite_enable &&
           profile->has_power_limit && profile->has_power_limit_readback &&
           profile->qualification == INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED;
}

inverter_write_permission_t inverter_profile_write_permission(const inverter_profile_t *profile,
                                                              bool declared_lab_target)
{
    if (!profile) return INVERTER_WRITE_FORBIDDEN;
    /* Confirmability is not negotiable in either mode: without a readback there
     * is no way to tell a command that landed from one that was ignored. */
    if (!profile->has_power_limit || !profile->has_power_limit_readback) {
        return INVERTER_WRITE_FORBIDDEN;
    }
    /* A device needing a prerequisite enable write is refused in BOTH modes. Its
     * setpoint register echoes the value back regardless, so commanding it would
     * produce a CONFIRMED verdict for a limit the inverter is ignoring. Being
     * unable to command is recoverable; being told a plant is limited when it is
     * not is not. See requires_prerequisite_enable in the header. */
    if (profile->requires_prerequisite_enable) return INVERTER_WRITE_FORBIDDEN;
    if (inverter_profile_allows_write(profile)) return INVERTER_WRITE_PRODUCTION;
    if (declared_lab_target) return INVERTER_WRITE_LAB_ONLY;
    return INVERTER_WRITE_FORBIDDEN;
}

const char *inverter_write_permission_label(inverter_write_permission_t permission)
{
    switch (permission) {
        case INVERTER_WRITE_PRODUCTION: return "production";
        case INVERTER_WRITE_LAB_ONLY: return "lab_simulator_only";
        case INVERTER_WRITE_FORBIDDEN: default: return "forbidden";
    }
}

const char *inverter_profile_qualification_label(inverter_profile_qualification_t qualification)
{
    static const char *const LABELS[] = {
        "Documented",
        "Simulator verified",
        "Bench verified",
        "Read-only qualified",
        "Write qualified",
        "Production approved",
    };
    return qualification <= INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED
        ? LABELS[qualification]
        : "Unknown";
}

bool inverter_profile_has_status_register(const inverter_profile_t *profile)
{
    return profile && inverter_status_register_is_configured(&profile->status_register);
}

const char *inverter_profile_connection_label(inverter_profile_connection_t connection)
{
    switch (connection) {
        case INVERTER_PROFILE_CONNECTION_MODBUS_TCP: return "Direct Modbus TCP";
        case INVERTER_PROFILE_CONNECTION_MODBUS_RTU_GATEWAY: return "Modbus RTU gateway";
        case INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY: return "Manufacturer logger/gateway";
        default: return "Unknown";
    }
}
