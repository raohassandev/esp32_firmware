# Brand register evidence, round 2: FoxESS, GoodWe, Knox/AISWEI — and why SolarEdge, SMA, Solax, SAJ and Fronius were refused

Companion to `docs/BRAND_REGISTER_EVIDENCE.md`, which covers Solis, Growatt,
Sungrow and Chint/CPS. Same rules apply here:

Status of every profile added or populated by this work: **DOCUMENTED**. Nothing
here has been exercised against physical equipment, and nothing here has been
exercised against the lab simulator either. No profile is production-approved,
and `tests/inverter_write_permission_test.c` executes the write gate over the
whole catalogue to prove none can command production.

This document exists so a reviewer can **check** the register maps in
`components/inverter_manager/inverter_profiles.c` rather than trust them. Every
value carries a file and page/section citation. Where a manual does not state
something, that is recorded as *not documented* and the corresponding profile
field is left unset — it is not interpolated. Word order, scale, data type,
address and timing are all in that category.

---

## 0. Where the manuals actually are

The task for this work stated that the manuals had moved to `D:\Working\Manuals`.
**They have not.** That directory exists and contains exactly one file:

```
D:\Working\Manuals\Analyzers\EM500 Register Data-New (1).pdf
```

which is an energy-meter register map, not an inverter manual. The previously
cited path `D:\Working\SolTrix-ESP-Lab-Validation` no longer exists. The inverter
manuals are in three unrelated trees, none of them `D:\Working\Manuals`:

