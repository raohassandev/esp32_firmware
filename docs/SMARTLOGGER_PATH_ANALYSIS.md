# Huawei SmartLogger path analysis

**Status:** research only. Nothing in this document has been exercised against a
physical SmartLogger or a physical SUN2000. No firmware was changed to produce it.

**Sources, cited throughout by short name:**

| Short name | Document | Issue / date |
|---|---|---|
| **SL-MB** | `SmartLogger ModBus Interface Definitions.pdf` | Issue 35, 2020-02-20 |
| **SL3000** | `SmartLogger_3000A_manual_240727_191232.pdf` (title page: *SmartLogger3000 User Manual*) | Issue 03, 2020-01-10 |
| **INV-V3** | `Huawei Inverter Modbus Interface Definitions (V3.0).pdf` | Issue 01, 2023-01-17 |

Page numbers are given as **PDF page** (as extracted, 1-based over the file) and,
where the printed footer differs, the document's own page number in parentheses.

Applicability caveat, stated up front: **SL-MB says "Applicable model: SmartLogger"**
and lists firmware versions `SmartLogger V100R001C00 / V100R002C00 / V200R001C00 /
V300R001C00 or later` and `SUN2000 V100R001C95 or later` (SL-MB, PDF p.10, §1.2
System Requirements). It does **not** name the SmartLogger3000A specifically. SL3000
is the 3000-series user manual and covers the 3000A/3000B hardware IDs (SL3000,
PDF p.15 (7), Table 2-1: hardware ID `A` = no fiber, max 80 solar inverters;
`B` = fiber, max 150). Where the two disagree, that is called out. Where only
SL-MB states a register, it is a *SmartLogger-generic* claim, and whether the
3000A firmware on site implements it is a **site-verification item**.

---

## 0. Addressing convention — checked, not assumed

SL-MB uses **decimal addresses that are the raw on-the-wire register addresses**,
identical to the convention INV-V3 uses. It settles this twice, by example:

- "A master node sends a request to a slave node (logic device ID: 01) to query
  register whose address is **32306/0X7E32**" — and the data frame shown is
  `00 01 00 00 00 06 00 03 7E 32 00 02`. `0x7E32 == 32306`.
  (SL-MB, PDF p.42 (34), §4.3.3.4)
- "A master node sends a Power-On instruction (**register address: 40200/0X9D08**)"
  — frame `... 00 06 9D 08 00 00`. `0x9D08 == 40200`.
  (SL-MB, PDF p.43 (35), §4.3.4.4)
- "set the active power control mode (**register address: 40118/0X9CB6**) to 2, and
  set the active power deration (**register address: 40119/0X9CB7**) to 50%" —
  frame `... 00 10 9C B6 00 02 04 00 02 00 32`. (SL-MB, PDF p.45 (37), §4.3.5.4)

**Conclusion: no offset. Same convention as INV-V3. Use the decimal numbers directly.**

Note the third example: it is an *inverter* register pair (40118/40119 are SUN2000
registers, not SmartLogger registers) being written with the logger's own frame
format — see §1.

Separately, SL-MB's exception-code text describes standard Modbus PDU semantics
("For a controller with 100 registers, the PDU addresses the first register as 0")
(SL-MB, PDF p.39 (31), §4.3.2, code 0x02). That is generic boilerplate about
quantity/range checking and does **not** contradict the worked examples above.

---

## 1. Addressing: how a client reaches an inverter through the logger

**Answer: by unit id (Modbus "logic device ID") in the MBAP header, and inverter
registers are at the SAME addresses as a direct connection. The logger does not
substitute its own address space for pass-through access — but it *also* offers a
separate remapped space and its own plant-level space.**

Quotes:

- "ModBus-TCP data frames identify devices by logic device IDs." Allocation table:
  **SmartLogger Local Address `0`; Access Device Address `1–247`; Reserved `248–255`.**
  "The address of an access device is an RS485 address which can be read on the LCD
  or built-in WebUI of the SmartLogger."
  (SL-MB, PDF pp.35–36 (27–28), §4.2.1)
- MBAP field "Logic device ID … Identifies a SmartLogger device or a subdevice
  accessed by the SmartLogger. **0: SmartLogger. 1–247: Inverters or other device**."
  (SL-MB, PDF p.37 (29), Table 4-1)
- §2.5 *Register Definitions for the SUN2000*: "The operating object of the register
  is an SUN2000 inverter. In the Modbus-TCP communications protocol, **the logic
  device ID is set to the RS485 address of the inverter**. For the detailed register
  definitions, see the SUN2000VXXXRXXXCXX MODBUS Protocol."
  (SL-MB, PDF p.28 (20), §2.5)
- §3 opening line: "**The MODBUS-TCP interface provided by the SmartLogger can
  directly access the inverter.** The built-in power interface of the SmartLogger
  can be used for array-level power adjustment. If the power interface is used, the
  power adjustment instruction is first processed by the SmartLogger and then
  forwarded to the inverter." (SL-MB, PDF p.32 (24), §3)
- §2.1 header: "the operation object of the register is the SmartLogger or all
  inverters accessed by the SmartLogger. In the Modbus-TCP communications protocol,
  **the logic device ID is fixed to 0**." (SL-MB, PDF p.11 (3), §2.1)

So there are **three distinct address spaces on one TCP socket to the logger**:

| Space | Unit id | Addresses | Contents | Citation |
|---|---|---|---|---|
| Logger's own | `0` (configurable, see below) | 40000–50002 SmartLogger registers | plant totals, plant setpoints, logger config | SL-MB PDF pp.11–20 (3–12), Table 2-1 |
| Pass-through to a device | `1–247` = that device's RS485 address | the **inverter's own** map, per INV-V3 (30000, 32080, 40125, 40199, 42017 …) | per-inverter signals and setpoints | SL-MB PDF p.28 (20), §2.5 |
| **Remapped** block | the **SmartLogger** address | `51000 + 25*(DeviceAddr-1) + offset` | 25 RO words per device | SL-MB PDF p.29 (21), §2.7 |
| Public per-device | `1–247` | 65521–65534 | port no., device address, name, connection status | SL-MB PDF p.28 (20), Table 2-6 |

The remapped space is explicitly **read-only and does not contain any power-limit
register**. Its offsets are: 0 active power (I32 kW gain 1000), 2 reactive power,
4 total DC input current, 5 total input power, 7 insulation resistance, 8 power
factor, 9 inverter status, 11 cabinet temperature, 12/14/16 fault and warning
codes, remainder spare (SL-MB, PDF pp.29–30 (21–22), Table 2-7). "The mapped
registers are accessed by the SmartLogger address. By default, each device takes
up 25 registers … Supported devices: inverter, environmental monitor instrument."
Two SmartLogger-added status values are defined there: `0xB000` communication
interrupt, `0xC000` uploading (SL-MB, PDF p.29 (21), Table 2-7 row 7).

