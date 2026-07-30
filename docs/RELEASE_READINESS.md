# Release readiness — Automatrix PV-DG Controller

**Commit:** see `git log`; last substantive update covered the acquisition
performance work, prerequisite enable sequencing and Float32 support
**Assessed:** 2026-07-30 (updated the same day; the hardware evidence in section 2
was gathered at `1282af8` and has not been re-run since)
**Target hardware:** ESP32-S3-DevKitC-1 N16R8 (16 MB flash, 8 MB octal PSRAM), ESP-IDF v6.0.1

This document records what has been demonstrated on physical hardware, what has
only been demonstrated in software, and what has not been demonstrated at all.
It exists so that a release decision is made against evidence rather than
against a passing build.

---

## 1. The decisive constraint

**No manufacturer profile is write-qualified. The controller cannot command a
real inverter, and this is by design, not by omission.**

The table below is generated from the compiled catalogue, so it cannot drift from
the code. "Lab authority" is what `inverter_profile_write_permission()` returns
when an endpoint **is** declared a simulator — i.e. the most authority the profile
can ever obtain today.

| Manufacturer | Profile | Qualification | Lab authority | Why not commandable |
|---|---|---|---|---|
| Custom | `custom.modbus-percent-v1` | Documented | forbidden | no registers configured |
| SolTrix Simulator | `soltrix.sim.huawei.v3` | Simulator verified | lab only | — (measured lab contract) |
| SolTrix Simulator | `soltrix.sim.huawei.v1` | Simulator verified | lab only | — (older lab contract) |
| SolTrix Simulator | `soltrix.sim.goodwe.v1` | Simulator verified | lab only | — |
| SolTrix Simulator | `soltrix.sim.solis.v1` | Simulator verified | lab only | — |
| **Huawei** | `huawei.sun2000.pending` | Documented | **lab only** | commandable in lab; see §1.2 |
| **Huawei (plant, via SmartLogger)** | `huawei.smartlogger.plant` | Documented | **lab only** | plant command at `40428` confirmed on **measured** power at `40525`, never on the stored-command echo; nothing exercised against a physical logger (§1.3) |
| GoodWe | `goodwe.commercial.pending` | Documented | forbidden | command/readback at 42407 transcribed, but the register is **flash-backed** with no documented write rate (see §1.2) |
| Solis | `solis.commercial.pending` | Documented | lab only | prerequisite enable at PDU 3069 is now described and **verified by readback** before any command (§1.2) |
| Growatt | `growatt.tl3x.documented` | Documented | forbidden | power-on write lock (§1.2) |
| Growatt | `growatt.tlx.documented` | Documented | forbidden | power-on write lock (§1.2) |
| Sungrow | `sungrow.string.documented` | Documented | lab only | prerequisite enable at PDU 5006 is now described and **verified by readback** before any command (§1.2) |
| Chint / CPS | `chint.cps.sch100_125ktl.documented` | Documented | forbidden | needs prerequisite enable (§1.2) |
| SolarEdge | `solaredge.terramax.documented` | Documented | lab only | best-evidenced manual of any brand (documents a settle time AND a command interval), but **inert at runtime**: no active-power register, so it never becomes eligible to command — and the manual contradicts itself on Float32 vs integer (see §1.6) |
| FoxESS | `foxess.commercial.pending` | Documented | lab only | command/readback at 49007 from the FoxESS commercial manual; addressing convention **deduced, not proven** (see §1.5 below) |
| AISWEI (Knox / Solplanet ASW) | `knox.aiswei.asw.documented` | Documented | forbidden | printed 44001 must enable active-power control before printed 45403 takes effect, and 45403 echoes either way |

Write-qualified or production-approved profiles: **0**.

**Huawei is the only real-brand profile that can be exercised at all**, and only
against a declared simulator. Everything else is refused before a command can be
issued.

### 1.2 Why four transcribed brands are still refused

