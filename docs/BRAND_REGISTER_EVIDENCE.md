# Brand register evidence: Solis, Growatt, Sungrow, Chint/CPS

Status of every profile added or populated by this work: **DOCUMENTED**. Nothing
here has been exercised against physical equipment, and nothing here has been
exercised against the lab simulator either. No profile is production-approved and
`tests/inverter_write_permission_test.c` executes the write gate over the whole
catalogue to prove none can command production.

This document exists so a reviewer can **check** the register maps in
`components/inverter_manager/inverter_profiles.c` rather than trust them. Every
value below carries a file and page/section citation. Where a manual does not
state something, that is recorded as *not documented* and the corresponding
profile field is left unset — it is not interpolated.

Manuals used (all under `D:\Working\SolTrix-ESP-Lab-Validation\Manuals\Inverter\`):

| Brand | File | Document identity |
|---|---|---|
| Solis | `Solis.pdf` | *RS485_MODBUS Communication Protocol*, "Translated on 2021.5.6", 47 pages |
| Growatt | `GROWATT.pdf` | *Growatt Inverter Modbus RTU Protocol*, doc TH-276-00, V1.20, effective 2020-05-12, 66 pages |
| Sungrow | `Sungrow .pdf` | *Communication Protocol of PV Grid-Connected String Inverters*, V1.1.36, 2021-02-07, 37 pages |
| Chint / CPS | `CPS\CPS_100_125kW-UL-Modbus-Map-Spec-FW-V12.0.pdf` | *CPS Inverter Model Data Mapping Specification For 403X*, applicable models 100kW(125kW)_1500V, V9.03, 2023-01-11, 57 pages |

Page numbers are **PDF page numbers** from `pypdf` extraction. For Solis and
Growatt the printed page number equals the PDF page number; for Sungrow and CPS
the printed footer also matches.

> **Chint and CPS are the same document.** The extracted text of
> `CPS\CPS_100_125kW-UL-Modbus-Map-Spec-FW-V12.0.pdf` and of
> `Chint\CPS_100_125kW-UL-Modbus-Map-Spec-FW-V120_240817_221331.pdf` is
> byte-identical (verified with `cmp`). There is one Chint/CPS protocol, not two.
> The other files in those folders are datasheets and a user manual, and contain
> no register map.

---

## 1. Addressing convention — stated per brand, because it is not uniform

This is the trap that has bitten the project before, so it is recorded first. All
addresses in `inverter_profiles.c` are **0-based PDU addresses as they go on the
wire**.

| Brand | Manual convention | Evidence | Conversion applied |
|---|---|---|---|
| Solis §5.2 (type info, FC 0x04) | already PDU | p.10: "The following table has the same address with the actual address of the message frame. No need extra offset or transform" | none |
| Solis §5.3 (operation info, FC 0x04) | 1-based tag | p.10: "the register address needs to offset one bit. Example: register address: 3000, the send address is 2999" | PDU = tag − 1 |
| Solis §5.6 (settings, FC 0x03/0x06/0x10) | 1-based tag | p.24: "the register address needs to offset one bit. Example: register address: 3007, the send address is 3006" | PDU = tag − 1 |
| Solis §5.5 (grid on/off, FC 0x05) | already PDU | p.24: "no need of offset or other conversion" | none (not used) |
| Solis §5.7 (EPM, FC 0x03/0x06/0x10) | already PDU | TOC p.2: "No need off set" | none (not used) |
| Growatt | already PDU (0-based) | p.9/p.33 both tables begin at index `00`; p.3 states "03 register range：0~124" — a range starting at 0 cannot be a 1-based tag | none |
| Sungrow | 1-based tag | p.5 §3: "Visit all registers by subtracting 1 from the register address. Example: if the address is 5000–5001, visit it using address 4999–5000. Entering `01 04 1387 00 02 + CRC` to check the data of address 5000–5001" — and `0x1387 == 4999` | PDU = tag − 1 |
| Chint/CPS | already PDU | p.8: "(5).Basic register address is 0x0000." | none |

**Weakest link:** Growatt. The manual's frame-format tables on p.5–7 are images
and carry no extractable address bytes, so there is no worked frame proving the
0-based reading — only the two structural arguments above. **This is the single
addressing item to prove with the first read in the lab.** Sungrow, by contrast,
is proved by a worked frame; Solis and CPS by explicit prose.

---

## 2. Solis

Profile id `solis.commercial.pending` (existing catalogue entry, populated in
place — its id is referenced by `tests/inverter_profile_catalogue_source_contract.py`).

| Item | Manual tag | PDU / FC | Type, scale | Citation |
|---|---|---|---|---|
| Inverter type (identity) | 35000 | 35000, FC 0x04 | U16, value table | p.10 §5.2 "SOLIS inverter type definition", `1010 --- 1phase inverter`, `1020 --- 3 phase inverter` |
| Active power output | 3005–3006 | **3004**, FC 0x04 | U32, 1 W → scale 0.001 | p.11 §5.3 "3005 - 3006 Active power U32 1W" |
| Active-power-% **WRITE** | 3052 | **3051**, FC 0x06 | U16, 100 raw/% | p.26 §5.6 "3052 Power limitation U16 … 10000<-->100% Range（0-100%）100% = rated" |
| **READBACK** of the write | 3052 | **3051**, FC 0x03 | U16, scale 0.01 | p.24 §5.6 header: "The function code is 0x03, 0x06 and 0X10" |
| Applied % (alternative readback) | 3050 | 3049, FC 0x04 | U16, 10000↔100% | p.12 §5.3 "3050 Power limit actual U16 1% 10000<-->100%" |
| Limit **enable** switch | 3070 | 3069, FC 0x06 | 0xAA ON / 0x55 OFF | p.27 §5.6 "3070 Power limitation switch U16 0xAA ON，0x55 OFF(Power to 100%)(for 3052 and 3081 Reg)" |
| Absolute power limit (not used) | 3081 | 3080 | S16, 10 W | p.29 "3081 Limit power actual value S16 10W … Use 06 code to open 3070 Reg, Then set 3081 Reg" |
| Ramp — start-up | 3148 | 3147 | U16, 10000↔100%, range 5–600%, default 16.67% | p.32 "3148 Power ramp rate (Wgra), general … Start up ramp rate" |
| Ramp — up / down | 3149 / 3150 | 3148 / 3149 | U16, "3000<-->30%/min", range 10–100% | p.32, both marked "Only for AUS" |
| Operating status | 3044 / 3072 | 3043 / 3071 | U16, Appendix 2 / 6 | p.12, p.14 |

### The Solis scale, and why 100 raw units per percent

The 3052 row is **internally contradictory**: its *Unit* column says `1%` while
its *Remark* column says `10000<-->100%` (100 raw per percent). The Remark is
the consistent one, and the document confirms it three independent ways:

1. p.26 tag 3051 reactive limitation, same `10000<-->100%` notation, range
   `(-6000 - +6000)` — which is ±60%, and is absurd as ±6000%.
2. p.32 tag 3142 `MaxLeadingVar%`, same notation, "Range：0---60%；Default:30%" —
   30% is raw 3000.
3. p.32 tag 3149, "3000<-->30%/min" — again 100 raw per percent.

The *Unit* column is therefore unreliable across this whole document, and
`raw_units_per_percent = 100` is used.

**This remains the highest-risk single value in the Solis map, and it must be
proved by one write-then-read before any production use.** The reason is that a
scale error here is *symmetric*: the readback decodes with the same wrong scale,
so a wrong scale would be **confirmed** by the readback rather than caught by it.
The only way to catch it is to observe the machine's actual output power change.

### Solis: not documented / left unset

- **Word order for U32.** The manual never states double-word order for numeric
  U32. The only ordering statement in the document is the serial-number example
  on p.13 (tag 3061 "SN High 4" … tag 3064 "SN LOW 4"), i.e. most significant
  word at the lower address. `INVERTER_WORD_ORDER_AB` is used on that basis and
  the lab simulator agrees, but it is an inference and is a first-read check.
- **Identity expected value.** Not set. p.10 does not settle whether the register
  holds decimal `1020` or `0x1020`: it also says "high 8 bit means protocol
  version, low 8 bit means inverter model", which only reads correctly as
  `0x10`/`0x20`. One read resolves it. Asserting a guess would block all reads.
- **Comms-loss fail-safe.** None for the Modbus control link. Tag 3153 "Internal
  EPM failsafe switch" (p.33) concerns the EPM export meter, not this controller.
- **Settle / response time.** Not documented anywhere. `power_limit_settle_ms`
  left at the firmware default; must be measured at commissioning.
- **Enable-register prerequisite.** The profile struct cannot express "write
  3070 = 0xAA first". See §6.

---

## 3. Growatt

One manual, **two incompatible telemetry maps by product family** — hence two
profiles: `growatt.tl3x.documented` and `growatt.tlx.documented`.

p.3 states the ranges verbatim:

- `TL3-X(MAX、MID、MAC Type)：03 register range：0~124,125~249；04 register range：0~124,125~249`
- `TL-X（MIN Type）：03 register range：0~124,3000~3124；04 register range：3000~3124,3125~3249`

| Item | Register | FC | Type, scale | Citation |
|---|---|---|---|---|
| Active-power-% **WRITE** (both families) | holding **03** | 0x06 | integer %, 0–100 or 255 | p.9 §4.1 "03 Active P Rate / Inverter Max output active power percent / W / 0-100 or 255 / % / 255 / 255: power is not be limited" |
| **READBACK** (both families) | holding **03** | 0x03 | U16, scale 1.0 | p.5 §2 "Function 3 Read holding register" |
| Active power — TL3-X (MAX/MID/MAC) | input **35–36** | 0x04 | U32 AB, 0.1 W → 0.0001 | p.34 §4.2 "35. Pac H Output power (high) 0.1W / 36. Pac L Output power (low)" |
| Applied % — TL3-X | input 113 | 0x04 | 0–100 % | p.37 "113. real Power Percent / real Power Percent 0-100 % MAX" |
| Active power — TL-X / TL-XH | input **3023–3024** | 0x04 | U32 AB, 0.1 W → 0.0001 | p.50 table headed "Use for TL-X and TL-XH": "3023 Pac H Output power 0.1W / 3024 Pac L" |
| Applied % — TL-X / TL-XH | input 3101 | 0x04 | 1%, 1~100 | p.53 "3101 RealOPPercent Real Output power Percent 1% 1~100" |
| Setpoint persistence | holding 02 | — | 1/0 | p.9 "02 PF CMD memory state / Set Holding register3,4,5,99 CMD will be memory or not" |
| Ramp (unusable, see below) | holding 20, 21 | — | 1–1000, 0.1% | p.10 "20 wPowerStartSlope Power start slope W 1-1000 0.1%", "21 wPowerRestartSlopeEE Power restart slope" |
| Export limit path (not used) | holding 122, 123 | — | 0.1% | p.15 "122 ExportLimit_En/dis", "123 ExportLimitPowerRate … -1000~+1000 0.1%" |
| Identity | holding 43 | 0x03 | DTC | p.11 "43 DTC Device Type Code &\*6"; note &\*6 on p.60 |

**Word order is documented** for Growatt: the registers are literally named
`Pac H` / `Pac L` with `H` at the lower address → AB.

**Documented timing constraint** (p.8): "Minimum CMD period (RS485 Time out):
850ms. Wait for minimum850ms to send a new CMD after last CMD. Suggestion is
1s". `telemetry_poll_ms = 1000` for that reason. Note the firmware issues more
than one transaction per poll, so a real RS-485 segment may still need the poll
period raised — that is a lab measurement, not a manual value.

Which family the site has determines which profile is correct. A 100 kW
commercial unit is a MAX-class TL3-X machine; the lab simulator implements the
TL-X map. **This is an owner decision, not a value to interpolate.**

### Growatt: UNRESOLVED and potentially blocking — the write lock

Note `&*7` on p.61, quoted in full:

> "Grid network power control command password: Inverter is in lock state after
> power on; change the power control by network command should unlock inverter
> first; default pw is XXXXXX; Unlock: send 0 to 3-135, then send password to
> 3-136~138; inverter will auto lock in 5min after unlocked; Change PW: unlock
> first, then send 1 to 3-135, then send new password to 3-136~138; Lock: send 0
> or 2 to 3- 135;"

Three problems, all of which need Growatt to answer:

1. **The password is redacted in the manual** (`XXXXXX`).
2. **The "3-135" notation is never defined in the document.** Holding register
   135 is "BLVersion3 … Reserved" (p.16), so it cannot be resolved as
   function-3-register-135. Input registers 3135–3138 are AC-charge energy
   counters (p.55), so it is not that either.
3. **Nothing in the extractable text says which registers the note applies to** —
   the `&*7` reference marker did not survive text extraction, so we cannot tell
   whether holding 03 is covered.

If it does cover holding 03, writes will be silently rejected — or accepted and
then auto-relocked after 5 minutes, which is worse. **No unlock sequence is
attempted in the firmware.** This is a hard blocker for commanding a Growatt on
real equipment and must be resolved with the manufacturer.

### Growatt: not documented / left unset

- **Ramp rate.** Holding 20/21 give a slope value 1–1000 in 0.1% but **no time
  base**, so no rate can be stated. Not written.
- **Comms-loss fail-safe.** Holding 42 "bfailsafeEn; G100 fail safe" (p.13) is
  the G100 *export-limitation* fail-safe tied to the export meter, not a watchdog
  on this controller's link. No control-link watchdog is documented. Not used.
- **Identity expected value.** Not set. The DTC table (note &\*6, p.60) is
  per-model and is elided with "……" for exactly the commercial types; holding
  28/29 "Inverter Module H/L" (note &\*5, p.60) is a packed bitfield, not a family
  constant. No expected value is assertable without the site's model.
- **Settle time.** Not documented.
- **Readback tolerance.** Set to 0.6% because the command quantisation is a whole
  percent: a request of 47.4% is written as 47, and a tighter tolerance would
  fault a perfectly accepted setpoint. This is derived from the documented
  integer range, not guessed.

---

## 4. Sungrow

Profile id `sungrow.string.documented`.

| Item | Manual tag | PDU / FC | Type, scale | Citation |
|---|---|---|---|---|
| Device type code (identity) | 5000 (3X) | 4999, FC 0x04 | U16, Appendix 6 | p.5 §3.1 "7 Device type code 5000 U16 See Appendix 6" |
| Active power output | 5031–5032 (3X) | **5030**, FC 0x04 | U32 **BA**, W → 0.001 | p.7 §3.1 "31 Total active power 5031 - 5032 U32 W" |
| Active-power-% **WRITE** | 5008 (4X) | **5007**, FC 0x06 | U16, 0.1% → 10 raw/% | p.16 §3.2 "9 Power limitation setting 5008 U16 See Appendix 6 0.1% / Available when the power limitation switch (5007) is enabled" |
| **READBACK** of the write | 5008 (4X) | **5007**, FC 0x03 | U16, scale 0.1 | p.5 §3: "Address of 4x type is holding register, supporting the CMD code inquiry of 0x03, and CMD codes write-in of 0x10 and 0x06" |
| Limit **enable** switch | 5007 (4X) | 5006, FC 0x06 | 0xAA Enable / 0x55 Disable | p.16 §3.2 "8 Power limitation switch 5007 U16 0xAA: Enable; 0x55: Disable" |
| Absolute power limit (not used) | 5039 (4X) | 5038 | U16, 0.1 kW | change log p.1 "5039–Power limitation adjustment"; Appendix 6 gives the 0.1 kW range |
| Export power limitation (not used) | 5010, 5011 (4X) | 5009, 5010 | 0xAA/0x55; 0–rated | p.16 §3.2 |
| Work state | 5038 (3X) | 5037 | U16, Appendix 1 | p.7 §3.1 "36 Work state 5038 U16 See Appendix 1" |

### Sungrow word order is DOCUMENTED and is little-endian

p.4 §4, verbatim:

> "U32: 32-bit unsigned integer; little-endian for double-word data. Big-endian
> for byte data.
> S32: 32-bit signed integer; little-endian for double-word data. Big-endian for
> byte data.
> Example: transmission order of U32 data 0x01020304 is 03, 04, 01, 02"

Least significant word first → `INVERTER_WORD_ORDER_BA`. This is the one brand
where the word order is stated outright rather than inferred, and it is the
**opposite** of the other three. See §5 — the lab simulator disagrees with the
manual here, and getting it wrong misreads 100 kW by orders of magnitude.

### Sungrow range

Appendix 6 (p.28–29) gives the power-limited range **per model** in 0.1%:
`0-1000` (SG30CX, SG125HV, SG100CX, SG75CX), `0-1100` (SG33CX, SG40CX, SG50CX,
SG110CX, SG80KTL-M, …), `0-1110` (SG250HX, SG250HX-US), `0-1250`
(SG250HX-IN). `0–100%` is inside every one of them and is the safe common
subset, so that is what the profile sets. Overload scheduling above 100% exists
(change log V1.1.35, "Add 100% Scheduling to Achieve Active Overload") but is
model-specific and is deliberately **not** enabled.

### Sungrow: not documented / left unset

- **Ramp / gradient.** None. No ramp, gradient or rate-of-change register appears
  anywhere in the document (searched for gradient/ramp/rate/slope).
- **Comms-loss fail-safe.** None documented.
- **Settle time.** Not documented.
- **Identity expected value.** Not set: Appendix 6 assigns a distinct code per
  model (SG110CX `0x2C06`, SG250HX `0x2C0C`, SG100CX `0x2C12`, SG33CX `0x2C00`,
  …). There is no family-wide constant, so no expected value is assertable
  without the site's model.

---

## 5. Chint / CPS

Profile id `chint.cps.sch100_125ktl.documented`. This is the best-documented of
the four brands: explicit 0-based addressing, an exact model-class identity
constant, and a single-register active power with no word order to get wrong.

| Item | Register | FC | Type, scale | Citation |
|---|---|---|---|---|
| Device type (identity) | **0x0000** | 0x04 | U16, expect **0x4035** | p.10 §1: "Device uint16 … This register value represents the type of device. 0x4035：100(125) kW_1500V inverter" |
| Model string | 0x000A–0x0013 | 0x04 | String20 | p.11 "These 10 registers represent the model of the device … e.g. SCH125KTL-DO/US-600" |
| Active power output | **0x001D** | 0x04 | U16, 0.1 kW → 0.1 | p.11 "0x001D … Pac uint16 0.1kW kW -1 … AC active power" |
| Active-power-% **WRITE** | **0x1001** | 0x06 | U16, 0.1% → 10 raw/%, 0–1000 | p.16 §2 "1). Power dispatching": "0x1001 … PSet uint16 0.1% … 0 … 1000 … Remote electric dispatch Active Power setting value" |
| **READBACK** of the write | **0x1001** | 0x03 | U16, scale 0.1 | same row is marked `RW`; the section header states "Modbus function code = 0x03.0x06" |
| Dispatch-mode **enable** | 0x2602 | 0x06 | 0/1/2 | p.32 "CtrModeActivePw … The control mode of active power. 0: Disable dispatch mode. 1: Remote dispatch mode. 2: Local control." |
| On/off | 0x1000 | 0x06 | 0xAAAA on / 0x5555 off | p.16 |
| Alternative remote setpoint | 0x2708 | 0x06 | U16, 0.1%, 0–1100 | p.38 "PSetPercentRemote … Remote electric dispatch Active Power setting value" |
| Local (NOT remote) setpoint | 0x250E | 0x06 | U16, 0.1%, 0–1100 | p.28 "Percentage … Local electric dispatch Active Power setting value" |
| Korea-only write block | 0x07D0–0x07D3 | 0x06 | 0x07D3 = "Active power reference" 0.1%, 0–1100 | p.15–16 "1.2. Holding Registers Data Mapping(Only for Korea)" |

### CPS: NOT DECIDED — 0x1001 vs 0x2708

Exactly the same shape of problem as Huawei 40125 vs 40199. Two registers carry
the identical description "Remote electric dispatch Active Power setting value",
in the same units, with different ceilings (0x1001 max 1000 = 100%, 0x2708 max
1100 = 110%). `0x1001` is used because it sits in the dedicated "Power
dispatching" block and its documented maximum is exactly 100%. **Which one the
firmware version on site honours must be confirmed before promoting the
profile.** Do not assume both are live.

Also note the third trap: `0x250E` is the **local** dispatch setpoint, and
`0x07D3` is in a block explicitly titled "Only for Korea". Neither is the remote
control path.

### CPS: not documented / left unset

- **Ramp / gradient.** No dispatch ramp-rate register is documented. The nearest
  items are p.27 `0x2505 NormSoftStartT, 1s, "Normal time in soft startup"`,
  `0x2504 NormSoftStopT` and `0x2506 NormDeratingStep, 0.01%, "Normal power
  derating step"` — a step *size* and start/stop *times*, not a rate limit on a
  dispatch setpoint. None are written.
- **Comms-loss fail-safe.** None documented.
- **Settle time.** Not documented.

---

## 6. Cross-check against the lab simulator — DISAGREEMENTS

Simulators read: `D:\Working\SolTrix-ESP-Lab-Validation\inverter-simulator\src\profiles\{solis,growatt,sungrow,chint-cps}.js`
plus `register-bank.js` and `modbus-server.js`. These were read, not executed.

Relevant simulator mechanics, established by reading the source:

- `RegisterBank.writeRegisters()` **rejects** a write to an address with no
  registered handler (`ILLEGAL_DATA_ADDRESS`). So a simulator that lacks a
  handler fails a write **loudly**, which is good.
- `RegisterBank.setU32()` and `setI32()` always write the **high word first**,
  unconditionally, with no per-profile word order.
- `modbus-server.js` serves FC 0x04 from `inputBank` when the profile declares
  one, otherwise from the same bank as FC 0x03.

### 6.1 Solis — MAJOR disagreement on the command register itself

The simulator's only write handler is
`holdingBank.onWrite(protocolAddress(3080), 1, …)` with
`state.percentLimit = values[0] / 10` — i.e. it treats **manual tag 3080** as a
percent×10 setpoint.

Manual tag 3080 (p.28) is **"Power control word"**, a bitfield:
"Bit0 --- Max power limit flag：0 --- Default is 1.09 rated P；1 --- Set as 1.1
rated P". It is not a percentage setpoint at all. The manual's percentage
register is tag 3052 (§5.6, p.26) with 100 raw units per percent.

Two independent errors in the simulator: **wrong register**, and **wrong scale**
(×10 instead of ×100). It also writes tag 3050 "Power limit actual" as a bare
percent, where the manual says `10000<-->100%`.

**Trust the manual.** The manual states the register's name and semantics
directly; the simulator is a model written without it. Consequence: the Solis
profile **cannot be lab-validated as it stands** — a write to PDU 3051 will be
rejected by the simulator with exception 02, because that address has no handler.
The simulator needs a `3052` handler with the ×100 scale before Solis lab
validation is possible.

Third, smaller simulator bug: `defineManualRange(inputBank, 35000, 20)` applies
the −1 offset to the type-info register, but §5.2 says that table needs **no**
offset. The manual and the simulator disagree by one register there too.

### 6.2 Sungrow — MAJOR disagreement on U32 word order

The simulator writes total active power with `setU32(inputBank, 5031, …)`, which
emits the **high word first**. The Sungrow manual (p.4) states U32 is
**little-endian for double-word data** with a worked byte-order example.

Concretely: 100 000 W is `0x000186A0`. Manual order on the wire is
`0x86A0, 0x0001`. The simulator sends `0x0001, 0x86A0`. A controller decoding
per the manual (`BA`) would read the simulator's frame as `0x86A00001` ≈ 2.25
billion W.

**Trust the manual** — it gives an explicit worked example, which is the
strongest form of evidence in any of these four documents. The profile is set to
`BA`. The practical effect is welcome: the mismatch is enormous and will be
caught in the first second of a lab run rather than on site.

Everything else about the Sungrow simulator agrees with the manual: the −1
offset, the 5007 enable (0xAA/0x55), the 5008 setpoint at 0.1%, and the 5008
readback. Sungrow is the one brand whose **write path** can be lab-validated
today.

One minor disagreement: the simulator writes device type code `1` at tag 5000.
No Sungrow model has code 1 (Appendix 6). Harmless here only because this
profile sets no identity probe.

### 6.3 Growatt — the write register agrees; the telemetry map is family-dependent

The simulator's `bank.onWrite(3, 1, …)` with `values[0] === 255 ? 100 : …`
**agrees with the manual exactly** — holding register 03, 0-based, integer
percent, 255 meaning unlimited. That is a genuine independent confirmation of
both the address and the 0-based convention.

The simulator's telemetry, however, is the **TL-X / TL-XH** map: output power at
`3023/3024` and applied percent at `3101`, both of which are exactly what p.50
and p.53 specify for TL-X. So:

- `growatt.tlx.documented` matches the simulator on all four values and can be
  lab-validated today.
- `growatt.tl3x.documented` (input 35/36) will read **0 kW** against the
  simulator, because the simulator defines registers 0–124 but never writes 35 or
  36. That is a *silent* wrong value rather than a loud failure, and is the one
  cross-check result here that could mislead a lab run.

**Trust the manual for both**: p.3 assigns the two ranges to two named product
families, and neither profile is wrong — they describe different machines. The
simulator simply only models one of them.

The simulator also implements no lock/password mechanism, so a lab run cannot
tell us anything about the `&*7` write lock described in §3.

### 6.4 Chint / CPS — MAJOR disagreement on the command register

The simulator's only write handler is `bank.onWrite(0x07D1, 1, …)` with
`state.percentLimit = values[0] / 10`.

Manual `0x07D1` (p.15) is **"Operation and mod"**: "0 : Unit alone / 2 or 5 :
connection (2 : power factor control operation, 5 : Q(V) operation)". It is a
mode selector in the **Korea-only** holding block. It is not an active-power
percentage register. Even within that Korea block, the active-power register is
`0x07D3`, not `0x07D1`.

**Trust the manual.** Consequences for lab validation, both loud:

1. The simulator does not define `0x1001` at all
   (`defineRange` covers `0x0000–0x003F`, `0x03F0–0x03FF`, `0x07D0–0x07DF`,
   `0x8200–0x8231`, `0x8400–0x840F`), so a write to `0x1001` returns exception
   02, and so does the readback.
2. The simulator defines input register `0x0000` but never writes it, so it reads
   `0`, and the identity probe (which expects `0x4035` per the manual) will fail.

So the CPS profile **cannot be lab-validated at all** until the simulator gains
the `0x1000/0x1001` power-dispatching block and sets `0x0000 = 0x4035`. The
identity probe is nevertheless set from the manual, because `0x4035` is stated
verbatim as the code for precisely the 100(125) kW 1500 V machine class this
document covers — it is the strongest identity evidence available for any of the
four brands, and weakening it to match a simulator gap would be backwards.

`0x001D` Pac agrees between the manual and the simulator (0.1 kW, one register).

### 6.5 Summary of the cross-check

| Brand | Identity | Active power | Write register | Readback | Lab-validatable today? |
|---|---|---|---|---|---|
| Solis | n/a (unset) | agrees | **DISAGREES** (sim uses tag 3080 ×10; manual tag 3052 ×100) | disagrees | **No** — write rejected by the simulator |
| Growatt TL-X | n/a (unset) | agrees | **agrees** | agrees | **Yes** |
| Growatt TL3-X | n/a (unset) | reads 0 (sim models TL-X) | agrees | agrees | Write yes, telemetry no |
| Sungrow | n/a (unset) | **DISAGREES** (word order) | agrees | agrees | Write yes; telemetry will read absurd until the simulator is fixed |
| Chint/CPS | **DISAGREES** (sim leaves 0x0000 = 0) | agrees | **DISAGREES** (sim uses 0x07D1; manual 0x1001) | disagrees | **No** — register absent from the simulator |

In every disagreement the recommendation is the same: **trust the manual**. The
manuals state register names, semantics, units and — for Sungrow — a worked
frame. The simulator is a model built to exercise the controller, and in these
four cases it was evidently written from something other than these documents.
The simulator should be corrected to match; the corrections needed are listed
above and are outside this change's ownership.

---

## 7. Items that require the physical machine and cannot be closed from paper

1. **Solis command scale (×100 vs ×10).** Symmetric through the readback, so
   only the machine's actual output proves it. Highest-risk item in this change.
2. **Solis U32 word order** for active power (inferred from the SN example only).
3. **Solis identity value at 35000** — decimal `1020` or `0x1020`.
4. **Growatt 0-based addressing** — no worked frame in the manual.
5. **Growatt write lock / password (&\*7)** — password redacted, register
   notation undefined, applicability to holding 03 unknown. Needs the
   manufacturer, not the machine.
6. **Growatt product family** — TL3-X vs TL-X decides which profile is correct.
7. **Growatt achievable poll period** on the real RS-485 segment, given the
   documented 850 ms minimum command period and more than one transaction per
   poll.
8. **Sungrow model** — decides the device type code, and the >100% range.
9. **CPS 0x1001 vs 0x2708** — which the site firmware honours.
10. **Every settle time.** No manual documents how long a percentage command
    takes to appear in its readback. `power_limit_settle_ms` is unset for all four
    brands. It must be measured, and until it is, a slow device can latch a false
    confirmation fault.
11. **The enable-register prerequisite for three of four brands** (see §8).
12. **Operating-status code tables.** Solis Appendices 2/6 and Sungrow Appendix 1
    exist in these documents, but no status register is configured for any
    profile — that is a separate, deliberate decision recorded in
    `inverter_profiles.c`.

## 8. Decisions that are the owner's, not this change's

1. **The enable-register gap.** Solis (tag 3070 = 0xAA), Sungrow (tag 5007 =
   0xAA) and CPS (0x2602 = 1) each require a prerequisite register before their
   percentage setpoint has any effect. `inverter_profile_t` has **no field** for a
   prerequisite write, so this firmware cannot set them. Today that means: a
   command is issued, the readback echoes it, the write is reported CONFIRMED —
   and the inverter ignores it because dispatch mode was never enabled. That is a
   *silent* failure mode and it is the most serious structural finding in this
   work. Either the struct gains an enable-register field, or enabling becomes a
   mandatory, checked commissioning step. Choosing which is not a transcription
   decision.
2. **Growatt: which family profile ships**, or whether both stay in the picker.
3. **Whether to pursue the Growatt write lock** with the manufacturer before
   Growatt is offered as a supported brand at all.
4. **Whether the simulator gets corrected** (Solis 3052, CPS 0x1001 + 0x0000,
   Sungrow U32 word order, Growatt TL3-X telemetry) so that Solis and CPS can be
   lab-validated before travel. Without that, two of the four brands go to site
   with paper evidence only.
5. **Sungrow >100% overload scheduling** — documented, model-specific,
   deliberately not enabled here.