**The logger's own unit id is configurable and can collide.** SL3000 exposes
`Settings > Modbus TCP > SmartLogger address` ("Set this parameter to the
communication address of the SmartLogger") and `Address mode` = `Comm. Address`
or `Logical address` — "If the communications address of the device connected to
the SmartLogger is unique, you are advised to select **Comm. Address**. In other
cases, you must select **Logical address**." (SL3000, PDF p.87 (79), Figure 6-11
parameter table). SL-MB's alarm 1105 *Device Address Conflict* confirms the
consequence: "The SmartLogger address configured for data forwarding using
Modbus-TCP conflicts with the address of a connected device. **The SmartLogger
forwarding address is 0 by default.** If the configured address conflicts with the
access device address, data of the access device fails to be forwarded using
Modbus-TCP." (SL-MB, PDF p.23 (15), Table 2-3). SL3000's alarm reference repeats
it and tells you to change either address — "The SmartLogger RS485 address
conflicts with the physical address (RS485 address) or logical address for the
connected southbound device" (SL3000, PDF p.152 (144), alarm 1105 *Device Address
Conflict*).

**Consequence for this firmware:** unit id 0 is *not* guaranteed to be the logger,
and unit ids are *not* guaranteed to be raw RS485 addresses when `Address mode`
is `Logical address`. What `Logical address` numbering actually is — its base, its
ordering rule, whether it is stable across a device re-scan — is **not documented
in the manuals available**. That is a first-order site-verification item, because
under `Logical address` a hard-coded unit id could silently address a *different*
inverter after a rescan.

---

## 2. Writes through the logger to per-inverter setpoints

**Answer: yes, writes pass through; the manual explicitly says the logger "can
directly access the inverter", and its own worked FC10 example writes SUN2000
registers 40118/40119. But there are documented gates and a documented
acknowledge-before-apply hazard.**

Pass-through writes are supported at the protocol level: function codes are
`0x03` read, `0x06` write single, `0x10` write multiple, `0x2B` read device
identifiers (SL-MB, PDF p.38 (30), Table 4-2) — identical to the inverter's own
list, which omits 0x2B (INV-V3, PDF p.146 (141), Table 6-3). And SL-MB's own
FC10 example writes inverter registers 40118 (active power control mode = 2) and
40119 (active power deration = 50 %) in a single frame (SL-MB, PDF p.45 (37)).
Note that example writes a **mode register together with the value** — INV-V3's
40125/40199 percentage registers are not the same pair, and SL-MB does not say
that writing 40125 alone is sufficient.

### Gates that must be enabled first

1. **Per-inverter**: `Remote power schedule` — "If this parameter is set to
   **Enable**, the inverter responds to the scheduling instruction from the remote
   port. If this parameter is set to **Disable**, the inverter does not respond to
   the scheduling instruction from the remote port." (SL3000, PDF p.102 (94),
   *Power Adjustment Parameters*). SL3000's own procedure makes this Step 1 of
   setting active power control: "Choose Monitoring > SUN2000 > Running Param. >
   Power Adjustment. On the displayed page, **check that Remote power schedule is
   set to Enable**." (SL3000, PDF p.128 (120), §6.4.2 Step 1).
   **This answers open question 5 of `HUAWEI_SUN2000_REGISTER_EVIDENCE.md`:
   there IS a documented enable that gates whether a percentage limit takes
   effect.** The manuals available do not give a Modbus register for it — it is
   documented only as a WebUI/app parameter, so **its register address is not
   documented in the manuals available**.
2. **Per-inverter**: `Schedule instruction valid duration (s)` — "Specifies the
   time for maintaining the scheduling instruction. **When this parameter is set
   to 0, the scheduling instruction takes effect permanently.**" (SL3000, PDF
   p.102 (94)). The inverter-side register for it is INV-V3 signal 433
   "Scheduling instruction maintenance time", RW U32, unit s, gain 1, **42019**,
   2 registers, "Permanently valid when equal to 0" (INV-V3, PDF p.71 region,
   signal 433). If this is non-zero on site, **a limit this controller writes will
   expire on its own** and the inverter will return to full output — a PV-DG
   controller must either set it to 0 or refresh inside the window.
3. **Per-inverter**: `Shutdown at 0% power limit` — "If this parameter is set to
   Enable, the inverter shuts down after receiving the 0% power limit command."
   (SL3000, PDF p.103 (95)). Directly relevant: this controller can command 0 %.
4. **Logger-level, if the logger's own power interface is used**: "To ensure that
   the SmartLogger will deliver scheduling commands to the connected solar
   inverters, **you must select the active or reactive power control mode** before
   adjusting the active or reactive power for a PV plant. If **Active power control
   mode** is set to **No limit** … **the SmartLogger does not send scheduling
   commands to the connected solar inverters**." (SL3000, PDF p.128 (120), §6.4.1)
   — with the important mitigation that the required mode is entered
   automatically: "As the **Remote communication scheduling** mode has a **higher
   priority**, the SmartLogger **automatically changes Active power control mode to
   Remote communication scheduling after receiving a scheduling command from the
   upper-layer management system**." (SL3000, PDF p.130 (122)).
5. **Logger-level**: Modbus TCP is **off by default** — "To reduce network security
   risks, the function of connecting to a third-party management system using
   Modbus TCP is **disabled by default**. … To use this function, set this
   parameter to **Enable(Limited)** or **Enable(Unlimited)**." (SL3000, PDF p.87
   (79)). Under `Enable(Limited)` **our controller's IP must be entered as one of
   `Client N IP Address`, N = 1..5**, or it will not be allowed to connect at all.
6. **Permission failure is a defined Modbus exception**: code **`0x80` NO
   PERMISSION — "An operation is not allowed because of a permission
   authentication failure or permission expiration."** (SL-MB, PDF p.41 (33),
   Table 4-3). This is a Huawei-specific code outside the standard set; firmware
   that only decodes 0x01–0x0B will mis-report it. Several licence-expiry alarms
   exist for control features (1119 License Expired, 1123–1125 Remote Control
   Certificate invalid/to-expire/expired) (SL-MB, PDF pp.21–22 (13–14)).

### Acknowledge before apply — yes, this hazard is documented

For the logger's own power interfaces, SL-MB states the write is **stored** and
then forwarded:

> "**This interface stores data** and the adjustment value should be issued at
> intervals of not less than 1 seconds."
> (SL-MB, PDF p.32 (24), Table 3-1, restraint on 40420/40422, on 40424/40426, and
> on 40428/40429)

and

> "After the SmartLogger receives the instruction value, it **synchronizes the
> value in percentage to all connected inverters**."
> (SL-MB, PDF p.33 (25), §3.3 for 40428; §3.1 says the same for 40420/40424)

Combined with the FC06 response format — which echoes address and value
(SL-MB, PDF p.43 (35), §4.3.4.2) — and the fact that these registers are **RW**
and "RW signals are permanently valid, will be retained until updated the next
time" (SL-MB, PDF p.11 (3), §2 preamble):