Transcribing the Solis, Growatt, Sungrow and Chint/CPS manuals found a failure
mode worse than a wrong address, and it is the reason those profiles carry
register maps yet cannot be commanded.

**Solis** (tag 3070 = `0xAA`), **Sungrow** (tag 5007 = `0xAA`) and **Chint/CPS**
(`0x2602` = 1) each require a register to be set before their power-limit setpoint
does anything. The trap is that the setpoint register still **accepts** the write
and still **echoes it back**. A controller would therefore see a matching readback,
report the command **confirmed**, and the inverter would ignore the limit and keep
generating.

That is worse than a mismatch and worse than a timeout, because the readback stops
being evidence and every layer above it — including the operator — is told the
plant is limited when it is not.

**The firmware now sequences and verifies a prerequisite**, so this section no
longer describes a blanket refusal. Authority depends on whether the profile can
describe an enable register that is both writable and *readable*: it writes it,
then RE-READS to confirm, and never trusts its own write. Solis (PDU 3069) and
Sungrow (PDU 5006) carry verifiable descriptions and are lab-commandable.

Two are still refused, for different reasons. **Chint/CPS** has a citation for
writing `0x2602` but none establishing it can be READ — an enable written blind is
an assertion the controller cannot check, which reaches the same false-confirmation
trap by another door. **Knox/AISWEI** is the same case at printed 44001. Being
unable to command is recoverable; being told a limit is in force when it is not is
not.

**Growatt** is refused for a related reason: it locks network power control after
power-on, the manual's unlock password is **redacted**, and it **auto-relocks
after five minutes** — so control would stop silently mid-run even if the unlock
were known.

**Huawei is not refused**, because its prerequisite is different in kind: when a
SmartLogger is in the path, `Remote power schedule` must be set to `Enable` per
inverter, and that is a **logger menu setting**, not a register the controller
writes. It is a one-time human commissioning step, recorded in
`docs/SITE_COMMISSIONING_RUNBOOK.md` §1.4 together with two related traps —
register 42019, where a non-zero schedule-validity period makes a commanded limit
**self-expire**, and register 40737, where anything other than remote scheduling
means something else owns plant scheduling. **The hazard is identical if the step
is skipped, and the controller cannot detect it.**

### 1.3 Plant-level control at the logger: implemented, on measured power

Commanding the plant at the logger (`40428`, RW U16, percent x10) rather than per
inverter is one write instead of N behind a documented >=1 s interval, and it avoids
a register collision where `42017` is `SystemTime: year` on the logger but
`active power gradient` on an inverter.

It was previously refused because reading `40428` back returns **the value the logger
stored, not the plant's achieved state**, and an undocumented "Adjustment
coefficient" means a commanded 80 % need not deliver 80 %. Confirming on that echo
would report `confirmed` for a limit that is not in force.

**Confirmation can now close on measured plant power** (`40525`, RO I32, raw watts),
and the honest part is what it refuses to claim:

| Situation | Verdict |
|---|---|
| Output was **above** the new limit before the command, at or below it after | **confirmed** — the limit is demonstrated |
| Output **above** the limit past the settle window | **mismatched** + safe-zero. Unambiguous: no change in irradiance can lift a plant above a limit in force |
| Output below the limit, but it was **already** below beforehand | **unverified** — equally consistent with the limit working and with the sun going in |
| No pre-command baseline (e.g. after a restart) | **unverified** |
| A commanded 100 % | **unverified, permanently** — the baseline can never be above it |

Verified independently of the implementation: a cloudy plant whose setpoint echo
matches perfectly is correctly **not** confirmed, which is the exact trap this
mechanism exists to avoid.

Two consequences to be aware of. The ambiguous verdict deliberately does **not**
demand a safe-zero — doing so would drive PV to zero every time irradiance dipped
below the commanded limit, most of a working day — so escalation waits for the next
sample. And under ambiguity no commanded value advances, so the control engine sees
no confirmed command, which is honest rather than convenient.

