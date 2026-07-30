# Huawei SUN2000 register evidence

**Source:** `Huawei Inverter Modbus Interface Definitions (V3.0)`, Issue 01 (2023-01-17)
**Cross-checked against:** SolTrix lab simulator, `inverter-simulator/src/profiles/huawei-v3.js`, measured live
**Date:** 2026-07-30

This records what the manufacturer manual actually says, what the lab simulator
actually does, and where they differ. It exists so the profile in
`components/inverter_manager/inverter_profiles.c` is attributable line by line
rather than trusted.

Nothing here has been exercised against a physical SUN2000. The profile remains
`DOCUMENTED` and the production write gate still refuses it.

---

## 1. Addressing convention — settled

This project has been bitten repeatedly by 1-based manual tags versus 0-based
Modbus PDU addresses. The manual settles it: it describes a command sent to
**"register address: 40200/0X9D08"**, and `0x9D08 == 40200`. The decimal
addresses in the manual are therefore the raw on-the-wire register addresses,
used directly with **no offset**.

The simulator's own README says the same thing from the other side: FUXA tag
addresses are `protocol register + 1`, and direct Modbus clients use the protocol
address.

## 2. Signals

| Signal | Manual ref | FC | PDU address | Words | Type | Unit | Gain | Firmware scale |
|---|---|---:|---:|---:|---|---|---:|---:|
| Model (nameplate) | reg 1 | 03 | 30000 | 15 | STR | — | — | identity probe reads word 0 |
| Active power | signal 171 | 03 | 32080 | 2 | I32 | kW | 1000 | `0.001` |
| Device Status | signal 178 | 03 | 32089 | 1 | E16 | — | — | **not used, see §4** |
| Active Power Percentage **Derating** [Low Precision] | signal 409 | 06 | 40125 | 1 | I16 | % | 10 | `raw = % x 10` |
| Active power fixed value derating | signal 410 | 06 | 40126 | 2 | U32 | W | 1 | not used |
| Active Power Percentage **Control** [Low Precision] | signal 419 | 06 | 40199 | 1 | I16 | % | 10 | not used — see §3 |
| Active power gradient | signal 432 | 06 | 42017 | 2 | U32 | %/s | 1000 | not written — see §5 |

Manual and simulator **agree** on every address, type, gain and scale above. The
simulator is a faithful model of this layout, which is why it is usable as a lab
target at all.

Measured confirmations against the live simulator:
- `30000` → `SUN2000-SIM`; first word `0x5355` = `"SU"`, the start of every
  SUN2000 nameplate. Used as the identity probe.
- `32080` → raw `85102` with the high word first → 85.102 kW. Confirms I32 watts,
  scale `0.001`, word order AB.
- `40125` → `1000` = 100.0 %. Confirms percent x10.

## 3. Open question: 40125 or 40199?

Both encode percent x10 and both are RW I16. The manual distinguishes them by
**purpose**:

- **40125**, signal 409, "Active Power Percentage Derating [Low Precision]",
  described as the *"Active fine adjustment interface"*.
- **40199**, signal 419, "Active Power Percentage Control [Low Precision]",
  described as *"the active power percentage control interface is used in
  distributed mode. The interface is sent to the power software in
  **anti-backcurrent control** to control the upper limit of the output active
  power when the power is increased in underfrequency."*

**Anti-backcurrent control is exactly this product's application** — holding PV
output below the point where power flows back into a generator or the grid. On
the manual's wording alone, 40199 may be the more correct control register.

The firmware currently commands **40125**, because it is the conventional
third-party derating interface. This is a **site-verification item**, not a
settled decision.

One suggestive but non-authoritative data point: the simulator applies a 40125
write about 1500 ms later, while 40199 applies immediately — consistent with
40199 being the interface intended for closed-loop control rates. A behaviour in
a model is a hint, not evidence about hardware.

**Action on site:** command both, observe which the machine honours, how fast,
and whether either is rejected. Then fix the choice with evidence.

## 4. Device status is deliberately unused

The manual defines signal 178 "Device Status" at 32089 as an E16 enumeration, but
defers the code table to a separate *"Inverter Key Signal Extension
Description"* which is **not among the manuals available**. The simulator emits
`0x0200` (512), and the meaning of that code is therefore **not documented in the
manuals available**.

The firmware consequently configures no operating-state description for any
profile and every inverter reports `INVERTER_STATE_UNKNOWN`. This is the
project's standing policy — a guessed status mapping is worse than an honest
unknown — and it also means `fleet_synchronised()` cannot yet be wired to real
status. Obtaining that extension document is the blocker.

## 5. The inverter has its own ramp limiter

Signal 432, "active power gradient", RW U32 at 42017, unit **%/s**, gain 1000,
*"Limiting the speed of power change caused by power..."*.

This firmware ramps in the control engine and does **not** write 42017. Two
independent rate limiters in series will interact: the slower one dominates, and
a controller ramp faster than the inverter's own gradient will simply not be
achieved, which would look like a tracking failure rather than a configuration
mismatch.

**Action on site:** read 42017 and reconcile it with the configured control-engine
ramp, or decide deliberately that the controller owns the rate and set the
gradient accordingly.

## 6. Timing is not specified

**No settle or response time for a percentage command is documented anywhere in
this manual.** The only timeouts it states are for logger-level operations
(starting an upgrade, uploading data), not for a setpoint reaching its readback
register.

Consequences:
- The firmware's default `INVERTER_CONFIRMATION_SETTLE_MS` (500 ms) and
  `DEADLINE_MS` (5000 ms) remain firmware-side acquisition windows, not
  manufacturer values. They must be measured per site.
- The lab profile declares `power_limit_settle_ms = 2500` because the simulator
  was **measured** deferring by ~1500 ms. The manufacturer profile leaves it at
  the default, because there is no evidence to set it from.
- This is not academic: with the old global 500 ms window, a device deferring
  1500 ms produced `MISMATCHED` for a perfectly accepted command, which latches a
  confirmation fault, removes the inverter from commandable capacity and drives it
  to zero. Measuring this on site is a commissioning step, not a nicety.

## 7. Still unresolvable without the physical machine

1. Whether 40125 or 40199 is the correct control register for a commercial
   SUN2000 in this application (§3).
2. Whether the readback at 40125 reports the **active** or the **requested**
   value on real hardware. The simulator reports the active value; the manual does
   not say.
3. How long the machine takes to apply a percentage command (§6).
4. The Device Status code table (§4).
5. Whether any mode or enable register must be written before a percentage limit
   takes effect. Nothing in the manual states one, but absence of a statement is
   not proof of absence.
6. Whether the SmartLogger sits in the path. The profile's connection type is
   `LOGGER_GATEWAY`, and `SmartLogger ModBus Interface Definitions.pdf` plus
   `SmartLogger_3000A_manual` are available but not yet analysed. A logger can
   re-map unit ids and addresses, which would change everything above.

## 8. Other manuals available, not yet analysed

`Manuals/Inverter/` also holds Solis, Growatt, Sungrow, SolarEdge, SMA, Solax,
CPS/Chint, Knox, SAJ and Fronius documents, and the simulator implements Solis,
Growatt, Sungrow and Chint/CPS profiles. The same extraction and cross-check
should be done per brand before any of them is offered as more than
`DOCUMENTED`.