**A successful FC06/FC10 to 40428 proves only that the logger stored the value.
Reading 40428 back reads the stored command, not the plant's achieved state.**
Readback of the command register is therefore *not* a confirmation of application.
It is a confirmation of *acceptance*.

The manuals available do **not** state a time between the logger storing a value
and the inverters having applied it. **Not documented in the manuals available.**

Note also that `0x05 ACKNOWLEDGE` ("The server has accepted the request and is
processing it, but a long duration of time will be required to do so") and `0x06
SERVER DEVICE BUSY` are in the logger's exception list (SL-MB, PDF p.40 (32)), as
are the gateway codes `0x0A GATEWAY PATH UNAVAILABLE` ("the gateway was unable to
allocate an internal communication path … Usually means that the gateway is
misconfigured or overloaded") and `0x0B GATEWAY TARGET DEVICE FAILED TO RESPOND`
("no response was obtained from the target device") (SL-MB, PDF p.41 (33)).
**`0x0B` is the exception a client will see when an inverter behind the logger is
offline, and it must not be treated as a bad register address.** This is a
concrete firmware gap worth checking in the Modbus client's exception handling.

---

## 3. The SmartLogger's own plant-level power-control registers

**Answer: yes. These exist and are explicitly intended for an external system.
Unit id = the SmartLogger address (0 by default).**

Signal table (all from SL-MB Table 2-1, PDF pp.12–19 (4–11), and Table 3-1,
PDF pp.32–33 (24–25); addresses are raw wire addresses per §0):

| Signal | R/W | FC | Address | Words | Type | Unit | Gain | Range / note | Citation |
|---|---|---:|---:|---:|---|---|---:|---|---|
| **Active power adjustment by percentage** | RW | 03 / 06 / 10 | **40428** | 1 | U16 | % | **10** | "The percentage range is 0–100%." Reference = "the sum of the rated power of all inverters". | SL-MB p.13 (5) SN18; §3.3 p.33 (25) |
| Active adjustment (absolute) | RW | 03/06/10 | 40420 | 2 | U32 | kW | 10 | "Adjusts the total active output power of all inverters… The adjustment value that is beyond the range is discarded." | SL-MB p.12 (4) SN14 |
| Active adjustment (absolute, 2nd) | RW | 03/06/10 | 40424 | 2 | U32 | kW | 10 | same object, second interface | SL-MB p.13 (5) SN16 |
| Reactive adjustment | RW | 03/06/10 | 40422 / 40426 | 2 | I32 | kVar | 10 | applied as Q/S to all inverters | SL-MB p.12–13 (4–5) SN15/17; §3.2 |
| Power factor adjustment | RW | 03/06/10 | 40429 | 1 | I16 | — | 1000 | "(-1,-0.8]U[0.8,1]" | SL-MB p.13 (5) SN19 |
| **Max. active adjustment** | RO | 03 | **40697** | 2 | U32 | kW | 10 | "Equals the total maximum power of all inverters connected in parallel." Query this for the live absolute range. | SL-MB p.16 (8) SN46; §3.1 |
| Max. / Min. reactive adjustment | RO | 03 | 40693 / 40695 | 2 | U32 / I32 | kVar | 10 | total max power x 60 % (and x -60 %) | SL-MB p.16 (8) SN44/45 |
| **Active power control mode** | **RO** | 03 | **40737** | 1 | U16 | — | 1 | `0` No limit; `1` DI active scheduling; `3` Percentage fixed-value limitation (open loop); `4` **Remote scheduling**; `6` Export Limitation(kW); `200` Remote output control; `65533` Slave SmartLogger; `65534` no scheduling | SL-MB p.17 (9) SN54 |
| Active power scheduling target value | RO | 03 | 40738 | 2 | U32 | kW | 10 | "Target total active power for the SmartLogger active power scheduling" | SL-MB p.17 (9) SN55 |
| **Active scheduling percentage** | RO | 03 | **40802** | 2 | **U32** | % | **1** | `[0, 100]` — note gain **1**, unlike 40428's gain 10 | SL-MB p.18 (10) SN59 |
| Reactive power control mode | RO | 03 | 40740 | 1 | U16 | — | 1 | see enum in source | SL-MB p.18 (10) SN56 |
| Reactive scheduling curve mode / target | RO | 03 | 40741 / 40742 | 1 / 2 | U16 / I32 | — / kVar | 1 / **10 or 1000** | gain depends on mode: 1000 for power factor, 10 for reactive fixed value | SL-MB p.18 (10) SN57/58 |
| Plant active power (measured) | RO | 03 | 40525 | 2 | I32 | kW | 1000 | "Equals the total active output power of all inverters." Gain 1000 ⇒ raw watts. | SL-MB p.13 (5) SN23 |
| Plant reactive power | RO | 03 | 40544 | 2 | I32 | kVar | 1000 | total of all inverters | SL-MB p.14 (6) SN26 |
| Plant input (DC) power | RO | 03 | 40521 | 2 | U32 | kW | 1000 | total input power of all inverters | SL-MB p.13 (5) SN21 |
| Phase A/B/C current | RO | 03 | 40572 / 40573 / 40574 | 1 | I16 | A | 1 | sum over inverters | SL-MB p.15 (7) SN36–38 |
| Uab / Ubc / Uca | RO | 03 | 40575 / 40576 / 40577 | 1 | U16 | V | 10 | — | SL-MB p.15 (7) SN39–41 |
| Locked | RO | 03 | 40699 | 1 | U16 | — | 1 | `0` Locked, `1` Unlocked — "If more than one inverter is on-grid and feeding power to the grid, the status is Unlocked." **Note the polarity: 0 = Locked.** | SL-MB p.16 (8) SN47 |
| Plant status (Qinghai) | RO | 03 | 40543 | 1 | U16 | — | 1 | `1` Unlimited power operation; `2` Limited power operation; `3` Idle; `4` Outage; `5` Communication interrupt. Manual marks it "Used by Qinghai". | SL-MB p.14 (6) SN25 |
| Plant status (Xinjiang) | RO | 03 | 40566 | 1 | U16 | — | 1 | 0 Idle…7 Communication interrupt; "Used by Xinjiang" | SL-MB p.15 (7) SN32 |
| Plant status (Ningxia) | RO | 03 | 40567 | 1 | U16 | — | 1 | "Used by Ningxia" | SL-MB p.15 (7) SN33 |
| **Power on/off (all inverters)** | **WO** | 06 / 10 | **40202** | 1 | U16 | — | 1 | `0` Power off all inverters, `1` Power on all inverters | SL-MB p.12 (4) SN10 |
| Power on/off (inverted polarity!) | WO | 06/10 | 40203 | 1 | U16 | — | 1 | `0` Power **on** all inverters, `1` Power **off** all inverters — **opposite sense to 40202** | SL-MB p.12 (4) SN11 |
| Power on / Power off | WO | 06/10 | 40200 / 40201 | 1 | U16 | — | 1 | data field can only be 0 | SL-MB p.12 (4) SN8/9 |
| Transfer trip | RW | 03/06/10 | 40204 | 1 | U16 | — | 1 | `0` Run, `1` Fault outage — "The device shuts down when it stops due to faults and **does not respond to the startup request**." | SL-MB p.12 (4) SN12 |
| ESN | RO | 03 | 40713 | 10 | STR | — | 1 | logger serial | SL-MB p.16 (8) SN49 |
| Rated plant capacity | RO | 03 | 41936 | 2 | U32 | kW | 1000 | — | SL-MB p.19 (11) SN62 |
| Total rated capacity of grid-connected inverters | RO | 03 | 41938 | 2 | U32 | kW | 1000 | the denominator behind 40428's percentage | SL-MB p.19 (11) SN63 |
| PV module capacity | RO | 03 | 41934 | 2 | U32 | kW | 1000 | `[0, 2000000]` | SL-MB p.19 (11) SN61 |
| **Communication abnormal shutdown** | RW | 03/06/10 | **41947** | 1 | U16 | — | N/A | `0` Disable, `1` Enable | SL-MB p.19 (11) SN66 |
| **Communication abnormal detection time** | RW | 03/06/10 | **41948** | 1 | U16 | **s** | N/A | **`[60, 1800]`** | SL-MB p.19 (11) SN67 |
| **Auto start upon communication recovery** | RW | 03/06/10 | **41949** | 1 | U16 | — | N/A | `0` Disable, `1` Enable | SL-MB p.19 (11) SN68 |
| SystemTime year…second | RW | 03/06/10 | 42017–42022 | 1 each | U16 | — | 1 | **collision warning: on the *logger* (unit 0), 42017 is "SystemTime: year", range 2000–2068. On an *inverter* (unit 1–247), 42017 is "active power gradient".** | SL-MB p.19 (11) SN69; INV-V3 signal 432 |
| Alarm Info 1 / 2 | RO | 03 | 50000 / 50001 | 1 | U16 | — | 1 | bit-mapped; alarm bit map in Table 2-2 | SL-MB p.20 (12) SN78/79 |
| System reset | WO | 06/10 | 40723 | 1 | U16 | — | 1 | "Resets the SmartLogger. **The data domain is not checked.**" — do not probe this register | SL-MB p.16 (8) SN50 |
| Fast device access | WO | 06/10 | 40724 | 1 | U16 | — | 1 | "Automatically allocates and searches for devices." — **can renumber devices; do not probe** | SL-MB p.16 (8) SN51 |
| Device operation | WO | 06/10 | 40725 | 11 | MLD | — | 1 | first 10 words = target ESN; last word `0` = **delete inverter**, `1` = reset inverter alarm. **Do not probe.** | SL-MB p.17 (9) SN52 |
| Device access status | RO | 03 | 40736 | 1 | U16 | — | 1 | `0` search complete, `1` in progress, `2` failed | SL-MB p.17 (9) SN53 |

**The 42017 collision above is the single most dangerous finding in this document.**
The same decimal address means "active power ramp gradient" on an inverter and
"system clock year" on the logger. A profile that writes 42017 with unit id
accidentally left at 0 sets the logger's clock year to a ramp value. Nothing in
this firmware currently writes 42017 (per
`HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §5), and this analysis is a reason to keep
it that way unless the unit id is proven.