| Tree | Contents |
|---|---|
| `D:\Working\KC_PV_DG\docs\` | `Inverter\` (Knox, SMA, Solar edge, Solax, CPS, Chint, Huawei, Growatt, Solis, Sungrow), `SAJ\`, `Frounus\` |
| `D:\Working\Previous AMX PV DG Programs\amx-pv-dg\Manuals\` | superset of the above, plus the **only GoodWe manual on the machine** |
| `D:\Working\FUXA SADA\FUXA-1.3.2\server\docs\manuals\` | the **only genuine FoxESS manuals on the machine** (a different project's tree) |

> **Recommendation to the owner:** consolidate these into one path and record it,
> because a manual that cannot be found is a manual that gets guessed from.

Manuals used by this round:

| Brand | File | Document identity |
|---|---|---|
| FoxESS | `FUXA SADA\FUXA-1.3.2\server\docs\manuals\FoxESS-Modbus-Protocol-V1.05.03.00.pdf` | *FOX commercial inverter Modbus interface definition description*, FoxESS Co. Ltd, document version V1.05.03.00, release date 2025-01-15, 50 pages |
| GoodWe | `Previous AMX PV DG Programs\amx-pv-dg\Manuals\GoodWe_grid-tied_GT-series_Modubus_Protocol(8).pdf` | *GoodWe Modbus Protocol of Inverter — GT Series (Customer Version)*, V1.0, 2023-08-25, 58 pages |
| Knox / AISWEI | `KC_PV_DG\docs\Inverter\Knox\MB001_ASW GEN-Modbus-en_V2.1.5(2).pdf` | *Technical Information — AISWEI Interface (Based On Modbus Standard Protocol)*, MB001_ASW GEN-Modbus-en_V2.1.5, © 2022 AISWEI Technology Co. Ltd, 44 pages |

Page numbers are **PDF page numbers** from `pypdf` extraction. The FoxESS PDF's
printed footer version string (`V1.02.00.00`) disagrees with its own title page
and change record (`V1.05.03.00`); the title page is treated as authoritative and
the discrepancy is recorded rather than resolved.

---

## 1. The brand correction that mattered most

The catalogue previously carried a single profile whose manufacturer string was
`"FoxESS / Knox"`. **These are two different manufacturers with two completely
different register maps.**

- **FoxESS** (FoxESS Co. Ltd) — percentage setpoint at address **49007**, gain 10.
- **Knox** — rebadged **AISWEI Technology** ASW hardware. AISWEI's own manual is
  the source; the manufacturer's-name register (printed 31057, p.5) returns
  `"AISWEI"`. Percentage setpoint at printed **45403** → PDU **5402**, gain 0.01.

Had the single joint profile been populated from the Knox manual (which is what
the round-2 task originally directed, and which was the only "Knox/FoxESS" manual
believed to exist), an operator selecting *FoxESS* would have been given AISWEI's
addresses and AISWEI's scale. A wrong address on a 100 kW machine thousands of
miles away is the exact failure this catalogue exists to prevent.

They are now two separate entries: `foxess.commercial.pending` (from the FoxESS
manual) and a new `knox.aiswei.asw.documented` (from the AISWEI manual).

---

## 2. Addressing convention — stated per brand, because it is not uniform

All addresses in `inverter_profiles.c` are **0-based PDU addresses as they go on
the wire**. This is the trap that has bitten this project repeatedly, so it is
recorded first, with the strength of the evidence graded honestly.

| Brand | Manual convention | Evidence | Conversion applied | Strength |
|---|---|---|---|---|
| **GoodWe** | already PDU | §9 Example 1, p.57: `Host sends: F7 03 7D 55 00 01 98 E0` reads *grid frequency*, whose table row is `32085 grid frequency RO U16 1 100 Hz`. `0x7D55 == 32085`. | none | **Worked byte-level frame. Proven.** |
| **Knox / AISWEI** | strip leading `3`/`4`, then −1 | §3.1, p.3: "Decimal Modbus address, you need to remove 3x or 4x and subtract 1, then convert to hexadecimal and use it in the communication frame. Such as 31001 (decimal) → 1000 (decimal) → 0x03e8 (hexadecimal)" | PDU = (printed − 30000 or 40000) − 1 | **Explicit rule plus a worked conversion. Proven.** |
| **FoxESS** | assumed already PDU | §5.3.2, p.44 (exception 0x02): "For a controller with 100 registers, the first PDU address is 0, and the last one is 99." Plus: the map's first row is `Model name … 30000` — address **30000**, not 30001, and a 1-based `3x` reference convention cannot produce 30000. | none | **DEDUCTION ONLY. No offset rule is stated and no worked data frame exists.** |

### FoxESS is the weak link, and it is the one thing to prove first

The FoxESS manual never states an offset rule and never prints a byte-level frame
for a data register. The only numeric frame in the document is §5.3.6 p.49,
"Configure slave address", which uses register address `0x5A5A` directly — but
that is a special command address, not a row of the data map, so it does not
prove the data map's convention.

**This is the single addressing item to prove with the first read**, and it is
part of why `foxess.commercial.pending` may not command real equipment yet. If
the convention turns out to be 1-based, every FoxESS address in the profile is
off by one — and 49006 is "None Power compensation (Q/S)", a *reactive power*
register. Writing an active-power percentage into a reactive-power register is a
live hazard, not a cosmetic error.

---

## 3. FoxESS — `foxess.commercial.pending`

Manual **sufficient** for a command/readback pair. Populated.

| Item | Manual address | PDU / FC | Type, scale | Citation |
|---|---|---|---|---|
| Active-power % **WRITE** | 49007 | **49007**, FC 0x06 | I16, gain 10 → 10 raw per percent | §3 table row 335, p.30: "[Power grid dispatch] Active power percentage derating (0.1%), RW, I16, %, Gain 10, 49007, 1, Range:[0, 100.0]", remark "Active power fine adjustment interface" |
| Active-power % **READBACK** | 49007 | **49007**, FC 0x03 | I16, scale 0.1 | Same row (`RW`); §5.3.1 p.44 lists 0x03 "Read register — Supports single and multiple register sequential reads" |
| Function codes | — | 0x03 / 0x06 / 0x10 | — | §5.3.1, p.44, Table 5-2 |
| Active power output | 39134 | *left unset* | I32, kW, gain 1000 | §3 table row 158, p.16: "Active power, RO, I32, kW, 1000, 39134, 2" |
| Identity | 30000 | *left unset* | STR, 16 registers | §3 table row 1, p.5: "Model name, RO, STR, 30000, 16" |

**Scale is documented twice over, not interpolated:** the signal name itself says
`(0.1%)`, and independently `Gain 10` with `Range:[0, 100.0]` puts raw 1000 at
100.0 %. Both give 10 raw units per percent.

### FoxESS: not documented / left unset

- **Word order of 32-bit values — not documented.** There is no endianness
  statement anywhere in the document, no High/Low word naming in the map, and no
  worked multi-register example. `active_power_address` is therefore **left
  unset** even though address, type and scale are all known, because a reversed
  word order turns 100 kW into a nonsense number. One read of 39134 against a
  known output closes this and active power can then be added. The command path
  is unaffected — 49007 is a single 16-bit register.
- **Identity expected value — not available.** 30000 is a 16-register ASCII
  string, not a numeric family constant, and this firmware's identity probe is a
  masked integer compare.
- **Ramp / gradient — not documented.** No active-power rate register exists in
  this map. 49008 "Fixed Active power derated (W)" and 49136 "Grid point power
  limit" are absolute-watt limits, not rates.
- **Comms-loss fail-safe — not documented for the percentage path.** Address
  46002 "Remote Timeout_Set" (U16, s) and 46007 "Remote Timeout Countdown" (RO, s)
  belong to the separate *Remote Control* watt group (rows 271–275, p.23), which
  commands in watts at 46003. Not used.
- **Settle time — not documented.** Must be measured at commissioning.
- **Minimum command interval — not documented.**

### FoxESS: prerequisite enable — NOT set, and here is the reasoning

No enable, unlock or dispatch-mode register is documented for 49007. The
`[Power grid dispatch]` group (49005–49010) contains no such register. By
contrast the separate *Remote Control* group does have one — row 271, p.23,
address 46001, `Bitfield16`, "Bit0: Remote Control enable, 0:Disable 1:Enable" —
but that group commands in watts at 46003 and is not the path used here.

There is a residual risk worth naming. §5.3.2 p.44 defines exception code
**0x80 "No permission — Authentication fails or permissions expire after timeout,
and operation is prohibited."** The document never says what that authentication
is, nor which registers it guards. **It is deliberately not treated as a
`requires_prerequisite_enable` case**, because if it applies the write is
*rejected with an exception* — it fails loudly, and this firmware surfaces that as
a write error. `requires_prerequisite_enable` exists for the opposite and far
worse failure: a device that silently accepts and echoes a setpoint it is
ignoring, producing a CONFIRMED verdict for a limit that is not in force. Setting
the flag for a loud failure would misreport the reason for a refusal and erode the
flag's meaning. **If a lab write to 49007 returns 0x80, FoxESS must be asked what
unlocks it, and the flag set then.**

---

## 4. GoodWe — `goodwe.commercial.pending`

Manual **sufficient**, and it is the best-evidenced map in the catalogue: the
addressing convention *and* the gain convention are both proven by one worked
frame. Populated.

| Item | Manual address | PDU / FC | Type, scale | Citation |
|---|---|---|---|---|
| Active power output | 32080 | **32080**, FC 0x03 | S32, 2 reg, gain 1000, kW → scale 0.001 | register table: "32080 active Power RO S32 2 1000 kW" |
| Active-power % **WRITE** | 42407 | **42407**, FC 0x06 | U16, gain 10 → 10 raw per percent | register table p.12: "42407 Active power percentage derating(0.1%) RW U16 1 10 %Pn [0,1100]" |
| Active-power % **READBACK** | 42407 | **42407**, FC 0x03 | U16, scale 0.1 | Same row (`RW`); §3.1 p.4 "Read the Content of Register (Function code: 03H)" |
| Ramp / gradient | 42433 | *not written* | U32, 2 reg, gain 10, %Pn/min, [1,6000000] | register table: "42433 Active power gradient RW U32 2 10 %Pn/min [1,6000000]" |
| Word order | — | AB | most significant word first | p.4 §2: "Long Integer Data 2 4 Two separate forwarding, from the most significant bit to the least significant bit" |

**Gain convention proven:** §9 Example 1 p.57 — "The frequency value received is
0x1388 (decimal 5000). After dividing by a gain factor of 100, the frequency
value is 50.00 Hz." So real value = raw ÷ Gain, throughout the table.

### GoodWe: EEPROM WEAR — a hard blocker for production approval

The remark column on 42407, and on **every** register in the 424xx block, reads:

> **"Storage, does not support high-frequency write operations"**

42407 is flash-backed. This controller's entire purpose is to move a setpoint
continuously against a moving generator load, and its default control period is
250 ms. Writing a flash-backed register at that rate destroys the inverter's
memory — a permanent hardware failure on a customer's machine, caused by the
controller working exactly as designed. This is invisible in the register numbers
and would not be caught by any readback check, because every individual write
succeeds.

The manual states the prohibition but gives **no permitted write rate and no
write-cycle budget**. Accordingly `min_command_interval_ms` is **left unset**:
inventing a timing number is precisely what must not happen here, and timing is
explicitly one of the values that may not be interpolated.

**Owner decision required.** Before GoodWe can be considered for production
approval, one of these must happen:
1. GoodWe supplies a permitted write rate / write-cycle budget for 42407; or
2. a deadband + minimum-interval strategy is agreed and implemented, so the
   register is written only on a material change rather than every control cycle; or
3. a non-flash dispatch path is obtained from GoodWe for the GT series.

Note this is **not** the accept-and-echo hazard, so
`requires_prerequisite_enable` is not set — using that flag for a different
hazard would misreport why a profile was refused. There is currently **no field
in `inverter_profile_t` that can express "this register is flash-backed"**, which
is itself worth fixing; see §9.

### GoodWe: range held at 100 %, deliberately

The manual's range for 42407 is `[0,1100]`, i.e. 0–110 %. `maximum_percent` is
held at **100**. Nothing in this document says a GT-series machine may be
scheduled into overload, and 110 % is not a number to hand a 100 kW inverter on
the strength of a range column alone.

### GoodWe: the write function code is the one thing to confirm

The manual demonstrates writes only with **0x10** (§3.2 "Set the Content of
Register (Function code: 10H)", and both write examples in §9 use `10`). This
firmware writes a single register with **0x06**. The `RW` access on 42407 permits
it and 0x06 is a standard single-register write, but **the document never shows
0x06 being used**. Confirm on the first lab write; if it is rejected, the profile
needs `power_limit_function = 16`.

### GoodWe: not documented / left unset

- **Prerequisite enable — none documented.** No enable, unlock, dispatch-mode or
  password register for 42407 anywhere in this map. 42430 "Active power
  compensation response mode" (0 Off / 1 Slope response / 2 Low-pass filter mode
  response) shapes the *response* to a dispatch value; the document does not make
  it a precondition for the value taking effect, so it is not treated as one.
- **Comms-loss fail-safe — not documented.** No watchdog, timeout or
  fallback-limit register for a third-party controller appears in this map. **If
  the link drops, the last written limit simply stands.** For a PV-DG plant that
  means the inverter *holds* its limit rather than reverting — state this to the
  owner explicitly, because a generator-protection scheme usually wants the
  opposite behaviour.
- **Identity — no numeric constant.** 35502 "Device serial" is a string.
- **Settle time — not documented.** Must be measured at commissioning.

---

## 5. Knox / AISWEI — `knox.aiswei.asw.documented` (new profile)

Manual **sufficient** for a command/readback pair. Populated, and **refused write
authority** because of a documented prerequisite enable.

| Item | Printed address | PDU / FC | Type, scale | Citation |
|---|---|---|---|---|
| Active-power % **WRITE** | 45403 | **5402**, FC 0x06 | U16, %Pn, Gain 0.01 → 100 raw per percent | §3.3, p.20: "45403 Active Power Set U16 %Pn 0.01 RW" |
| Active-power % **READBACK** | 45403 | **5402**, FC 0x03 | U16, scale 0.01 | Same row (`RW`); §3.1 p.3 "RW: Read and Write"; §3.6.1 p.35 "Read Holding Register (Function Code: 0x03)" |
| **Prerequisite enable** | 44001 | 4000 | E16, 0 = Disable, 1 = Enable | §3.3, p.16: "44001 Active power control function： 0 = Disable 1 = Enable E16 RW" |
| Active power output | 31371–31372 | *left unset* | S32, W, Gain 1.0 | §3.3, p.9: "31371~31372 Active power S32 W 1.0 RO" |
| Ramp up / down | 45404 / 45405 | *not written* | U16, %Pn/min, Gain 0.01 | §3.3, p.20: "45404 Increase rate of active power U16 %Pn/min 0.01 RW", "45405 Decrease rate of active power U16 %Pn/min 0.01 RW" |
| Grid-connection load rates | 45401 / 45402 | *not written* | U16, %Pn/min, Gain 1.0 | §3.3, p.20 |
| Limiting active? (diagnostic) | 31390 | *not used* | U16 | §3.3, p.9: "31390 Power limitation master-slave status： 0 = Not available（Power limitation disable） 1 = Master（Power limitation enable） 2 = Slave（Power limitation enable）" |
| Function codes | — | 0x03 / 0x04 / 0x06 / 0x10 | — | §3.6, p.35–39 |

**Scale is stated, not interpolated:** §3.1 p.3 defines the Gain column as "Gain
Real value = Gain * output value". With Gain 0.01, real percent = 0.01 × raw, so
raw = percent × 100 → `raw_units_per_percent = 100`.

### Knox: PREREQUISITE ENABLE — set, and this is why it cannot be commanded

Printed address **44001** "Active power control function： 0 = Disable / 1 =
Enable" (§3.3, p.16) governs whether the setpoint at 45403 has any effect. PDU
4000 by the §3.1 rule.

`requires_prerequisite_enable = true` is set, which refuses this profile write
authority in **lab mode as well as production**. The reason is the specific
failure mode: 45403 is an ordinary RW holding register, so it will
**accept the write and echo the value back** whether or not 44001 is enabled.
The controller
would read a matching readback, report **CONFIRMED**, and every layer above it —
including the operator — would be told the plant is limited while the inverter
keeps generating at full output. A false confirmation is worse than a mismatch and
worse than a timeout, because nothing downstream has any way to notice.

This firmware has no field describing a prerequisite write and no code to sequence
and verify one, so it fails closed. Lifting the refusal requires either that
capability, or 44001 becoming a checked commissioning step whose completion the
controller can confirm — note that **31390 makes that verifiable**, since it
reports whether power limitation is actually enabled.

### Knox: not documented / left unset

- **Word order of 32-bit values — not documented.** §3.2 "AISWEI Data Types and
  NaN Values" p.4 lists S32/U32 widths and NaN values but says nothing about word
  order. The document's only ordering statement concerns the two ASCII bytes
  *inside* a String register ("the high 8-bit is the first ASCII character",
  p.4), and there is no worked multi-register example. `active_power_address` is
  therefore **left unset** despite address, type and scale all being known. One
  read of PDU 1370 against a known output closes it.
- **Identity — no usable numeric constant.** Printed 31001 "Device Type: 1=Single
  phase / 3=Three phase" is typed **String** in this manual, not U16;
  31019–31026 "Machine type" and 31065–31072 "Brand name" are Strings; and 31073
  "Inverter model" is an E16 enumeration whose largest commercial code is
  `5-PV Three phase50-60kW` — **which does not cover a 100 kW machine at all.**
  That is a further reason the site's actual model must be confirmed rather than
  assumed.
- **Comms-loss fail-safe — not documented for a third-party control link.** The
  V2.1.5 change record (p.2) adds printed 44029 "the enable/disable of
  communication checking for G100" and 33059 the matching read-back. G100 is the
  UK export-limitation scheme, tied to the export meter, not to loss of this
  controller, and the register overview gives no timeout or fallback-limit value
  for it. Not used.
- **Settle time and minimum command interval — neither documented.**

### Knox: this profile is currently inert, and that is an owner call

With active power unset (word order) and writes refused (44001), this profile can
neither read telemetry nor command. It is carried because it records a genuinely
complete, well-evidenced map and because separating it from FoxESS prevents a real
brand-conflation error. **If the owner would rather not surface an inert entry in
the operator's picker, deleting it loses nothing but this document.** Both
missing pieces are one lab session away.

---

## 6. Brands examined and REFUSED — with exactly what was missing

Nothing was populated for any of these. No profile was added, and no existing
profile was modified.

### 6.1 SolarEdge — manual EXCELLENT; **the firmware, not the manual, is the blocker**

Source: `KC_PV_DG\docs\Inverter\Solar edge\se-modbus-interface-for-solaredge-terramax-inverter-technical-note.pdf`
(*Modbus Interface for the SolarEdge TerraMax™ Inverter — Technical Note*, Version 1.0, May 2024, 21 pages).

This is the best-documented manual in the entire set. It gives everything asked
for, including things no other brand documents:

| Item | Address | Type | Citation |
|---|---|---|---|
| Addressing | dual columns "(base 0)" and "(base 1)" throughout | — | p.10: "The base register of the Device Specific block is set to 40070 (MODBUS PLC address [base 1]), or 40069 (MODBUS Protocol Address [base 0])" |
| Active-power % WRITE + READBACK | **0xF322** | Float32, 2 reg, R/W, 0–100 % | p.14: "F322 2 R/W Dynamic Active Power Limit Float32 0-100 %" |
| Ramp up / down | 0xF318 / 0xF31A | Float32, %/min, −1 = disabled | p.13 |
| **Comms-loss fail-safe** | 0xF310 + 0xF312 | Uint32 s + Float32 % | p.13: "Command Timeout … If the inverter doesn't receive one of the dynamic commands within this time frame, it will revert to the fallback settings"; "F312 2 R/W Fall-back Active Power Limit Float32 0-100 %" |
| Documented command interval | — | — | p.12: "The controller command interval must be at least Command Timeout interval / 2" |
| Active power output | base-0 **40083** | int16 W + scale factor at 40084 | p.10 |

**Two firmware capability gaps make this unrepresentable, and populating it would
be actively dangerous:**

1. **No float32 data type.** `inverter_value_type_t` in
   `components/inverter_manager/include/inverter_profile_decode.h` is
   `{U16, S16, U32, S32}` — there is no IEEE-754 type. The command encoder
   `encode_command()` in `inverter_manager.c` computes
   `raw = llround(percent * raw_units_per_percent)` and writes it as a plain
   integer. Writing 50 % into a Float32 register as integer `0x00000032` is the
   float `7e-44` — effectively **zero percent**. The readback would decode the
   same garbage the same wrong way and could well *confirm* it.
2. **The command encoder hardcodes AB word order.** `encode_command()` emits
   `words[0] = raw >> 16; words[1] = raw;` unconditionally. SolarEdge documents
   the opposite: p.14, "Each 32-bit value spans over two registers in the
   **little-endian word order (LSB-MSB)**", with "The two registers must be
   written together using Modbus function 16." There is no `power_limit_word_order`
   field at all — word order is only modelled on the *readback* side.

   The manual even offers a way out — p.14: "If the controller does not support
   the little-endian word order, another map using the big-endian word order
   correlating to this one exists at an offset of 0x800 from this map" (i.e.
   0xFB22) — but that only fixes word order, not the float32 problem.

**Also, and independently:** the active-power register uses a SunSpec *runtime*
scale factor (`I_AC_Power_SF` at base-0 40084). `active_power_scale` is a
compile-time float, so a runtime SF cannot be honoured. For a 100 kW+ TerraMax the
int16 W value cannot represent the output at SF 0, so SF is certainly non-zero —
but the actual value must be read, not assumed.

**And SolarEdge would be refused write authority anyway**, because it documents a
substantial prerequisite chain (p.12) that this firmware cannot sequence:

1. `0xF142 AdvancedPwrControlEn` → 1 (default 0)
2. `0xF104 ReactivePwrConfig` → 4 (default 0)
3. `0xF100` Commit Power Control Settings → 1 —
   **"This command stops production and restarts the inverter."**
4. initialise `0xF308`–`0xF320`
5. `0xF300 Enable Dynamic Power Control` → 1 (default 0)

Plus p.14: the volatile registers "DO NOT maintain their value following an
inverter restart and must be re-configured after the inverter restarts."

> **Owner decision.** SolarEdge is the strongest candidate for real closed-loop
> control in this set — it is the only brand with a documented comms-loss fail-safe
> and a documented command interval. Unlocking it is a **firmware** task, not a
> documentation task: add a float32 value type, add a command-side word order
> field, add runtime scale-factor support, and add prerequisite-write sequencing.

### 6.2 SMA — register map found, but **no readback evidence, no telemetry register, and an undetermined lock**

Sources:
- `KC_PV_DG\docs\Inverter\SMA\SMA-Modbus-general-TI-en-10.pdf` (*SMA Modbus Interface — general*, 24 pages) — formats and addressing only, **contains no register list**.
- `KC_PV_DG\docs\Inverter\SMA\PARAMETER-STPxx-US-50_03-02-07-R\parameterlist_en.html` — a real Modbus map for Sunny Tripower xx-US-50, firmware 03.02.07.R, with columns "SMA Modbus register address / Name / SMA Modbus access / Unit ID / data type / data format / Resolution / Group / Channel / Value range / Default / Number of combined registers". 456 rows.
- `KC_PV_DG\docs\Inverter\SMA\ennexOS-SunSpec-Modbus-TI-en-10.pdf` (7 pages).
- `KC_PV_DG\docs\Inverter\SMA\SMA STP50-4x en.pdf` (128 pages) — installation manual, no register map.

What **was** established:

| Item | Address | Type | Citation |
|---|---|---|---|
| Addressing (SMA profile) | used directly, no offset | — | `SMA-Modbus-general` §3.5.2 p.10: "The Unit ID of the SMA inverter is requested via the Modbus command Read Holding Registers on the register address 42109 with the Unit ID 1" |
| Addressing (SunSpec profile) | −1 | — | §3.5.3 p.10: "use the register addresses reduced by the offset 1 in each case. Example: … 40001 - 1 = 40000" |
| Byte order | big-endian within a register | — | §3.5.4 p.10: "With data storage in the Motorola format 'Big Endian'…" |
| Access legend | RW = read and write | — | §3.5.5 p.10 |
| Active-power % setpoint | **40016** | S16, FIX0 (resolution 1), 1 register, 0–100 %, `Setpoint.PlantControl.Inverter.WModCfg.WCtlComCfg.WNom` | parameter list row 310 |
| Operating mode | 40210 | default is `1079: External active power setpoint (WCtlCom)` — so **no mode change is needed** to use 40016 | parameter list row 111 |
| Comms-loss fail-safe | 41193 + 41195 | "External active power setting, fallback behavior" (`2506: Values maintained` / `2507: Apply fallback values`) and "External active power setting, timeout" (1 s – 10,000 s, default 600 s) | parameter list rows 112–113 |
| Ramp | 44031 / 44033 | "External active power setting, increase/decrease rate", 0.01 %/s – 1,000 %/s | parameter list rows 178–179 |
| Cyclic writes are SAFE for this parameter | — | `WMaxLimPct` is explicitly excluded from the flash-wear prohibition | `ennexOS-SunSpec-Modbus` p.5: "Cyclical changing of these parameters leads to destruction of the flash memory of the SMA products. The following parameters are excluded… WMaxLimPct" |

**Why it was still refused — three gaps, any one of which is sufficient:**

1. **No readback evidence.** 40016 is `RW`, so it is readable by the general
   document's own access definition — but it is a `Setpoint.PlantControl.*`
   channel, and unlike the `Parameter.*` channels it has **no RO mirror**. (For
   comparison, `Parameter.Inverter.WModCfg.WCnstCfg.WNom` has both an RW register
   at 40214 *and* an RO mirror at 30839.) **Nothing in these documents states that
   reading an SMA setpoint register returns the last commanded value.** The
   project rule is that a command which cannot be confirmed must not be issuable,
   and "probably readable" is not confirmation.
2. **Grid Guard protection status undetermined.** `SMA-Modbus-general` p.8 states
   that a Grid Guard code must be transmitted (register 43090, Unit ID 3) for
   protected parameters, that "If an SMA inverter is restarted during Grid Guard
   mode, the SMA Grid Guard code must be transmitted again", and then defers:
   "For parameters that are Grid Guard-protected, see product pages or Modbus page
   at www.SMA-Solar.com." **The documents present do not say whether 40016 is
   protected.** If it is, that is a prerequisite unlock *that also expires on
   restart*.
3. **No measured active-power register anywhere in the doc set.** The parameter
   list is parameters only; `SMA-Modbus-general` contains no register list; the
   STP50 PDF is an installation manual. The profile would have no telemetry.

> **Owner decision.** SMA is one document away. Obtaining SMA's device-specific
> Modbus assignment table for the site's actual model (which would supply the
> measured-value registers and the Grid Guard protection flags) would likely close
> gaps 1–3 together. Note also that the map found is for **STPxx-US-50** — a US
> 50 kW-class product — and the site is 100 kW; the model match is unconfirmed.

### 6.3 Solax — **no active-power-percentage write register exists**

Source: `KC_PV_DG\docs\Inverter\Solax\Hybrid-X1X3-G4-ModbusTCPRTU-V321-English_0622-pub_240818_001120.pdf`
(*Hybrid X1/X3-G4 Modbus TCP/RTU*, V3.21, 52 pages).

- The **only** percentage register in the document is `0x0025 PowerLimitsPercent`,
  "output power limits precent", range 0–100, uint16 — and its property column is
  **`R` (read-only)**. It is a readback with nothing to write.
- The writable export-control registers are in **watts, not percent**:
  `0x0041 Export control Factory_Limit W (0~60000) 1W uint16` and
  `0x0042 Export control User_Limit W (0~60000) 1W uint16`. This firmware's
  command model is a percentage of rated power; an absolute-watt limit is a
  different quantity and is not interchangeable.
- Both are marked **★**, which p.6 defines as flash-backed: "some registers will
  be write in EEprom if they are changed… But the EEprom has the write times
  limit. **Too frequent operation will lead to irreversible hardware damage.**
  Related registers are marked with ★."
- There is an `0x0000 UnlockPassword W` register, i.e. a prerequisite unlock.
- Documented timing, for the record: p.6, "The least interval time between two
  instructions 1 Sec", "Response timeout 1 Sec".
- Documented word order, for the record: p.6, "32bit data use little endian
  format".

**Also the wrong product class:** this is a *residential hybrid X1/X3-G4*
(single/three-phase, battery, EPS), not a commercial 100 kW string inverter. Even
if a percentage write register existed, this manual would not describe the site's
machine. **A commercial Solax manual is needed.**

### 6.4 SAJ — **the limit register is WRITE-ONLY, so it can never be confirmed**

Source: `KC_PV_DG\docs\SAJ\saj-plus-series-inverter-modbus-protocal.pdf`
(*SAJ Plus Series Inverter Modbus Protocol*, 20 pages).

- §4.6 "Special Registers", p.20: **`801FH 1 LimitPower UInt16 W Limit Power
  percentage`**. The property column value is **`W`**, and §2 p.2 defines the
  legend explicitly: **"Property: R represents read-only; W represents
  write-only."**
- A write-only command register **cannot be read back**. That is a hard refusal
  under both `inverter_profile_write_permission()` and
  `tests/inverter_profile_catalogue_source_contract.py`: a command that cannot be
  confirmed must not be issuable. No search of the document found a separate
  readback of the applied limit.
- The row is also incomplete in two further ways: the **scale factor column is
  blank**, and **no range is given**. The unit/description columns conflict — the
  description says "Limit Power percentage" while the unit column reads `W`. The
  scale of this register is therefore genuinely unknown, and guessing it is
  exactly what must not happen.
- Addressing, for the record, *is* clean: §3.1.1–3.1.2 p.3 show worked frames
  where "register 0X0001" appears on the wire as starting address `00 01`, so
  table addresses are direct PDU. Active power is at `0112H` ("Active power of
  inverter total"). None of that helps without a readback.
- Note also exception code `13 System lock` (§3.1.3, p.4), suggesting a lock
  mechanism the document does not describe.

**What is missing:** a readback register for the power limit, and a stated scale
and range for 801FH. Both must come from SAJ.

### 6.5 Fronius — **there is no Fronius manual on this machine**

The folder `KC_PV_DG\docs\Frounus\` (note the misspelling) contains exactly one
file: `User Manual PV-7200 (1)_240319_235329.pdf`.

**It is not a Fronius document.** Its cover page reads "PLATINUM Series / PV 7200
/ 6 Kilowatts Inverter / Pure Sine Wave / … / Compatible With Lithium ion Battery
/ RGB Lights for Aesthetics / Touch Screen LCD". It is a **Platinum-brand 6 kW
residential hybrid/off-grid inverter** user manual, filed under a misspelled
"Fronius" folder.

It is also an **image-only scan**: `pypdf` extracts 1,241 characters from 35
pages, all of it the cover page. Pages 2–35 yield nothing. There is no Modbus
content, no register map, and no protocol section.

A filesystem-wide search (`find /d/Working -iname '*.pdf'` filtered for
`fronius|frounus`) returned only this file and its duplicates in other project
trees. **A Fronius manual must be obtained before anything can be said about
Fronius.** Nothing was assumed from the Fronius SunSpec implementation known
generally, because that is memory, not evidence.

---

## 7. Summary table

| Brand | Manual sufficient? | Action taken | Blocker |
|---|---|---|---|
| **FoxESS** | Yes (command + readback) | `foxess.commercial.pending` **populated** | Addressing convention deduced, not proven; word order undocumented so no telemetry |
| **GoodWe** | Yes (best evidenced) | `goodwe.commercial.pending` **populated** | Flash-backed command register, no permitted write rate documented |
| **Knox / AISWEI** | Yes (command + readback) | `knox.aiswei.asw.documented` **added** | Prerequisite enable at 44001 → write refused; word order undocumented so no telemetry |
| **SolarEdge** | Yes — fully, better than any other | none | **Firmware** cannot represent Float32 or a command-side word order; runtime scale factors unsupported; large prerequisite chain |
| **SMA** | Partly | none | No readback evidence for a setpoint channel; Grid Guard status undetermined; no measured-power register in this doc set |
| **Solax** | No | none | No active-power-percentage **write** register exists; wrong product class (residential hybrid) |
| **SAJ** | No | none | Limit register is **write-only**; scale and range blank |
| **Fronius** | **No manual exists** | none | The "Frounus" folder holds a mislabelled, image-only Platinum PV-7200 6 kW manual |

---

## 8. Items that require the physical machine and cannot be closed from paper

Nothing in this list can be resolved by reading more carefully. Each needs a live
Modbus transaction against real equipment (or, where noted, a question to the
manufacturer).

**FoxESS**
1. **Addressing convention** — is the map column already 0-based PDU? Read
   address 30000 (16 registers) and check whether an ASCII model name comes back.
   If it does, the convention is confirmed for the whole map. **Highest priority
   FoxESS item**; if wrong, 49007 becomes 49006, a *reactive power* register.
2. **Word order of I32** — read 39134 (2 registers) against a known output; both
   orderings differ by orders of magnitude, so one read settles it. Until then no
   active power.
3. **Does a write to 49007 return exception 0x80?** If yes, ask FoxESS what
   authentication is required and set `requires_prerequisite_enable`.
4. **Function code 0x06 on 49007** — accepted, or does it need 0x10?
5. **Settle time** — how long before 49007 echoes an accepted setpoint.
6. **Scale confirmation** — one write-then-read of a mid-range value (say 47.3 %
   → raw 473). A scale error here is symmetric and would self-confirm.

**GoodWe**
7. **Permitted write rate for the flash-backed 42407** — *a question for GoodWe,
   not a measurement.* Blocking for production approval. See §4.
8. **Function code** — the manual only ever demonstrates 0x10; confirm 0x06 is
   accepted, or switch the profile to 16.
9. **Settle time** — not documented.
10. **Behaviour on link loss** — confirm the last limit holds (no fail-safe is
    documented), and confirm with the owner that holding is acceptable for
    generator protection.
11. **Whether 42430 "Active power compensation response mode" must be non-zero**
    for a dispatch value to take effect. The manual does not say it is a
    precondition; a lab write with it at 0 would confirm.

**Knox / AISWEI**
12. **Word order of S32** — read PDU 1370 (2 registers) against a known output.
13. **Confirm 44001 must be 1 before PDU 5402 takes effect**, and confirm that
    PDU 5402 echoes the setpoint even when 44001 is 0 — that echo-while-ignoring
    is the whole reason for the refusal, and it should be *demonstrated* rather
    than assumed from the map's structure.
14. **31390 as the verification channel** — confirm it reports
    enabled/disabled truthfully, since it is what would make 44001 a checkable
    commissioning step.
15. **The site's actual model** — the manual's model enumeration (31073) tops out
    at "Three phase 50-60kW" and does not cover 100 kW.
16. **Settle time and minimum command interval** — neither documented.

**Cross-brand**
17. **Whether the site's inverters are any of these brands at all.** Every profile
    here is a paper transcription against a manual whose product-family match to
    the installed equipment is unconfirmed.

---

## 9. Decisions that are the owner's, not this change's

1. **`docs/RELEASE_READINESS.md` must be updated by its owner.** This change adds
   one profile and changes the write authority of two others, so
   `tests/release_doc_catalogue_source_contract.py` **fails until the table is
   updated**. That file is not owned by this change. The exact rows required are
   reported with this work.
2. **GoodWe's flash-backed command register.** Blocking for production. Either
   GoodWe supplies a permitted write rate, or a deadband/minimum-interval
   strategy is implemented, or a non-flash dispatch path is obtained. See §4.
3. **There is no field in `inverter_profile_t` for "this register is
   flash-backed".** GoodWe 42407 and Solax's ★ registers both need it, and the
   hazard (permanent hardware destruction) is severe and silent — every individual
   write succeeds. Recommend adding one, rather than relying on this document
   being read.
4. **Whether to keep the inert `knox.aiswei.asw.documented` entry.** It can
   currently neither read nor command. See §5.
5. **Whether to invest in the SolarEdge firmware work.** It is the only brand in
   this set with a documented comms-loss fail-safe *and* a documented command
   interval — i.e. the only one whose manual describes a device designed to be
   driven by a controller like this one. The gaps are all on our side. See §6.1.
6. **Whether to obtain the three missing documents**: SMA's device-specific
   assignment table, a *commercial* Solax protocol, and any Fronius protocol at
   all.
7. **Manual storage.** The manuals are spread across three unrelated trees and the
   path this task was given (`D:\Working\Manuals`) contains none of them. See §0.
8. **`maximum_percent` for GoodWe is held at 100 although the manual permits 110.**
   If the owner wants overload scheduling, that needs per-model evidence.