A demonstrated limit still says only that output is at or below what was asked. It
says **nothing** about the Adjustment coefficient, which has no register and is
invisible to a Modbus client.

Full evidence and citations: `docs/SMARTLOGGER_PATH_ANALYSIS.md`.

### 1.6 SolarEdge: the best-documented manual, and two reasons it is still inert

The SolarEdge TerraMax technical note is the strongest evidence obtained for any
brand. It is the **only** manual read for this project that documents both a
setpoint reaction time ("< 1 s") and a data-transfer interval ("< 0.1 s"), and it
proves its addressing convention with four worked frames. Float32 support and
per-profile command word order were added to the firmware specifically so it could
be expressed — before that, writing 50 % into its Float32 register as an integer
would have produced the float ~7e-44, effectively zero, and the readback would have
decoded the same garbage the same wrong way and **confirmed** it.

It is nonetheless not commandable, for two independent reasons.

**It is inert at runtime.** The profile carries no active-power register, because
SolarEdge reports AC power as an int16 with a *runtime* scale factor in a separate
register, which the profile structure cannot express. Without telemetry an inverter
never becomes eligible for a command, so nothing is issued. Same condition as the
Knox/AISWEI entry. Fixing it needs runtime scale-factor support.

**The manual contradicts itself on the data type.** Its type tables state Float32
three times; Appendix A's worked examples treat the same registers as integers — a
read response decoding to `0x00000032` (integer 50) and a write of `0x64` rather
than `0x42C80000`. The type tables were taken as normative, because they are stated
three times and the appendix self-contradicts internally, but **the entire command
path rests on that choice until one read is performed on real hardware.** A single
read-only transaction on `0xF304` settles both the type and the word order at once
and is the first thing to do on site.

**A third item is a genuine safety consideration, not a blocker.** SolarEdge
documents a comms-loss fail-safe: a command timeout (`0xF310`) and a fall-back
active power limit (`0xF312`). The manual states **no default for either**. If the
fallback is 100 %, then losing the controller *raises* the plant's limit rather than
holding or lowering it — the opposite of fail-safe from a generator's point of
view. Both registers must be read and recorded during commissioning. The firmware
does not write them.


"Documented" means the register map was transcribed from a manual and has never
been exercised against the physical equipment. Promoting a profile requires the
exact manual, a model-specific mapping, simulator evidence, a bench test and a
physical readback qualification — in that order.

**The Huawei entry is now a real transcription rather than a placeholder.** Every
field is attributed line by line to `Huawei Inverter Modbus Interface Definitions
(V3.0)`, Issue 01 (2023-01-17), and the addressing convention is settled by the
manual's own `40200/0X9D08` line. It is still `DOCUMENTED`: nothing in it has been
exercised against a physical SUN2000, so the production write gate still refuses
it. Transcription raises the *quality of the claim*, not the qualification level.
Attribution and the open items are in `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md`;
the procedure for closing them is `docs/SITE_COMMISSIONING_RUNBOOK.md`.

**Consequence for this release:** it is a *monitoring, commissioning and
protection* release. It is not a *closed-loop control* release. Automatic
control is structurally inhibited against physical equipment and will remain so
until a profile is qualified against real hardware.

### 1.1 The one narrow exception: declared lab simulators

The site's inverters are ~2000 miles away, so the control loop had to become
exercisable before anyone travels. It previously could not be at all: writing
requires a production-approved, non-simulator profile, none exists, and the loop
was unreachable.

Rather than weaken the production gate, a **second and narrower** authority was
added. An authenticated engineer may declare a specific inverter's endpoint to be
a Modbus simulator; that declaration, and nothing else, unlocks a command through
a profile that is not production-approved, and it grants `lab_simulator_only`
authority — never production.

What bounds it:

- `INVERTER_WRITE_FORBIDDEN` is the zero value, so uninitialised or unreadable
  state denies the write.