**Note the two gains for the same physical quantity:** the writable
`40428` is **percent x 10** (gain 10), while the read-only status
`40802 Active scheduling percentage` is **percent x 1** (gain 1) and is U32 over
2 words. A confirmation path that reads 40802 after writing 40428 must not reuse
one scale factor for both.

---

## 4. Timing, intervals and fail-safe

Everything the manuals state, quoted exactly. This is much less than the firmware
needs, but it is not nothing.

**Minimum command interval — the only hard number for commanding:**

> "This interface stores data and **the adjustment value should be issued at
> intervals of not less than 1 seconds**. The adjustment value that is beyond the
> range is discarded."
> — SL-MB, PDF p.32 (24), Table 3-1, applied to 40420/40422, 40424/40426, and
> 40428/40429.

**Response timeout:**

> "In unicast mode, a slave node returns one response for each request from the
> master node. **If the master node does not receive any response from the slave
> node in 5s, the communication process is regarded as timed out.** In broadcast
> mode, slave nodes receive instructions from the master node, but do not respond
> to the instructions."
> — SL-MB, PDF p.37 (29), §4.2.4 Interaction Process.

This 5 s is a **client-side timeout convention the logger's protocol assumes**,
not a guarantee of the logger's own response latency, and it is not a settle time
for a setpoint. It is a striking coincidence with this firmware's 5000 ms
confirmation deadline — but the coincidence is not evidence, and the firmware's
deadline covers *settle-and-readback*, which is a longer thing than *one
request/response*.

**Read/write size limits (affects how a poll is batched):**
- FC03 "Number of registers: 1–125" (SL-MB, PDF p.41 (33), §4.3.3.1)
- FC10 "Number of registers: 0x0000–0x007b" (= 123) (SL-MB, PDF p.44 (36), §4.3.5.1)
- "A ModBus-TCP frame can contain a maximum of 256 bytes." (SL-MB, PDF p.36 (28), §4.2.2)
- Big-endian: "Modbus uses a big-Endian to represent addresses and data."
  (SL-MB, PDF p.37 (29), §4.2.3)

**Fail-safe on loss of communication — three separate documented mechanisms:**

1. **Logger loses its northbound link (i.e. loses *us*)** — SL-MB registers
   41947/41948/41949: `Communication abnormal shutdown` (0 Disable / 1 Enable),
   `Communication anbormal detection time` **[60, 1800] s** [sic, spelling as
   printed], `Auto start upon communication recovery` (0 Disable / 1 Enable)
   (SL-MB, PDF p.19 (11), SN66–68). **These are the real numbers the firmware was
   missing for the northbound watchdog: the shortest configurable detection window
   is 60 s.** The manuals available do **not** state what the logger does to the
   inverters during that window (hold last value vs release to full output) — that
   is **not documented in the manuals available**. Whether the 3000A firmware on
   site implements 41947–41949 at all is a site-verification item: they were added
   in SL-MB Issue 35 (2020-02-20) (SL-MB, PDF p.3, Change History) and SL3000
   Issue 03 (2020-01-10) predates that and does not describe a matching WebUI
   parameter.
2. **Inverter loses the logger (southbound)** — per-inverter, WebUI-level:
   `Communication disconnection fail-safe` — "In the inverter export limitation
   scenario, if this parameter is set to Enable, the inverter will perform active
   power derating by percentage when the communication between the inverter and
   the SmartLogger or Smart Dongle is disconnected for more than the time
   specified by **Communication disconnection detection time**";
   `Fail-safe power threshold (%)` — "Specifies the derating value of the inverter
   active power by percentage." (SL3000, PDF p.104 (96), *Power Adjustment
   Parameters*). No register addresses are given for these in the manuals
   available. Note the precondition — "In the inverter export limitation
   scenario" — so it may not apply in a remote-scheduling-only configuration;
   that qualification is not elaborated anywhere in the manuals available.