- A readback register is required in **both** modes. An unconfirmable command is
  never permitted.
- A simulator-only profile can never reach production authority, and a lab
  declaration never raises a profile's qualification level.
- Commanding real equipment this way would require a human to declare real
  equipment a simulator — a deliberate false statement, not an accident and not a
  default. **This is the one thing the design cannot defend against**, and it is
  called out as such in the site runbook.
- Declaring or revoking a lab target disables automatic control, so it cannot take
  effect underneath a running loop.

**The commissioning verdict now carries a scope**, and the scope is decided by the
weakest link:

| Scope | Meaning |
|---|---|
| `none` | Not commissioned. Automatic control inhibited. |
| `lab_simulator_only` | Commissioned, but at least one commanded inverter is a declared simulator. **Nothing observed is evidence about physical equipment.** One declared simulator makes the whole verdict LAB even if every other inverter is production-qualified. |
| `production` | Every commanded inverter passed production write qualification. Not reachable today. |

Lab counts are never summed into the qualified count, and the status, gate and
assignment APIs all publish the scope beside the verdict, so `"commissioned":
true` cannot be read without also knowing whether the target was real.

**A `lab_simulator_only` verdict is not a release.** No verdict reachable on this
commit is a production verdict.

## 2. Demonstrated on physical hardware

Flashed to the board over COM5, hash-verified, and observed live at
192.168.100.14 on `Automatrix-4G`.

**This evidence was gathered at `1282af8` and has not been re-gathered at
`6dd862c`.** The commits since then touch the write-permission gate, the profile
store (schema 2, with migration) and the settle window, so the board has not been
observed running the code this document now assesses. Nothing in the table below
should be read as evidence about `6dd862c`; it is evidence that the board ran a
close ancestor.

| Area | Evidence |
|---|---|
| Boot stability | 13 min continuous uptime, no crash, no reboot loop |
| Wi-Fi | Associated, IP 192.168.100.14, RSSI −49 dBm |
| Meter acquisition | EM500 slave 3 online, quality good |
| **Acquisition latency** | Grid data age **8–123 ms** across repeated samples; control cycle age 75 ms |
| Source detection | Resolved **generator via tariff 2** with 220 V applied to the tariff port |
| Fail-closed control | `control_enabled:false`, `inhibit_reason:"No inverter is enabled."` |
| Alarm state model | `rtn_unacknowledged` observed live on NET-001 and MTR-003 (ISA-18.2 gap A1) |
| Root-cause grouping | `active 2` reduced to `primary_active 1` (gap A5) |
| Nuisance suppression | `suppressed_transitions 1` — on/off delay absorbing chatter (gap A4) |
| Shelving authorisation | Unauthenticated shelve returns **401** |
| Engineering gateway | Commissioning gate, write-confirmation, identity and audit-log return **401** unauthenticated. **Not a blanket 401:** `engineering_guard.c` deliberately routes `GET /api/config`, `/api/meters`, `/api/inverters` and `/api/inverter-telemetry` to reduced *safe* payloads with HTTP 200, with credentials and SSIDs stripped. The property that matters is that no unauthenticated response carries a secret and every mutating route is refused — not that every route answers 401. |
| Operator history | 200 with 24.8 KB payload (PSRAM-dependent; previously failed with 500) |
| **Alarm journal durability (gap A2)** | Storage partition provisioned on first boot (`storage partition provisioned; the alarm journal is now durable`), then **survived a hard reset**: `stored` 8 → 12, `next_sequence` continued 9 → 13 rather than resetting, sequences 1–12 all readable and ordered, 0 unreadable, 0 write failures. Provisioning did **not** repeat on the second boot. |

## 3. Demonstrated in software only