3. **Scheduling instruction expiry** — `Schedule instruction valid duration (s)`
   (SL3000, PDF p.102 (94)) / INV-V3 signal 433 at **42019**, U32 s gain 1,
   "Permanently valid when equal to 0" (INV-V3, PDF p.71 region). If non-zero,
   the limit self-cancels. **This is effectively an inverter-side dead-man switch
   and is the correct mechanism for a safety-relevant limit — but its numeric
   value on site is unknown.**

**Ramp / gradient:** `Active power change gradient (%/s)` is a documented
per-inverter parameter (SL3000, PDF p.103 (95)); the register is INV-V3 signal
432, RW U32, %/s, gain 1000, **42017** — with the unit-id collision warned about
in §3.

**Logger-internal loop periods** (only for logger-owned closed-loop modes, not for
pass-through): `Adjustment period` — "Specifies the interval for sending
adjustment commands by the SmartLogger"; `Adjustment deadband`; and for export
limitation `Power lowering adjustment period`, `Maximum protection time`
("maximum duration from the time when the SmartLogger detects backflow to the time
when the inverter output power reaches 0"), `Power raising threshold`, `Fail-safe
power threshold` (SL3000, PDF pp.131 (123), 139 (131), 140 (132)). **No default
or permitted-range values are printed for any of these in SL3000.**

**Under Remote Communication Scheduling the logger's allocation policy is
configurable and changes plant behaviour:** `Percentage(%)` = `Disable` /
`Strategy 1` / `Strategy 2`. "Disable: The SmartLogger controls the solar inverter
to work at full load and **will not receive scheduling commands sent by the
management system**." "Strategy 1: Open-loop scheduling policy. That is, the
SmartLogger **evenly allocates** the power value from the scheduling and delivers
the average value to each solar inverter … The adjustment value delivered by the
SmartLogger is constant. If **Adjustment coefficient** is set, the power value
will be sent to the solar inverter **after being multiplied by the preset
coefficient**." "Strategy 2: The customized function is provided for a specific
power plant. Set **Overshoot**, **Adjustment period**, and **Adjustment deadband**
based on the scheduling requirements of the power plant." (SL3000, PDF p.130
(122)).

**Read that twice.** Under Strategy 1 with an `Adjustment coefficient` set, a
40428 write of 80 % does **not** produce 80 % at the plant. The coefficient is
invisible to a Modbus client. There is no register for it in the manuals
available. **A controller commanding 40428 must close its loop on measured power,
never on the assumption that the commanded percentage is the delivered
percentage.** This firmware measures at the meter, which is the right structure —
but any open-loop feed-forward term keyed to the commanded percentage is unsafe
through a logger.

**No documented polling interval for a third-party client.** The only polling
periods in the manuals concern Huawei's own management system data collection
("Data synchronization mechanism: five-minute interval", INV-V3, PDF page in the
management-interface chapter) and `Night silent` / `Wakeup period` for southbound
device queries (SL3000, PDF pp.90–91 (82–83)). A per-inverter southbound poll
budget — i.e. how fast the logger can refresh 80 inverters over RS485 at the
configured baud (`1200, 2400, 4800, 9600, 19200, or 115200`, SL3000, PDF p.90
(82)) — is **not documented in the manuals available**, and it bounds how fresh
*any* pass-through read can possibly be. `Device disconnection time` — "Specifies
the duration for determining device disconnection" — is configurable with no
stated default or range (SL3000, PDF p.93 (85), built-in MBUS parameters).

---

## 5. Unit-id discovery

**Answer: yes — a device-list query exists, but it is FC 0x2B, not a register.**

> "**Command for Querying a Device List.** Request: Function code `0x2B`, MEI type
> `0x0E`, ReadDeviId code `03`, Object ID `0x87`."
> Object list: `0x80–0x86` reserved (null, length 0); **`0x87` = "Number of
> devices", int, "Returns the number of devices connected to the RS485 address"**;
> `0x88` = "Information about the first device", ASCII string; `0x89` = second; …
> `0xFF` = 120th; then `0x00` = 121st, `0x01` = 122nd, …
> — SL-MB, PDF pp.47–48 (39–40), §4.3.6.2, Tables 4-9 to 4-11.

Each device description is `attribute=value;` pairs:

> "For example: `1=SUN2000;2=V100R001C01SPC120;3=P1.0-D1.0;4=123232323;5=2;6=1`"
> with `1` Device Model, `2` Software version, `3` Version of the communications
> protocol, `4` **ESN**, `5` Device number ("0,1,2,3... (Assigned by NE; 0 indicates
> the master device to which the ModBus card is inserted)"), `6` Parallel network
> number ("0xFF: invalid value").
> — SL-MB, PDF p.49 (41), §4.3.6.3, Table 4-12.

Critically: **the device description does NOT contain the device's unit id / RS485
address.** It contains model, versions, ESN, device number and parallel-network
number. The mapping *unit id → which physical inverter* therefore has to come
from elsewhere:

- **Per-unit public registers**, queried at each candidate unit id: `65522 Port
  number` (RO U16), `65523 Device Address` (RO U16), `65524 Device name`
  (**RW** STR, 10 words), `65534 Device connection status` (RO U16, `0xB000`
  Disconnection / `0xB001` Online), and `65521 Device list change number`
  (RO U16) (SL-MB, PDF p.28 (20), Table 2-6, and the note beneath it). **This is
  the practical discovery primitive: sweep unit ids 1–247, read 65534; treat
  `0xB001` as present; read 65523/65522/65524 to identify it; and use 65521 to
  detect that the list changed under you.** Note 65524 is writable — a discovery
  sweep must never write it.
- **Or off-band**: "The address of an access device is an RS485 address which can
  be read on the LCD or built-in WebUI of the SmartLogger." (SL-MB, PDF p.36
  (28), §4.2.1 note); `Maintenance > Device Mgmt. > Device List` and
  `Maintenance > Device Mgmt. > Connect Device > Auto Assign Address` /
  `Address Adjustment` on the WebUI, with device info exportable and importable
  as `.csv` (SL3000, PDF pp.175–176 (167–168), §8.8/§8.9).
- **Constraint on the sweep range:** RS485 `Start address` / `End address` are
  per-COM-port settings with "1 ≤ Start address ≤ Communication address of the
  connected device ≤ End address ≤ 247" and "The address segments of COM ports can
  overlap" (SL3000, PDF p.90 (82)).

**Also unresolved:** if `Address mode` is `Logical address` rather than
`Comm. Address` (SL3000, PDF p.87 (79)), the unit id in the MBAP header is *not*
the RS485 address, and 65523's relationship to it is **not documented in the
manuals available**. Under `Auto Assign Address` / `Address Adjustment`, addresses
can be *reassigned* by a site engineer (SL3000, PDF pp.175–176 (167–168)) — so a
unit id persisted in this firmware's configuration is not permanently valid. Bind
identity to the **ESN** (via 0x2B object list, or per-inverter INV-V3 registers)
rather than to the unit id, and re-verify on every connect.

---

## 6. Direct vs logger; contention and connection limits

**Can a client bypass the logger and reach an inverter directly over Modbus TCP?**
INV-V3 says the inverter itself speaks Modbus-TCP only over IP-capable media:
"Huawei solar inverters provide Modbus communication based on physical media such
as MBUS, RS485, WLAN, FE, and 4G. **MBUS and RS485 comply with the Modbus-RTU
format. The communication through the WLAN, FE, and 4G media is based on the TCP
link and complies with the Modbus-TCP format.**" (INV-V3, PDF p.140 (135), §6.1).
And: "Based on the TCP communications host, **unit 0 is used by default to access
the directly connected slave node**, and other addresses are used to access the
downstream devices of the slave node. **The default address of the slave node is
0. The address is adjustable.**" (INV-V3, PDF p.144 (139), §6.2.2.3). Port: "The
master node can use the **502** port to request data services from the slave node"
(INV-V3, PDF p.145 (140), §6.2.2.4). SL-MB gives the same port for the logger:
"Communicates over an Ethernet. **Port number: 502**" (SL-MB, PDF p.35 (27), §4.1).

So a direct path exists **only if the inverter has its own IP connectivity**
(WLAN/FE/4G). In a SmartLogger plant the inverters are typically on RS485 or MBUS
to the logger, in which case **there is no direct TCP path and the logger is not
optional** — but which media the site actually uses is **not determinable from the
manuals**. Note also that in the *inverter's* own TCP addressing, unit `0` means
"the directly connected device" — the **opposite** convention from a pass-through
frame to the logger, where `0` means the logger. A client that guesses the
topology will address the wrong thing.

**Connection limits, stated:**

> "If this parameter is set to **Enable(Limited)**, the SmartLogger can connect to
> a maximum of **five** preset third-party management systems. If this parameter is
> set to **Enable(Unlimited)**, the SmartLogger can connect to a maximum of
> **five** third-party management systems with a valid IP address."
> — SL3000, PDF p.87 (79), Modbus TCP `Link setting`.

So **five concurrent northbound Modbus TCP clients maximum**, either way; the
difference is IP allow-listing, not count. (The same wording appears for IEC104,
SL3000, PDF p.88 (80).) A slave SmartLogger cascade uses one of those slots:
`Link setting = Enable(Limited)` with `Client N IP Address` = master
SmartLogger's IP (SL3000, PDF p.91 (83), §6.3.4).

**Contention between two writing masters is nowhere addressed.** The manuals do
not say what happens if two of the five clients write 40428 with different values,
nor what happens if one client writes plant-level 40428 while another writes
per-inverter 40125. What *is* documented is that the logger's mode switches under
you: "the SmartLogger **automatically changes Active power control mode to Remote
communication scheduling** after receiving a scheduling command from the
upper-layer management system" (SL3000, PDF p.130 (122)). So a second EMS writing
the logger will **silently take over the plant mode** and 40737 will reflect it.
Reading 40737 is therefore also a *contention detector*.

Also note the logger has scheduling authorities of its own that will fight a
third-party client: DI active scheduling from a ripple-control receiver,
percentage fixed-value limitation by time-of-day schedule, grid connection with
limited power (export limitation, meter-based closed loop), and remote output
control from a utility server (SL3000, PDF pp.128–133 (120–125)). Any of these
being configured on site means **something other than this controller is already
commanding the plant.**

Finally, the manuals do not document any bypass *prohibition* — SL-MB explicitly
blesses per-inverter access through the logger ("can directly access the
inverter", SL-MB, PDF p.32 (24), §3). What is absent is any statement about doing
plant-level and per-inverter commanding *simultaneously*. **Not documented in the
manuals available**, and it should be treated as unsafe: the logger's own
allocation loop and a per-inverter controller would be two masters of the same
actuator.

---

## Unknown / requires the physical site

Ordered by how badly a wrong assumption hurts.

1. **Is the SmartLogger actually in the path at all, and on which media
   (RS485 / MBUS / direct FE)?** Everything below branches on this. Not
   determinable from manuals.
2. **`Address mode`: `Comm. Address` or `Logical address`?** Determines whether
   unit id == RS485 address. `Logical address` numbering is **not documented in
   the manuals available**.
3. **The logger's own unit id** (default 0, configurable, `Settings > Modbus TCP >
   SmartLogger address`) and whether it collides with a device address (alarm 1105).
4. **`Remote power schedule` per inverter: Enable or Disable?** If Disable, every
   write this firmware makes is silently ignored. Register address for this
   parameter is **not documented in the manuals available** — it must be read on
   the WebUI/app.
5. **`Schedule instruction valid duration` / register 42019 value.** Non-zero means
   our limit expires by itself.
6. **`Active power control mode` (40737) as found**, i.e. is some other authority
   already scheduling the plant (DI, time-of-day, export limitation, utility
   remote output control, slave-logger cascade)?
7. **Under Remote Communication Scheduling: `Percentage(%)` = Disable / Strategy 1 /
   Strategy 2, and any `Adjustment coefficient`.** A coefficient makes commanded
   percentage ≠ delivered percentage, invisibly.
8. **Whether 40125 or 40199 is honoured, and how fast**, on real hardware —
   unchanged from `HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §3, and now additionally
   unknown *through a logger*.
9. **Settle time**: logger-store → inverter-applied → readback-reflects, for both
   the plant path (40428 → 40802 / 40525) and the per-inverter path (40125 →
   40125 / 32080). **Not documented in the manuals available.** This is the number
   the firmware's 500 ms settle / 5000 ms deadline windows are currently guessing.
10. **Whether the per-inverter readback at 40125 through the logger returns the
    inverter's live value or a logger-cached one, and how stale that cache can be.**
    Not documented. The remapped block (§2.7) is explicitly a cache with its own
    "communication interrupt" sentinel, which suggests caching exists in the
    pass-through path too — but that is inference, not evidence.
11. **Whether the 3000A firmware implements SL-MB Issue 35 registers**
    41947/41948/41949 (northbound comms watchdog) and 40802.
12. **What the logger does to inverter output when its northbound link is lost**
    (hold / release to 100 % / shut down), inside and after the 60–1800 s window.
13. **Southbound poll cycle time for the actual inverter count and baud rate** —
    bounds telemetry freshness and therefore the control loop period.
14. **How many of the five northbound Modbus TCP slots are already used**, and by
    whom.
15. **Whether `Enable(Limited)` is set and whether our controller's IP is
    allow-listed** — if not, the socket never opens.
16. **`Shutdown at 0% power limit`** per inverter — determines whether a 0 %
    command from this controller shuts the machine down or holds it at zero.
17. **Whether writes return exception `0x80 NO PERMISSION`** (licence/certificate
    gating on control features).

---

## Recommendation: command at the logger, not per inverter

**Command the plant at the SmartLogger, using `40428 Active power adjustment by
percentage` (unit id = SmartLogger address, FC06, U16, percent x 10, 0–100 %),
and close the loop on measured power at the meter. Keep per-inverter access
read-only.**

Reasoning:

1. **It is the interface Huawei designates for exactly this caller.** SL-MB §3 is
   titled *Power Adjustment for Inverters* and describes the built-in power
   interface as being for "array-level power adjustment", with "The external device
   sends the active power adjustment target value in percentage" (SL-MB, PDF p.33
   (25), §3.3). SL3000 calls our role "the management system or independent power
   adjustment device" (SL3000, PDF p.130 (122)). A PV-DG controller is an
   independent power adjustment device.