| Area | Evidence | Not yet shown |
|---|---|---|
| Full contract suite | **55** Python source contracts wired into CI at this commit (up from 54; the recorded 0-failure run was at `1282af8` and the full suite has not been re-run locally at `6dd862c`) | A recorded green run at this exact commit |
| Executable unit tests | 6 gcc-compiled tests wired into CI (source mode, source detection, Solar-Grid integration, commissioning gate, write confirmation, **write permission**) | — |
| Browser modules | 3 JS suites, syntax checks on all edited modules | Rendered layout |
| Build | Clean ESP-IDF build, **zero warnings**; app 1,686,144 bytes, 46% of the 3 MB partition free | — |
| Inverter command path | Simulator scenarios incl. rollback, timeout, comm-lost | Any physical inverter |
| Alarm journal ring behaviour | Host-compiled test: wrap, corruption (exactly one record lost), sequence continuity across reopen | Wrap and corruption recovery on real SPIFFS (durability itself is verified — see above) |
| Commissioning gate | Nine prerequisites, fail-closed on unreadable state; scope published with every verdict | Payload inspection (needs Engineering password) |
| Write permission gate | Executed over the real profile catalogue: FORBIDDEN is zero, NULL forbidden, **no shipped profile can command production**, simulator profiles never reach production, readback mandatory even in the lab, every level below production-approved requires an explicit declaration | Anything about physical equipment |
| Per-profile settle window | `power_limit_settle_ms`, with the deferred-apply sequence walked in `tests/inverter_write_confirmation_test.c`: PENDING throughout, CONFIRMED once applied, MISMATCHED for a genuine disagreement past the window, and pending never demanding a safe-zero | A settle value measured on physical equipment |
| UI contrast | WCAG arithmetic on parsed token values, 32 pairs, 0 failures | Visual rendering |

### 3.1 The settle window, and the false fault it would have caused

This is worth stating plainly because it is the clearest case so far of a
firmware-side invented value doing real harm.

Measuring the lab simulator showed it **accepts** a 40125 percentage write and
applies it about **1500 ms later**, reporting the *previous* active limit until it
does. Against the old **global 500 ms** settle window, a perfectly accepted command
read as `MISMATCHED`. `MISMATCHED` latches a confirmation fault, removes the
inverter from commandable capacity and **drives it to zero**. A false fault on a
healthy 100 kW machine is as damaging as missing a real one.

How long a setpoint takes to reach its readback register is a property of the
**device**, not of the controller, so it now lives in the profile as
`power_limit_settle_ms` (zero means "use the firmware default"). The lab profile
declares 2500 ms, derived from measurement with margin. The **Huawei manufacturer
profile deliberately leaves it at the firmware default, because no manual states a
value and there is no evidence to set one from.**

The window can only ever *delay* a verdict — past it, a disagreeing readback is
still a mismatch, and the 5000 ms deadline still bounds how long an unconfirmed
setpoint may stand. A profile value is clamped strictly below that deadline,
because a settle window at or beyond it would leave a disagreement permanently
pending.

**Unresolved by this change:** the correct value for real SUN2000 hardware, and
whether the readback reports the *requested* or the *active* limit. The simulator
reports the active value; the manual says nothing. Both must be measured on site
(`docs/SITE_COMMISSIONING_RUNBOOK.md` §5).

### 3.2 The closed loop: proven at the Modbus level, not through the firmware

Two separate claims, deliberately kept apart:

- **Proven.** The lab simulator's register layout and behaviour were exercised
  **directly, by a Modbus client**, and the results are recorded in
  `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §2: `30000` returns `SUN2000-SIM`
  with first word `0x5355`; `32080` decodes as I32 watts, high word first;
  `40125` reads percent x 10; and a 40125 write is applied ~1500 ms later while
  40199 applies immediately. Manual and simulator agree on every address, type,
  gain and scale. This establishes that the transcribed map is coherent and that
  the loop closes **at the Modbus level**.
- **Not proven.** The loop has **not** been closed **through the firmware**. No
  run exists in which the controller itself issued a command, confirmed it by
  readback and regulated against a meter. The blocker is access, not design:
  driving that path requires the Engineering-scope API, and the **Engineering
  password has not been supplied**, so the lab-target declaration that unlocks a
  write cannot be made.

Until that run exists, the closed loop is a design supported by unit tests and by
direct-client register evidence — not a demonstrated firmware behaviour. Do not
read section 3.1's simulator measurements as firmware validation; they were taken
with a Modbus client standing where the firmware would stand.

## 4. Not demonstrated

1. **Any physical inverter write.** See section 1. This is the one item that
   keeps the release from being a control release.
2. **The closed loop through the firmware, even against the simulator.** Proven at
   the Modbus level by a direct client; never driven by the controller itself.
   Blocked on Engineering-API access. See section 3.2.
3. **Write confirmation against real equipment.** The readback evaluator is
   unit-tested and the settle window is now per-profile, but
   `INVERTER_CONFIRMATION_SETTLE_MS = 500` (the default the Huawei profile still
   uses) and `DEADLINE_MS = 5000` remain **firmware-side values chosen without a
   manual** and need site measurement. See section 3.1.
4. **Which register a real SUN2000 honours for percentage control.** The manual
   documents both `40125` ("active fine adjustment interface") and `40199` (the
   anti-backcurrent "active power percentage control interface"). Anti-backcurrent
   is exactly this product's application, so 40199 may be the more correct
   register; the firmware commands 40125 because it is the conventional
   third-party derating interface. **This is an open site-verification item, not a
   settled decision.**
5. **Whether a SmartLogger sits in the Modbus path.** The Huawei profile's
   connection type is `LOGGER_GATEWAY`. A logger can re-map unit ids and
   addresses, which would invalidate every transcribed address. The SmartLogger
   documents are available but unanalysed.
6. **Protected endpoint payloads.** Correctly returning 401; contents
   uninspected pending the Engineering password.
7. **Visual rendering of the UI.** The last visual audit run was invalid (37 of
   60 runs, adapter suspended mid-run) and has not been repeated.
8. **Grid/generator synchronisation interlock.** `fleet_synchronised()` exists
   but is not wired into the control engine, because it needs per-manufacturer
   inverter status registers that have not been supplied. For Huawei specifically
   the blocker is now identified: signal 178 "Device Status" at `32089` is an E16
   whose **code table the manual defers to an "Inverter Key Signal Extension
   Description" that is not among the manuals available.** No profile configures a
   status register and every inverter reports `INVERTER_STATE_UNKNOWN`. A guessed
   mapping could report "on grid" while an inverter is faulted, so the honest
   unknown stands until that document is obtained.
9. **Alarm journal wrap and corruption recovery on real flash.** Proven on the
   host at 16384 records; the board has written 12. Reaching a wrap in the field
   takes time, so the ring's oldest-first eviction is unproven on real SPIFFS.
10. **FAT / SAT.** Not started.
11. **Any of the site procedure in `docs/SITE_COMMISSIONING_RUNBOOK.md`.** It is
    written, reviewed against the source, and unexecuted. Writing a procedure is
    not performing it.

## 4a. Known defect: three definitions of "stale"

Found by the operator-documentation audit, quantified here, **not yet fixed**.

Three independent thresholds decide whether a meter sample is too old:

| Where | Threshold | Governs |
|---|---|---|
| `safety_manager.c` | the **configured** `meter_stale_timeout_ms` (default **1000 ms**) | whether control input is blocked |
| `operational_api.c` | fixed `METER_FRESH_MS` = **5000 ms** | whether alarm MTR-001 raises |
| `web_api.c` | fixed `METER_STALE_AFTER_MS` = **5000 ms** | the dashboard's `meter_stale` flag and quality word |

With the shipped defaults there is therefore a **1000–5000 ms window** in which
control is inhibited because the sample is stale, while the dashboard reports the
measurement as good and no alarm is raised. An operator in that window sees a healthy
plant that is not controlling, with nothing on screen explaining why — which is
precisely the confusion this product's provenance work exists to remove.

The measured acquisition latency makes this reachable rather than theoretical: mean
93 ms but 24 % of transactions exceed 250 ms and the tail reaches 319 ms, so a brief
gateway stall plus a retry can cross 1000 ms without ever approaching 5000 ms.

**The fix is one definition.** The configured value should govern all three, because
it is the one an engineer can tune to the site's measured latency; the two fixed
constants cannot be. Not applied in this change only because both files were being
edited concurrently and this is safety-relevant code that should not be merged
carelessly.

## 4b. Deferred by the product owner: inverter profile qualification

Parked deliberately, not forgotten. Recorded here so the reasons survive and nobody
has to re-derive them.

| Brand | Why it is not commandable | What would unpark it |
|---|---|---|
| **GoodWe** | Command register 42407 is **flash-backed** ("does not support high-frequency write operations") and the manual gives **no permitted write rate** and no write-cycle budget. Commanding it continuously would wear out the inverter's non-volatile memory -- a permanent hardware failure while every write reports success. | A permitted rate **from GoodWe**. This is a question for the manufacturer, not a measurement. Once stated, set `min_command_interval_ms` and the existing guard lifts by itself. |
| **Growatt** | Locks network power control after power-on, the manual's unlock password is **redacted**, and it **re-arms after five minutes** -- so control would stop silently mid-run even if the unlock were known. | The unlock procedure from Growatt, plus a decision on how to handle the five-minute re-arm. |
| **Chint / CPS** | `0x2602` is cited for **writing** only; nothing establishes it can be **read**. An enable written blind cannot be verified, and an unverifiable enable means the setpoint is accepted, echoed back and ignored. | One citation showing `0x2602` is readable (FC 0x03 or 0x04), or a site read proving it. |
| **Knox / AISWEI** | Printed 44001 must enable active-power control before 45403 takes effect, and 45403 echoes either way. Same unverifiable-enable case. | A citation or site read establishing 44001 readback. |
| **SolarEdge** | Inert at runtime: SolarEdge reports AC power with a **runtime scale factor** the profile structure cannot express, so telemetry never becomes valid and the inverter is never eligible for a command. Separately, its manual **contradicts itself** on Float32 versus integer. | Runtime scale-factor support in the profile model, and one read on real hardware to settle the data type. |
| **FoxESS** | Commandable in lab, but its addressing convention is **deduced, not proven** -- no offset rule and no worked frame. If the deduction is wrong, 49007 becomes 49006, a *reactive power* register. | One read of 30000 on the physical machine. |

**None of these is a firmware defect.** In every case the firmware is refusing to
command equipment on evidence it judges insufficient, which is the intended behaviour.
The refusals are enforced structurally and covered by executable tests, so they cannot
be lifted accidentally -- each one needs a specific, named piece of evidence.

**Huawei is the exception and the shortest path to a control release.** Its map is
transcribed from the manufacturer manual with per-value citations, its addressing
convention is settled by the manual's own worked example, and it is commandable
against a declared simulator today. The open questions are narrow and enumerated in
`docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md`: which of `40125` or `40199` the machine
honours, the settle time, and whether the readback reports the requested or the active
value. `docs/SITE_COMMISSIONING_RUNBOOK.md` is written to close all three in one visit.

## 5. Open decisions

These are product decisions, deliberately not made unilaterally.

| # | Decision | Current behaviour |
|---|---|---|
| D1 | Should a **disabled generator ramp** block commissioning? | It blocks. "No rate limit" is treated as unsafe rather than inherited from a default. |
| D2 | Should **one unqualified inverter** block the whole plant? | It blocks. Every enabled inverter must be write-qualified and readback-capable. |
| D3 | Was deleting `inverter_command_policy.{c,h}` correct? | Deleted. It decided the same question for a synchronous path that no longer exists; two competing confirmation policies in safety firmware is worse. Reversible. |
| D4 | Settle window / deadline 5000 ms | Now **per profile** (`power_limit_settle_ms`). The lab profile declares 2500 ms from measurement; every other profile, including Huawei, falls back to the invented 500 ms default. The 5000 ms deadline remains global and invented, and a profile value is clamped strictly below it. Documented in source as needing site measurement. |
| D7 | Is the lab-simulator write authority an acceptable way to reach the control loop before travelling? | In force. It is the only route to exercising the loop at all, it grants `lab_simulator_only` and never production, and its one weakness is a human falsely declaring real equipment a simulator. Reversible: removing it makes the loop unreachable again. |
| D8 | `40125` or `40199` for Huawei percentage control? | 40125 is commanded, on the grounds that it is the conventional third-party derating interface. **Not decided on evidence** — the manual's description of 40199 (anti-backcurrent) matches this application better. Deliberately left as a site-verification item rather than switched on reasoning alone. |
| D9 | Should the controller write the inverter's own ramp gradient (Huawei signal 432, `42017`, %/s)? | It does not. The control engine ramps and the inverter's gradient is left alone, so two rate limiters sit in series and the slower dominates. A controller ramp faster than the inverter's gradient will simply not be achieved and will look like a tracking failure. Reconciliation is a site step. |
| D5 | Repeated-mismatch policy | An inverter that mismatches then confirms a safe zero rejoins the fleet. `mismatch_count` is retained but there is no "N strikes and out" latch, so a marginal inverter will cycle. |
| D6 | `.eyebrow` brand orange at **2.23:1** on light background, 10 px | Left untouched, colour and size, pending a brand decision. Pinned by contract so it cannot be silently half-fixed. |

## 6. Inputs still required

- **Generator ratings: rated kW, minimum loading %, reserve kW, reverse-power
  margin.** Still not supplied. The gate treats a zero rating as "not
  commissioned" and holds PV at zero, and no default may be invented. Capturing
  these is the largest single gap the site visit closes.
- Engineering password, to verify protected endpoint payloads **and to drive the
  closed loop through the firmware at all** (section 3.2). This is now a blocker,
  not merely an inspection convenience.
- GoodWe manual (and any other manual intended for write qualification)
- The Huawei *"Inverter Key Signal Extension Description"*, for the Device Status
  code table — the blocker on the synchronisation interlock
- `SmartLogger ModBus Interface Definitions` for the logger model actually
  installed, if one is in the Modbus path
- Confirmation of whether anything else at the site already writes the inverter
  power-limit registers (plant SCADA, EMS, logger export limitation, grid-code
  curtailment). Two masters on one register will fight.

## 7. Recommendation

Release as **monitoring, commissioning and protection firmware**, with automatic
control documented as inhibited pending profile qualification. Do not describe
this build as a closed-loop PV-DG synchronisation controller until at least one
manufacturer profile has passed physical readback qualification and the items in
section 4 are closed.

Two additions since the last assessment, and neither changes that recommendation:

- The Huawei register map is now **attributable to a manual line by line** rather
  than a placeholder. That improves the quality of the claim and makes a site
  visit efficient. It is still `DOCUMENTED`, and the production gate still refuses
  it.
- The controller can now be commanded against a **declared lab simulator**, so the
  loop is reachable for the first time. That is a lab capability with a
  `lab_simulator_only` verdict attached, and it is explicitly not evidence about
  physical equipment.

The single item that would most change this assessment is still to **qualify one
real inverter profile end to end on physical equipment**: exact manual,
model-specific mapping, simulator evidence, bench test, then physical readback.
`docs/SITE_COMMISSIONING_RUNBOOK.md` is the procedure for that visit; it is
written and **unexecuted**.

The nearest item that can be closed **without** travelling is section 3.2 — obtain
the Engineering password and drive the closed loop through the firmware against the
simulator. That would move the loop from "unit-tested design plus direct-client
register evidence" to "demonstrated firmware behaviour against a model", which is
the last step available before the plant itself.