2. **The mode gate resolves itself.** Writing the logger causes it to switch to
   Remote communication scheduling automatically and at higher priority (SL3000,
   PDF p.130 (122)). Commanding per-inverter does **not** grant that priority, and
   leaves the logger's own scheduling authority (DI, time-of-day, export
   limitation) live and free to overwrite our per-inverter setpoints at its own
   `Adjustment period`. **Two masters of one actuator.**
3. **One write instead of N.** With a documented ≥1 s minimum interval per
   interface, N per-inverter writes serialised behind one logger and one RS485 bus
   is a poll budget problem that scales with plant size; 40428 does not.
4. **The plant-level readback set is coherent**: 40737 (mode — also a contention
   detector), 40802 (active scheduling percentage), 40525 (measured plant active
   power), 40697 (live max active adjustment), 40699 (locked/unlocked), 41938
   (the rated-capacity denominator), 50000/50001 (alarms). Per-inverter, the
   read-only remapped block (51000 + 25*(addr-1)) gives per-machine active power
   and fault codes for diagnostics **without any write risk** — and is the right
   place to detect one dead inverter inside a healthy plant.
5. **It avoids the 42017 unit-id collision** and the whole class of "wrote an
   inverter register at unit 0" faults.

**Risks of commanding at the logger, and their mitigations:**

| Risk | Mitigation |
|---|---|
| Commanded % ≠ delivered %, because of `Adjustment coefficient` / Strategy 1 averaging / Strategy 2 deadband (SL3000 p.130 (122)) | Close the loop on **measured** power (meter), never on the commanded value. Confirm 40802/40525 move in the right direction rather than matching a target exactly. |
| Write is only *stored* by the logger; FC06 echo and 40428 readback prove acceptance, not application (SL-MB p.32 (24), §3.3 p.33 (25)) | Treat readback-of-command as ACK; treat **40802 + 40525** as the confirmation signals; measure the real settle time on site and configure the settle window from that measurement, not from a default. |
| ≥1 s minimum command interval (SL-MB p.32 (24)) | Rate-limit the control loop's write path to ≥1 s and let the ramp live in the controller, with a deliberate reconciliation against the per-inverter `Active power change gradient` / 42017 found on site. |
| Logger is a single point of failure for the whole plant | Enable an inverter-side or logger-side fail-safe (41947/41948/41949 if implemented, and/or a non-zero 42019 refresh discipline) so that losing this controller does not leave a stale limit — or leaves a *safe* stale limit. Decide which, explicitly. |
| Another of the five northbound clients takes the plant over silently | Poll 40737; treat an unexpected mode, or a 40802 that does not follow our command, as a control-authority fault and surface it. |
| Whether 41947–41949 exist on this 3000A firmware is unknown | Read them on site before relying on them; if absent, the northbound watchdog has to be provided some other way. |

**Risks of commanding per-inverter (why it is the weaker choice here):**

- Requires `Remote power schedule = Enable` on every inverter, a parameter with no
  documented register — so it can be flipped off by a service visit and this
  firmware would have **no way to detect it** except by observing that power does
  not move.
- Leaves the logger's own scheduling free to fight us, with the logger having
  higher effective authority (it will switch modes and re-broadcast to all
  inverters).
- Unit-id fragility: `Auto Assign Address` can renumber devices (SL3000, pp.175–176
  (167–168)), and under `Logical address` mode the mapping is undocumented.
- 40125 vs 40199 is still unresolved (see `HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §3),
  and resolving it through a logger conflates two unknowns.
- Readback may be a logger cache of unknown staleness.
- The `0x0B GATEWAY TARGET DEVICE FAILED TO RESPOND` exception becomes routine
  (any offline inverter), and must not be confused with a bad address.

**Where per-inverter commanding *is* the right answer:** if the site turns out to
have **no SmartLogger in the path** (inverters directly on FE/WLAN), or if the
logger's plant interface is proven unimplemented or refused on site. The profile's
`INVERTER_PROFILE_CONNECTION_LOGGER_GATEWAY` should therefore be understood as
"**topology unknown until commissioning**", and the commissioning wizard should
*determine* the path rather than assume it. **Do not promote
`huawei.sun2000.pending` past `DOCUMENTED` on the strength of this document — it
resolves manual questions, not hardware behaviour.**

A defensible implementation shape, for whoever picks this up: a distinct
`huawei.smartlogger.plant` profile whose command target is unit id
(SmartLogger address) / FC06 / 40428 / percent x 10, whose readback is 40802
(U32, gain 1) plus 40525 for measured power, whose mode assertion is 40737 == 4,
whose minimum write interval is 1000 ms, and which carries the per-inverter
remapped block as read-only telemetry — kept separate from the per-inverter
`huawei.sun2000.pending` profile rather than folded into it, because the two have
different unit ids, different scales and different failure modes.

---

## Site-verification checklist (laptop + mbpoll)

Read-only first, throughout. `-1` = one-shot, `-t4` = holding registers (FC03),
`-0` = **use PDU addresses as given, no +1 offset** — required, because §0 above
establishes the manual's decimal numbers are already wire addresses. `$SL` = the
logger's IP. `$U` = the logger's unit id (try 0 first). Confirm your mbpoll's
`-0` semantics on a known register (e.g. 41938 rated capacity) before trusting any
result.

**Before touching anything:**

- [ ] On the WebUI, record: `Settings > Modbus TCP` → `Link setting`,
      `Client 1..5 IP Address`, `Address mode`, `SmartLogger address`.
      Add the controller's IP if `Enable(Limited)`.
- [ ] On the WebUI, record `Settings > Active Power Control` → `Active power
      control mode`, and if Remote communication scheduling: `Percentage(%)`
      strategy, `Adjustment coefficient`, `Overshoot`, `Adjustment period`,
      `Adjustment deadband`.
- [ ] On the WebUI, record per inverter: `Remote power schedule`,
      `Schedule instruction valid duration (s)`, `Shutdown at 0% power limit`,
      `Active power change gradient (%/s)`, `Communication disconnection
      fail-safe`, `Communication disconnection detection time`,
      `Fail-safe power threshold (%)`.
- [ ] `Maintenance > Device Mgmt. > Device List`: export the CSV. This is the
      authoritative unit-id ↔ inverter map. Keep it with the commissioning record.
- [ ] Confirm how many of the five northbound Modbus TCP slots are already in use.

**Identify the logger and prove the addressing convention:**

```
mbpoll -m tcp -a 0  -r 41938 -c 2 -t4 -0 -1 $SL     # Total rated capacity of grid-connected inverters, U32 kW gain 1000
mbpoll -m tcp -a 0  -r 40713 -c 10 -t4 -0 -1 $SL    # ESN (STR) - should be readable ASCII
mbpoll -m tcp -a 0  -r 40737 -c 1  -t4 -0 -1 $SL    # Active power control mode (expect 0/1/3/4/6/200/65533/65534)
```
- [ ] 41938 returns a plausible plant kW x1000 and 40737 returns one of the
      documented enum values ⇒ unit id and no-offset convention are both confirmed.
      If these read garbage, **stop** and re-derive the convention; do not proceed
      to any write.

**Plant-level read set (all read-only, safe):**

```
mbpoll -m tcp -a $U -r 40525 -c 2 -t4 -0 -1 $SL   # plant active power, I32 kW gain 1000 (raw watts)
mbpoll -m tcp -a $U -r 40697 -c 2 -t4 -0 -1 $SL   # Max. active adjustment, U32 kW gain 10
mbpoll -m tcp -a $U -r 40802 -c 2 -t4 -0 -1 $SL   # Active scheduling percentage, U32 %, gain 1
mbpoll -m tcp -a $U -r 40428 -c 1 -t4 -0 -1 $SL   # current stored percentage command, U16 % gain 10
mbpoll -m tcp -a $U -r 40699 -c 1 -t4 -0 -1 $SL   # Locked: 0=Locked, 1=Unlocked
mbpoll -m tcp -a $U -r 50000 -c 2 -t4 -0 -1 $SL   # Alarm Info 1/2 bitmaps
mbpoll -m tcp -a $U -r 41947 -c 3 -t4 -0 -1 $SL   # comms-abnormal shutdown / detection time / auto-start
```
- [ ] Record every value. **Whether 41947–41949 read at all is itself the answer to
      unknown #11** — an exception `0x02` here means this firmware does not
      implement them.
- [ ] Cross-check 40525 against the site's own metering. If they disagree,
      the gain assumption (1000 ⇒ watts) is wrong and must be re-derived before
      any closed-loop control.

**Discover devices behind the logger (read-only):**

- [ ] Sweep unit ids over the RS485 `Start address`..`End address` range and read
      the per-device connection status:
      `for u in $(seq 1 32); do mbpoll -m tcp -a $u -r 65534 -c 1 -t4 -0 -1 $SL; done`
      → `0xB001` (45057) = Online, `0xB000` (45056) = Disconnection.
- [ ] For each responder, read `65523` (Device Address), `65522` (Port number),
      `65524` (Device name, 10 words). **Never write 65524.**
- [ ] Read `65521` (Device list change number) before and after the sweep; if it
      changed, the list moved under you — redo it.
- [ ] Compare the sweep to the exported Device List CSV. Any mismatch means
      `Address mode` is `Logical address` (or an address was reassigned) — record
      the actual mapping.
- [ ] Per inverter, confirm the direct map is reachable through the logger:
      `mbpoll -m tcp -a $u -r 30000 -c 8 -t4 -0 -1 $SL` (nameplate string, expect
      `SU…`) and `-r 32080 -c 2` (active power, I32 watts). If 30000 reads the
      nameplate through the logger, pass-through addressing is confirmed identical
      to direct.
- [ ] Read the remapped block for the same device and cross-check active power:
      `mbpoll -m tcp -a $U -r $((51000 + 25*(u-1))) -c 2 -t4 -0 -1 $SL`. Equality
      with 32080 confirms both the formula and the pass-through path; a difference
      quantifies the cache staleness (unknown #10).
- [ ] Per inverter, read the currently active limits **without writing**:
      `-r 40125 -c 1`, `-r 40199 -c 1`, `-r 42017 -c 2` (gradient, %/s gain 1000),
      `-r 42019 -c 2` (scheduling instruction maintenance time — **if non-zero,
      any limit expires**).

**Never probe these. They are write-only actions, not settings:**
`40200`, `40201`, `40202`, `40203` (plant power on/off), `40205` (array reset),
`40723` (system reset — "the data domain is not checked"), `40724` (fast device
access — reassigns addresses), `40725` (device operation — **deletes inverters**),
`42730` (inspection), `42779` (IV curve scanning). Also treat `40204`
(transfer trip) as untouchable: setting it to 1 latches a fault outage that "does
not respond to the startup request".

**Write test — only with the plant owner present, in daylight, with a way back:**

- [ ] Record the pre-test state: 40737, 40428, 40802, 40525, and the site meter.
- [ ] Write a mild limit and time it. Use a value clearly below present output but
      well above zero, e.g. 80.0 % → raw 800:
      `mbpoll -m tcp -a $U -r 40428 -t4 -0 800 $SL`
- [ ] **Timestamp four things**: (a) the FC06 response, (b) the first change in
      40428 readback, (c) the first change in 40802, (d) the first change in 40525
      / the site meter, and (e) the time 40525 settles. **(a)→(e) is the real
      settle time.** This is the number that replaces the firmware's guessed
      500 ms / 5000 ms windows. Repeat at least three times.
- [ ] Confirm 40737 flipped to `4` (Remote scheduling) by itself, as SL3000 says
      it will. If it did not, the mode gate is not automatic here and must be
      handled explicitly.
- [ ] **Check the coefficient:** compute delivered % = 40525 / 40697 and compare to
      80 %. A systematic offset is `Adjustment coefficient` or Strategy-1
      averaging. Record the ratio; it is the reason the loop must close on
      measurement.
- [ ] Repeat with 60 % and 90 % to confirm monotonicity and to measure the ramp
      rate actually delivered (compare against the per-inverter 42017 gradient).
- [ ] Verify ≥1 s spacing matters: issue two writes 200 ms apart and check whether
      the second is honoured or discarded.
- [ ] Restore: write back the recorded pre-test 40428 value, confirm 40802/40525
      return, and confirm 40737 with the plant owner. **If the pre-test mode was
      not Remote scheduling, the mode has now been changed and the owner must
      decide how to restore it — this is not reversible from Modbus alone.**
- [ ] Only after all of the above: repeat the timing test **per inverter** on one
      machine at 40125 and at 40199, to settle the 40125-vs-40199 question with
      hardware evidence — then stop, because the recommendation is not to command
      per-inverter in production.

**Comms-loss behaviour (the test nobody runs and everybody needs):**

- [ ] With a non-zero limit in force, unplug the controller's Ethernet. Watch
      40525 / the site meter (via the WebUI, not Modbus) for the full configured
      detection window. **Does the plant hold the limit, release to full output, or
      shut down?** Record the answer and the elapsed time. This determines whether
      a PV-DG controller failure over-generates into a generator — the failure this
      whole product exists to prevent.
- [ ] Repeat with 41947 `Communication abnormal shutdown` enabled and 41948 set to
      its minimum (60 s), if those registers are implemented, and record the
      difference.
