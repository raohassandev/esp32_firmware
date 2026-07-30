# Site commissioning runbook — Automatrix PV-DG Controller

**Audience:** one commissioning engineer at the plant, with a laptop, `mbpoll`, and
the controller. No firmware knowledge assumed.
**Branch/commit this runbook was written against:** `phase1-fix` @ `6dd862c`
**Written:** 2026-07-30

---

## 0. What this document is, and what it is not

This is an **ordered, executable procedure**. Steps are numbered so they can be
cited in a report. Every step is tagged:

| Tag | Meaning |
|---|---|
| **[RO]** | **Read-only.** No register is written. Cannot move power. |
| **[WRITE]** | **Writes a register.** Can move real power. Requires the abort path of §11 to be in place and rehearsed first. |
| **[CFG]** | Changes controller configuration only. Does not itself command an inverter, but changes what the controller is later permitted to do. |

**This document contains no manufacturer timing values, no tolerances, no
generator ratings and no confirmed control register.** None of those exist as
evidence in this repository. Wherever a number is needed, the runbook tells you
to **measure and record it**. If you find yourself wanting to fill a blank from
memory or from another site, stop: that is the failure mode this procedure exists
to prevent.

**Standing state of the product before this visit** (from
`docs/RELEASE_READINESS.md` and `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md`):

- **No manufacturer profile has passed physical qualification.** Automatic
  control is therefore structurally inhibited. That is by design, not a fault.
- The Huawei register map in `components/inverter_manager/inverter_profiles.c` is
  **transcribed from the manual only** (`Huawei Inverter Modbus Interface
  Definitions (V3.0)`, Issue 01, 2023-01-17). It has never touched a physical
  SUN2000.
- The control loop has been exercised against a **Modbus TCP lab simulator**, not
  against equipment.
- **Which register the machine honours for percentage control is an open
  question** (§4). Do not assume.

### Credentials

The Engineering-scope API requires a password. **Obtain it from the product
owner** before travelling. It is deliberately not recorded here or anywhere in
this repository. If it has been lost, recovery requires physical reflashing and
serial-console access — arrange that with the product owner before the visit, not
during it.

### Prerequisites to bring

- [ ] `mbpoll` installed and its version recorded: `____`
- [ ] Laptop able to reach the inverter network **and** the controller
- [ ] The Engineering password, obtained from the product owner
- [ ] A clamp meter or the plant's own metering, independent of the controller,
      so PV output can be cross-checked without trusting the thing under test
- [ ] This runbook, printed or offline — do not assume site internet
- [ ] Authority to stop: written confirmation of who on site may halt the work
      and who must be told. Name: `____` Contact: `____`

---

## 1. Before touching anything — record the plant [RO]

Nothing in this section touches a register. Do all of it before anything else,
because most of it cannot be reconstructed later and several later steps are
undecidable without it.

### 1.1 Equipment inventory

For **each** inverter:

| # | Manufacturer | Model (from nameplate) | Rated kW | Serial | Firmware ver. | IP:port | Modbus unit id |
|---|---|---|---|---|---|---|---|
| 1 | `____` | `____` | `____` | `____` | `____` | `____` | `____` |
| 2 | `____` | `____` | `____` | `____` | `____` | `____` | `____` |
| 3 | `____` | `____` | `____` | `____` | `____` | `____` | `____` |

- [ ] Photograph every nameplate. The **model string on the nameplate** is the
      thing you will compare against register 30000 in §3.
- [ ] Record the exact manual revision the site believes applies to these
      machines: `____`. If it differs from the V3.0 / Issue 01 document the
      profile was transcribed from, **stop and report it** — the transcription's
      addresses are attributable to that revision only.

### 1.2 Generator(s)

**These have not been supplied to the project and must be captured here.** The
commissioning gate treats a generator rating of zero as "not commissioned" and
holds PV at zero; there is no default and none may be invented.

| Field | Value | Source of the value |
|---|---|---|
| Generator make/model | `____` | `____` |
| Rated kW (prime, not standby — record which) | `____` | `____` |
| Minimum loading %, per the **engine manufacturer** | `____` | `____` |
| Reverse-power protection setting, if fitted (kW and time delay) | `____` | `____` |
| Number of gensets and whether they run in parallel | `____` | `____` |
| Who authorises the minimum-loading figure | `____` | `____` |

- [ ] Minimum loading is an **engine** limit, not a controller preference. Take
      it from the genset documentation or the genset supplier and record which.
      Do not derive it from a rule of thumb.
- [ ] Reserve kW and reverse-power margin are **product-owner decisions** informed
      by the above. Record the values agreed and by whom: `____` / `____`.

### 1.3 Metering

- [ ] Grid meter: model `____`, unit id `____`, CT ratio `____`
- [ ] Generator meter(s): model `____`, unit id `____`, slot `____`
- [ ] Meter orientation actually observed: does import read **positive** or
      **negative**? `____` (this maps to `meter_orientation` in the Solar-Grid
      config, and getting it backwards inverts the control loop)
- [ ] Where physically is each CT clamped, and in which direction? Photograph.

### 1.4 Network topology — and specifically, is a SmartLogger in the path?

This is the single most consequential topology question, because a logger or
gateway can **re-map unit ids and register addresses**, which would invalidate
every address in §3 and §4.

- [ ] Draw the path: controller → `____` → `____` → inverter. Include every
      switch, router, RS-485 gateway, data logger and NAT.
- [ ] Is a **Huawei SmartLogger** (or any manufacturer logger/gateway) between the
      controller and the inverters? **Yes / No:** `____`
  - If **yes**: record its model and firmware `____`, and note that the Huawei
    profile's connection type is already `LOGGER_GATEWAY`. Obtain the
    `SmartLogger ModBus Interface Definitions` document for **that** model, and
    treat every address in §3–§6 as **unverified through the logger** until you
    have read it back through the logger yourself. A logger may present a
    different unit id per inverter and may not expose all registers.
  - If **no**: record that the inverters are addressed directly, and by what
    transport (Modbus TCP direct, or RTU behind a generic gateway).
- [ ] Is anything else already writing to these inverters — a plant SCADA, an
      energy-management system, the logger's own export limitation, a grid-code
      curtailment function? List: `____`
      **Two masters writing the same power-limit register will fight.** If
      anything else writes it, resolve ownership with the product owner *before*
      §7. Record the decision and who made it: `____`

### 1.5 Back up before you change anything [RO]

- [ ] Controller configuration: `GET /api/config` — save the response to a file,
      timestamped. Also capture `GET /api/solar-grid/config`,
      `GET /api/source-detection`, `GET /api/meters/config`,
      `GET /api/inverters/config` and `GET /api/inverter-profiles`.
- [ ] Controller identity and firmware: `GET /api/system/identity` — record the
      firmware version/commit reported: `____`
- [ ] Baseline status: `GET /api/status` and `GET /api/solar-grid/status`. Record
      `control_enabled`, `inhibit_reason`, `commissioned`, `commissioning_scope`.
      Expected before commissioning: `control_enabled: false` with a stated
      inhibit reason, `commissioning_scope: "none"`.
- [ ] Existing alarms: `GET /api/operator/alarms`. Record what was already active
      **before** you arrived, so you do not later attribute a pre-existing
      condition to your own work.
- [ ] Inverter-side settings you are about to interrogate or change: for each
      inverter record the **present** value of the power-limit register(s) and the
      active-power gradient (§6) so you can put them back. This is the rollback
      baseline and §11 depends on it.

| Inverter | 40125 (raw) | 40199 (raw) | 42017 (raw, 2 words) |
|---|---|---|---|
| 1 | `____` | `____` | `____` |
| 2 | `____` | `____` | `____` |
| 3 | `____` | `____` | `____` |

- [ ] Confirm the plant's own protection settings are live and untouched. You are
      not authorised to relax a protection setting to make commissioning
      convenient.

**Gate:** do not proceed to §2 until §1.5 backups exist as files on the laptop.

---

## 2. Addressing: 1-based manual tag vs 0-based PDU address [RO]

**This has caused real errors on this project.** Read this section before typing
any `mbpoll` command.

### 2.1 What the manual settles

The Huawei manual describes a command to *"register address: 40200/0X9D08"*, and
`0x9D08 == 40200`. So **the manual's decimal addresses are the raw on-the-wire
PDU addresses, used directly with no offset.** That is recorded in
`docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §1 and in the profile comment in
`components/inverter_manager/inverter_profiles.c`.

### 2.2 What `mbpoll` does by default

`mbpoll` reference numbering is **1-based by default**: `-r N` requests PDU
address `N-1`. The `-0` flag switches it to 0-based, so `-0 -r N` requests PDU
address `N`.

**Consequence:** to read the register the manual calls 30000 you must use
`mbpoll -0 -r 30000`, *or* `mbpoll -r 30001` without `-0`. Mixing these is
exactly the off-by-one that has bitten this project.

### 2.3 Prove the convention on this machine, empirically

Do not take the above on trust — prove it, once, per site.

- [ ] Read the model string with `-0` (0-based, matching the manual):

  ```
  mbpoll -a <unit_id> -0 -r 30000 -c 15 -t 4 -1 <inverter_ip>
  ```

  Record the raw words: `____`
  Decoded ASCII: `____`

- [ ] Read the same thing without `-0`:

  ```
  mbpoll -a <unit_id> -r 30001 -c 15 -t 4 -1 <inverter_ip>
  ```

  Record: `____`

- [ ] **These two must return the same data.** If they do, the convention is
      confirmed for this machine and this path. Tick and record which form you
      will use for the rest of the visit: `____`
- [ ] If `-0 -r 30000` returns a plausible model string and the other form
      returns an exception or nonsense, record that too — that is still an
      answer, but a weaker one. Note it and say so in the report.
- [ ] If **neither** returns a model string: do not start guessing offsets.
      Suspect the logger (§1.4), the unit id, or the wrong register block. Record
      the exception code returned: `____` and stop to think.

**Every command in the rest of this runbook is written with the manual's decimal
address. Apply your confirmed convention when you type it.**

Also note: **all writes in this runbook are single-register FC06 writes** to
addresses the manual documents as RW I16. Do not substitute FC16 unless you have
established that this machine requires it; and never write a multi-register
address (like 42017, 2 words) with a single-register function.

---

## 3. Read-only identification of every inverter [RO]

**No write in this section.** Complete it for **every** inverter before writing
to any of them.

For each inverter, in turn:

### 3.1 Reachability

- [ ] Ping / TCP-connect to the endpoint. Record round-trip: `____` ms
- [ ] Confirm the unit id. If a logger is in the path (§1.4) the unit id is the
      **logger's** id for that inverter, which need not match anything printed on
      the inverter. Record it: `____`
- [ ] Enumerate: if you are unsure which unit id maps to which physical machine,
      identify each by reading its model/serial and its live active power and
      correlating with what you can see and measure on site. **Do not assume unit
      id order matches physical order.** Record the mapping:

  | Unit id | Physical position / label | Nameplate model | Serial |
  |---|---|---|---|
  | `____` | `____` | `____` | `____` |

### 3.2 Identity — register 30000 (manual "Model", RO STR, 15 registers)

```
mbpoll -a <unit_id> -0 -r 30000 -c 15 -t 4 <inverter_ip>
```

- [ ] Decoded model string: `____`
- [ ] Does it match the nameplate recorded in §1.1? **Yes / No:** `____`
      If no, **stop**. You are talking to a machine you have not identified.
- [ ] First word in hex: `____`. The firmware's identity probe reads word 0 only
      and expects `0x5355` (`"SU"`) for a SUN2000. If word 0 is not `0x5355` on a
      machine whose nameplate says SUN2000, record it — the profile's identity
      probe will reject this machine and that is a finding, not something to work
      around.

### 3.3 Active power — signal 171, 32080, RO I32, gain 1000 (raw watts)

```
mbpoll -a <unit_id> -0 -r 32080 -c 2 -t 4 <inverter_ip>
```

- [ ] Raw two words, in the order returned: `____`
- [ ] Interpreted **high word first** (word order AB, as the firmware assumes):
      `____` W → `____` kW
- [ ] Interpreted low word first, for comparison: `____` kW
- [ ] Cross-check against an **independent** measurement (clamp meter, plant
      metering, the inverter's own display): `____` kW
- [ ] Which interpretation matches reality? `____`
      If low-word-first matches, the firmware's word order is wrong for this
      machine — **record it as a defect and do not proceed to any write**, because
      a mis-decoded active power means the control loop is regulating against a
      number it has misread.
- [ ] Repeat over at least three samples several minutes apart, at different
      output levels, so you are not confirming a word order against a value that
      happens to be symmetric or small. Samples: `____` / `____` / `____`

### 3.4 Present power-limit state — read only

```
mbpoll -a <unit_id> -0 -r 40125 -c 1 -t 4 <inverter_ip>
mbpoll -a <unit_id> -0 -r 40199 -c 1 -t 4 <inverter_ip>
```

- [ ] 40125 raw: `____` → `____` % (manual gain 10, i.e. raw = percent x 10)
- [ ] 40199 raw: `____` → `____` %
- [ ] Do they agree with each other? `____` (Interesting either way. Record it.)
- [ ] Does either read 100.0 % (raw 1000), i.e. unlimited? `____`
- [ ] If either reads something **other** than unlimited, something is already
      curtailing this machine. Find out what before you add a second limiter
      (§1.4). Record: `____`
- [ ] Does a read of 40199 return a Modbus exception on this machine? `____`
      (A machine that will not even let you read a register is unlikely to let you
      write it. This is a free, safe data point for §4.)

### 3.5 Device status — 32089, and why the firmware ignores it

- [ ] Read it anyway and record: `____` (hex `____`)
- [ ] **The code table is not in any manual available to this project.** The
      firmware therefore configures no status mapping and every inverter reports
      `INVERTER_STATE_UNKNOWN`. Do **not** invent a meaning for the code you read.
- [ ] While on site, record the code observed against the state shown on the
      inverter's **own display or portal** at the same moment. Do this for as many
      distinct states as you can observe naturally (running, standby, shutdown,
      fault if one occurs of its own accord). **Do not induce a fault to collect a
      code.**

  | Observed code (hex) | State shown on inverter display | Timestamp |
  |---|---|---|
  | `____` | `____` | `____` |

  This is evidence towards a future status mapping. It is **not** a mapping, and
  it must not be turned into one from a handful of samples. The real fix is to
  obtain the *Inverter Key Signal Extension Description* document.

**Gate to §4:** every inverter identified, model matched to nameplate, active
power decoded and cross-checked against an independent measurement. If any of
those failed, the visit stops at read-only and that is a legitimate outcome.

---

## 4. The open register decision: 40125 or 40199? [WRITE — low power]

### 4.1 What is actually open

Both registers encode percent x 10 and both are documented RW I16. The manual
distinguishes them by **purpose**:

- **40125**, signal 409, "Active Power Percentage Derating [Low Precision]",
  described as the *"Active fine adjustment interface"*.
- **40199**, signal 419, "Active Power Percentage Control [Low Precision]",
  described as *"the active power percentage control interface ... used in
  distributed mode ... sent to the power software in **anti-backcurrent
  control** to control the upper limit of the output active power"*.

Anti-backcurrent control is exactly this product's application, so **40199 may be
the more correct register**. The firmware currently commands **40125** because it
is the conventional third-party derating interface. The lab simulator applies
40125 with a delay and 40199 immediately — a hint that 40199 is the
control-rate interface, but **a behaviour in a model is not evidence about
hardware.**

**Your job here is to determine which register this machine actually honours, at
the lowest power that gives an unambiguous answer.**

### 4.2 Conditions before the first write of the visit

- [ ] **One inverter only.** Pick the smallest, or the one whose loss is least
      consequential. Which and why: `____`
- [ ] The plant is on a source that can absorb the change. Record the source
      carrying the plant right now: `____`
- [ ] **PV output is low but non-zero and reasonably steady.** You cannot detect a
      derating command on a machine already producing near zero (irradiance-
      limited output masks the limit entirely), and you should not test at full
      output. Record present output: `____` kW of `____` kW rated = `____` %
- [ ] The controller's automatic control is **disabled** — you are the only
      writer. Confirm via `GET /api/status`: `control_enabled` is `____`.
      If it is true, disable it before proceeding (§11.1).
- [ ] Nothing else is writing the register (§1.4 resolved). Confirmed: `____`
- [ ] The abort path of §11 is open in another terminal, tested, and you have
      rehearsed typing it. Confirmed: `____`
- [ ] Someone on site knows you are about to move power. Who: `____`
- [ ] Present readings recorded immediately before the write:
      active power `____` kW, 40125 `____`, 40199 `____`, wall-clock `____`

### 4.3 Test 40125 first (the register the firmware currently uses)

Choose a target percentage that is **clearly below present output but not zero**,
so the effect is unmistakable and the machine is not shut down. Pick it from the
output you measured in §4.2, and record your reasoning rather than copying a
number from anywhere:

- Present output `____` % of rated. Chosen target: `____` %. Reason: `____`

- [ ] Note the wall-clock time to the second: `____`
- [ ] Write:

  ```
  mbpoll -a <unit_id> -0 -r 40125 -t 4 -1 <inverter_ip> <target_percent x 10>
  ```

  (`-t 4` = holding register; `mbpoll` issues a single-register write. Confirm
  from the tool's output that it wrote **one** register at the intended address.)
- [ ] Transport-level result: accepted / exception `____`
- [ ] Immediately begin the readback timing measurement of §5 — **do not wait**,
      the first few hundred milliseconds are the data you came for.
- [ ] Did **active power** (32080) actually fall towards the target? `____`
- [ ] Independent measurement confirms the change? `____` kW
- [ ] Time from write to first observed change in active power: `____` ms
- [ ] Time for active power to settle at the new level: `____` s

### 4.4 Restore, then test 40199

- [ ] Restore 40125 to the value recorded in §1.5 (normally raw 1000 = 100.0 %):

  ```
  mbpoll -a <unit_id> -0 -r 40125 -t 4 -1 <inverter_ip> <original_raw>
  ```

- [ ] Confirm active power returns to its unlimited level: `____` kW
- [ ] Wait until output is steady again and record it: `____` kW. **Do not test
      the second register while the first is still recovering** — you will not be
      able to attribute what you see.
- [ ] Write the same target to 40199:

  ```
  mbpoll -a <unit_id> -0 -r 40199 -t 4 -1 <inverter_ip> <target_percent x 10>
  ```

- [ ] Transport-level result: accepted / exception `____`
- [ ] Did active power fall? `____`
- [ ] Time to first change: `____` ms; time to settle: `____` s
- [ ] Restore 40199 to its recorded original: `____`. Confirm output recovers:
      `____` kW

### 4.5 Interpreting the result — and the cases that are not clean

Record the verdict explicitly. All four outcomes are real possibilities and only
the first two are clean:

| Outcome | What it means | Action |
|---|---|---|
| Only 40125 moved power | 40125 is the control register; the firmware is already correct | Record as evidence; proceed |
| Only 40199 moved power | The firmware commands the wrong register | **Raise as a defect.** Do not proceed to §8 promotion. Firmware change + re-test required |
| **Both** moved power | Two working interfaces; which one wins when they disagree is now the open question | Do **not** guess. See §4.6 |
| **Neither** moved power | A mode/enable register, a permission, or the logger is blocking it | See §4.7 |

- [ ] Verdict: `____`
- [ ] Evidence bundle saved: raw `mbpoll` transcripts (stdout, with timestamps),
      the independent measurements, and photographs of the inverter display.
      File names: `____`
- [ ] Repeat the whole of §4.3–§4.4 **at least twice more**, at different target
      percentages and preferably at different times of day. One trial is an
      anecdote. Trials recorded: `____`

### 4.6 If both registers work

Then the machine has two limit interfaces and the plant needs to know which
governs. Determine it deliberately, still at low power:

- [ ] Set 40125 to a **higher** percentage and 40199 to a **lower** one. Which
      does the output follow? `____`
- [ ] Restore both, then reverse: 40125 low, 40199 high. Which wins? `____`
- [ ] If the lower always wins, the machine applies the **most restrictive** limit
      and writing either is safe in the "reduce power" direction — but a
      controller that writes only one of them can never *raise* output past a
      limit sitting in the other. Record which register the plant will own, and
      confirm the other is left at 100 %: `____`
- [ ] Restore both to their §1.5 originals and confirm output recovers.

### 4.7 If neither register works

- [ ] Record the exact exception code, if any: `____`
- [ ] Is a logger in the path (§1.4)? If yes, the logger may not forward writes,
      may require its own export-limit interface, or may need remote control
      enabled in its configuration. Record the logger's own setting: `____`
- [ ] Does the inverter require a mode, permission or enable register to be
      written before a percentage limit takes effect? **Nothing in the available
      manual states one, but the absence of a statement is not proof of absence.**
      This is listed as unresolved in `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md`
      §7.5. Do **not** go hunting by writing registers you have not identified.
      Escalate: contact the manufacturer or its integrator with the model,
      firmware version and the exception observed.
- [ ] Outcome: the profile cannot be promoted, the plant remains
      monitoring-only, and that must be reported plainly.

---

## 5. Measuring what the manuals do not state: settle time and readback semantics [WRITE — low power]

**No settle or response time for a percentage command is documented anywhere in
the available manual.** The firmware's default settle window (500 ms) and
confirmation deadline (5000 ms) are firmware-side acquisition windows, **not
manufacturer values**.

**Why this matters, concretely:** the write-confirmation logic reads the setpoint
back and compares it to what was requested. If the machine takes longer to apply
the command than the settle window allows, a perfectly accepted command is judged
`MISMATCHED`. That latches a confirmation fault, removes the inverter from
commandable capacity and **drives it to zero**. This is not hypothetical: it is
exactly what happened against the lab simulator, which defers a 40125 write by
about 1500 ms and reports the previous active limit until it applies. Under the
old global 500 ms window a healthy device was falsely faulted. The fix was to
make the window a **per-profile** property (`power_limit_settle_ms`); the Huawei
manufacturer profile leaves it at the firmware default **because there is no
evidence to set it from**. You are here to produce that evidence.

### 5.1 Method

Use the register that §4 established as the working control register.

For **each** trial:

- [ ] Start a continuous poll of the **readback** register in one terminal,
      logging with timestamps, at the fastest interval the link sustains without
      errors:

  ```
  mbpoll -a <unit_id> -0 -r <control_register> -c 1 -t 4 -l 200 <inverter_ip>
  ```

  Record the poll interval you actually used and whether it produced errors:
  `____` ms, errors `____`.
  **Note the measurement floor:** your poll interval bounds your resolution. A
  200 ms poll cannot distinguish 50 ms from 150 ms. Record the floor and do not
  report a precision you did not have.

- [ ] Also poll **active power** (32080) in a second terminal, same way. The
      setpoint register and the actual power do **not** change at the same time,
      and both timings matter.

- [ ] Write a new target, recording the wall-clock time of the write to the
      millisecond if you can (`date +%H:%M:%S.%3N` immediately before, or use
      `mbpoll`'s own output timestamps).

Record per trial:

| Trial | Target % | Write time | First readback showing the NEW value | Δt (ms) | First change in active power | Active power settled |
|---|---|---|---|---|---|---|
| 1 | `____` | `____` | `____` | `____` | `____` | `____` |
| 2 | `____` | `____` | `____` | `____` | `____` | `____` |
| 3 | `____` | `____` | `____` | `____` | `____` | `____` |
| 4 | `____` | `____` | `____` | `____` | `____` | `____` |
| 5 | `____` | `____` | `____` | `____` | `____` | `____` |

- [ ] **At least five trials**, spanning increases and decreases, small steps and
      larger ones. The window must cover the **slowest** case you observed, not
      the average.
- [ ] Maximum Δt observed: `____` ms
- [ ] Recommended `power_limit_settle_ms` for this profile: `____` ms — the
      maximum observed **plus margin**. State the margin and your reasoning:
      `____`
- [ ] **Constraint from the firmware:** a profile settle value is clamped to
      strictly below the 5000 ms confirmation deadline. If your measured maximum
      is at or beyond 5000 ms, a settle window alone cannot accommodate this
      machine — **raise it as a design question**, do not quietly pick 4999.
      Applies here? `____`

### 5.2 Does the readback report the *requested* or the *active* value?

This is unresolved in the repository (`HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §7.2:
the simulator reports the **active** value; the manual does not say). It changes
what "confirmed" means.

- [ ] During the Δt window — after the write, before the machine has applied it —
      what does the readback register show?
  - The **new requested** value immediately → the register echoes the request.
    Readback confirms only that the command was *stored*, not that it is *in
    effect*. Observed: `____`
  - The **previous** value until the change takes effect → the register reports
    the **active** limit. Readback confirms the command is genuinely applied, and
    the settle window is doing real work. Observed: `____`
- [ ] Verdict: `____`
- [ ] Evidence: attach the timestamped poll log showing the transition. File:
      `____`
- [ ] If the readback merely echoes the request, note in your report that
      confirmation via this register **cannot** prove the limit is active, and
      that active power (32080) is the only real evidence. That is a material
      finding about how much the confirmation mechanism is worth on this machine.

### 5.3 Repeat per machine model

- [ ] Was the settle time measured on **every** distinct inverter model on site,
      or extrapolated from one? `____`
      Different models and different firmware versions may differ. A value
      measured on one machine is evidence about **that** machine. Say so.

---

## 6. The inverter's own ramp limiter — signal 432 at 42017 [RO, then optionally CFG/WRITE]

The manual documents signal 432, "active power gradient", RW U32 at **42017**, 2
registers, unit **%/s**, gain 1000.

This firmware ramps in the **control engine** and does **not** write 42017. Two
independent rate limiters in series interact: **the slower one dominates**. A
controller ramp faster than the inverter's own gradient will simply not be
achieved, and it will look like a tracking failure rather than a configuration
mismatch.

### 6.1 Read it [RO]

```
mbpoll -a <unit_id> -0 -r 42017 -c 2 -t 4 <inverter_ip>
```

- [ ] Raw words as returned: `____`
- [ ] Interpreted as U32, high word first, gain 1000 → `____` %/s
- [ ] Interpreted low word first, for comparison → `____` %/s
- [ ] Which is plausible as a %/s rate for this machine? `____`
      **You cannot resolve U32 word order from a single reading if one
      interpretation is not absurd.** If both are plausible, resolve it by
      observation in §6.3, not by preference.
- [ ] Do all inverters report the same gradient? List any that differ: `____`

### 6.2 Read the controller's configured ramp [RO]

- [ ] `GET /api/config` → `control.generator_ramp`:
      `enabled` `____`, `up_percent_per_second` `____`,
      `down_percent_per_second` `____`
- [ ] `control.grid_ramp`: `enabled` `____`, up `____`, down `____`
- [ ] Note the firmware's own rule, enforced by the commissioning gate: the
      generator ramp must be **enabled**, both rates must be `> 0` and `<= 100`
      %/s, and the **down rate must be at least the up rate** — reducing PV is the
      direction that protects the generator. A disabled generator ramp blocks
      commissioning deliberately: "no rate limit" must be a decision, not a
      default.

### 6.3 Reconcile them

- [ ] Compare: inverter gradient `____` %/s vs controller generator ramp up
      `____` %/s / down `____` %/s.
- [ ] Which is slower in each direction? `____`
- [ ] Observe it: command a step change (as in §5) and measure the actual rate of
      change of active power: `____` %/s. Compare with both configured rates.
      **This is also how you settle the word order from §6.1** — the observed rate
      should be consistent with one interpretation and not the other, provided the
      inverter's gradient is the slower limiter.
- [ ] Decide, deliberately, and record who decided:
  - **Option A — the inverter owns the rate.** Leave 42017 alone. Set the
    controller's ramp no faster than the inverter's gradient, so the controller
    does not ask for a rate that cannot be delivered. Chosen? `____`
  - **Option B — the controller owns the rate.** Requires writing 42017, which is
    a **[WRITE]** step, changes a persistent inverter setting, and needs explicit
    authorisation. Do not do this on your own judgement. Authorised by: `____`
    Old value recorded for rollback (§1.5): `____`
- [ ] Whichever is chosen, the **generator-protecting direction (down) must not be
      rate-limited into uselessness.** If the inverter's own gradient is slower
      than the generator can tolerate a PV loss, that is a plant-level finding for
      the product owner, not something to tune away. Recorded? `____`

---

## 7. First controlled write through the controller [WRITE — minimum power]

§4 and §5 wrote registers **directly with mbpoll**. This section is the first time
the **controller** commands an inverter, which is a different risk: the controller
writes repeatedly, on its own schedule, based on meter readings.

### 7.1 Preconditions — all must hold

- [ ] §4 verdict is clean: one register established as the control register, or a
      documented decision under §4.6.
- [ ] §5 complete: a measured settle window and known readback semantics.
- [ ] §6 complete: the two rate limiters reconciled.
- [ ] The measured settle window has been **applied to the profile in firmware**.
      If it has not, the controller is still using the firmware default and the
      false-fault scenario of §5 is live. Applied? `____` By whom? `____`
      If it has not been applied, **do not proceed** — go no further than §4/§5
      manual writes and report.
- [ ] Meter data is fresh: `GET /api/status`, grid data age `____` ms. Meter
      quality `____`.
- [ ] Source detection reports the correct source: `GET /api/source-detection`,
      resolved source `____`, and it matches what is physically true `____`.
- [ ] **One inverter enabled only.** Every other inverter disabled in
      `POST /api/inverters/config`. Which one: `____`
- [ ] Generator limits from §1.2 configured via `POST /api/solar-grid/config`
      (`generator_rated_kw`, `generator_minimum_loading_percent`,
      `generator_reserve_kw`, `generator_reverse_power_margin_kw`). Confirmed by
      reading back: `____`
- [ ] The abort path (§11) rehearsed **on this visit**, not merely read.

### 7.2 Understand the authority you are about to use

The controller will refuse to write unless the assigned profile passes the write
gate. Today, **no manufacturer profile is production-approved**, so there are only
two ways the controller can command anything:

1. **Lab authority.** An authenticated engineer declares a specific inverter's
   endpoint to be a **Modbus simulator**. That declaration, and nothing else,
   unlocks a command through a non-production-approved profile, and it grants
   `lab_simulator_only` authority. Any such declaration anywhere in the commanded
   fleet makes the whole commissioning verdict `lab_simulator_only`.
2. **Production authority.** The profile is production-approved *and* carries a
   readback register. That requires §8 to have been completed first.

> **Do not declare a real inverter a lab target in order to make the controller
> write to it.** That is a deliberate false statement about physical reality, it
> is the one thing the safety design cannot defend against, and it would produce a
> `lab_simulator_only` verdict that misrepresents a live plant. If the controller
> refuses to write, the correct response is §8, not a declaration.

- [ ] Which authority applies to this first controlled write? `____`
- [ ] If the answer is "lab", **stop**: you are at a real plant. Go to §8.

### 7.3 The write

- [ ] Set the plant target so the **commanded PV is at or near the controller's
      minimum**, not at whatever the loop would naturally ask for. Configure via
      `POST /api/solar-grid/config` / `POST /api/config` so the first closed-loop
      action is small. What was set, and what commanded percentage do you expect?
      `____`
- [ ] Record everything immediately before enabling: active power `____` kW,
      setpoint register `____`, grid/generator meter `____` kW, wall-clock `____`
- [ ] Enable automatic control (`POST /api/config` with `control.enabled` true).
- [ ] **Watch, do not touch, for at least one full ramp plus the settle window.**

### 7.4 What must be observed before proceeding

All of these, or you abort:

- [ ] `GET /api/status`: `control_enabled` true, `commissioned` true,
      `commissioning_scope` `____`, `inhibit_reason` empty.
- [ ] `GET /api/inverters/write-confirmation`: the inverter progresses
      `pending` → **`confirmed`**. Record the states seen and their timings:
      `____`
- [ ] It does **not** report `mismatched` or `unverified`. If it does, abort
      (§11) — and note that `mismatched` and a lapsed deadline both demand a
      safe-zero, so the firmware will already be driving the machine to zero.
      Investigate the settle window (§5) before retrying.
- [ ] Active power moves in the **commanded direction** and stops where commanded.
      Independent measurement agrees: `____` kW.
- [ ] The rate of change is no faster than the reconciled ramp (§6): `____` %/s
- [ ] No new alarms: `GET /api/operator/alarms` compared with the §1.5 baseline.
      New conditions: `____`
- [ ] The grid/generator meter moves towards its target **without overshoot into
      export or into reverse power**. Observed extremes: `____`
- [ ] No oscillation. Watch for at least `____` minutes (record how long) and
      confirm the setpoint is not hunting. If it hunts, the tuning is wrong —
      abort and report; do not tune by feel while the plant is live.

### 7.5 Only then, widen

One change at a time, re-checking §7.4 after each:

- [ ] Raise the commanded level in steps. Steps used: `____`
- [ ] Enable the second inverter. Re-check §7.4 in full.
- [ ] Continue one inverter at a time. Record after each: `____`
- [ ] **Never** enable the remaining fleet in one action.

---

## 8. Promotion criteria: documented → production approved

A profile's qualification level is a **claim about evidence**. The ladder in
`components/inverter_manager/include/inverter_profiles.h` is:

`Documented` → `Simulator verified` → `Bench verified` → `Read-only qualified` →
`Write qualified` → `Production approved`

Only **`Production approved`** (and only for a non-simulator profile carrying a
readback register) grants `INVERTER_WRITE_PRODUCTION`. A lab-target declaration
**never** raises a profile's qualification and never grants production authority —
that is enforced in `inverter_profile_write_permission()`, which consults the lab
declaration only *after* the production predicate has already failed.

### 8.1 Evidence required before promotion

Every item must be a **file or photograph in the evidence bundle**, not a
recollection:

- [ ] The **exact manual**, with revision, matching the **exact model and
      firmware version** on site. Manual: `____` Model: `____` Firmware: `____`
- [ ] Identity register read back and matching the nameplate (§3.2)
- [ ] Active power decoded and cross-checked against an independent measurement,
      over at least three samples at different output levels (§3.3)
- [ ] The control register **determined by observation**, not by assumption (§4),
      with at least three trials
- [ ] Settle time measured over at least five trials, with the maximum recorded
      and a stated margin (§5.1)
- [ ] Readback semantics established: requested vs active (§5.2)
- [ ] Ramp reconciliation recorded, with the decision and who made it (§6)
- [ ] A commanded write, confirmed by readback **and** by an independent power
      measurement, at low power (§4 or §7)
- [ ] A commanded return to unlimited, confirmed the same way
- [ ] Behaviour on **loss of communication** mid-command observed and recorded
      (§9.4)
- [ ] The safe-zero path exercised: a deliberate mismatch or a comms loss drives
      the machine to its safe fallback. How it was induced: `____`
- [ ] Timestamped raw `mbpoll` transcripts for every measurement above
- [ ] Every open item in `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §7 either
      closed with evidence or **explicitly carried forward as still open**, with a
      statement of what the plant does in the meantime

### 8.2 What promotion may NOT rest on

- [ ] Simulator behaviour. A model is not the machine. Confirmed not relied upon.
- [ ] Another site, another model, or another firmware version.
- [ ] A single trial of anything.
- [ ] "It seemed to work." Every claim traces to a recorded measurement.
- [ ] The Device Status mapping — that remains **unknown** (§3.5), and promotion
      must not silently assume otherwise. Every inverter continues to report
      `UNKNOWN` state, and `fleet_synchronised()` stays unwired.

### 8.3 Sign-off

Promotion is a **firmware change** — the qualification level lives in
`inverter_profiles.c` — so it goes through review, not through a field edit.

| Role | Name | Date | Signature |
|---|---|---|---|
| Commissioning engineer (produced the evidence) | `____` | `____` | `____` |
| Firmware reviewer (verified the evidence supports the level, and made the change) | `____` | `____` | `____` |
| Product owner (accepted the residual risk, incl. any items still open in §7 of the register evidence doc) | `____` | `____` | `____` |

- [ ] Promotion recorded in `docs/RELEASE_READINESS.md` with the commit that made
      the change.
- [ ] The evidence bundle archived where it can be found in two years: `____`

**If the evidence is incomplete, the profile stays where it is.** A plant running
monitoring-only is a correct outcome of a commissioning visit. A plant running
closed-loop control on unqualified evidence is not.

---

## 9. The nine commissioning prerequisites → a PRODUCTION verdict

The gate is defined in `components/commissioning_gate/commissioning_gate.c`. It is
**fail-closed**: any prerequisite whose state the controller could not read is
`UNMET`, never assumed satisfied. It says only that the *configuration* the
controller needs is present, self-consistent and qualified — runtime evidence
(meter freshness, grid evidence, source settling, active alarms) is checked
separately every cycle, and **both** must hold before a command is issued.

### 9.1 Reading the gate

```
GET /api/commissioning/gate      (Engineering scope; 401 unauthenticated)
```

Authenticate first via `POST /api/engineering/login` — this sets an `eng_session`
cookie, which you must carry on subsequent requests. `GET /api/status` also
carries a summary: `commissioned`, `commissioning_scope`,
`commissioning_unmet_count`, `commissioning_first_unmet`.

`first_unmet` is the **lowest-numbered** unmet prerequisite, and it is what the
control engine publishes as its inhibit reason. Work the list in order; fixing
the first often reveals the next.

### 9.2 The nine, in evaluation order

Work top to bottom. Tick only what you have verified by reading the gate back.

**1. `meter_roles` — "Meter roles assigned"**
- [ ] Exactly **one** enabled meter has the grid role, and no two meters claim the
      same generator slot.
- [ ] `POST /api/meters/config`; verify with `GET /api/commissioning/gate`.
- Reasons: `grid_meter_missing` (none assigned) · `grid_meter_ambiguous` (more
  than one) · `generator_slot_duplicate` (two meters, same slot).

**2. `inverter_profile_qualified` — "Inverter profile qualified for writing"**
- [ ] Every enabled inverter carries a profile that passes the production write
      gate, **or** is an explicitly declared lab target. One unqualified inverter
      blocks the whole plant, deliberately.
- [ ] For a **PRODUCTION** verdict this means §8 has been completed. There is no
      other route.
- Reasons: `no_enabled_inverter` · `profile_not_write_qualified`.

**3. `write_readback` — "Setpoint readback available"**
- [ ] Every enabled inverter's profile carries a manual-verified readback
      register. A command that cannot be read back can never be confirmed.
- Reason: `readback_unavailable`.

**4. `fleet_capacity` — "Fleet capacity commissioned"**
- [ ] The summed rated power of write-qualified enabled inverters is positive and
      finite. This is **configuration**, not live availability — it is `rated_kw`
      in `POST /api/inverters/config`.
- [ ] Rated kW entered per inverter matches the nameplates from §1.1: `____`
- Reason: `capacity_not_commissioned`.

**5. `ramp_policy` — "Generator ramp policy commissioned"**
- [ ] Generator ramp **enabled**, both rates in `(0, 100]` %/s, and
      **down >= up**.
- [ ] Rates reconciled with the inverter's own gradient (§6): `____`
- Reason: `ramp_policy_invalid` (covers disabled, out of range, and down < up —
  the reason code does not distinguish them, so check all three).

**6. `source_detection` — "Source detection configured"**
- [ ] Either explicit Modbus grid evidence (`grid_available` /
      `grid_breaker_closed` signals in the Solar-Grid config) **or** measured
      source detection is configured. Explicit evidence is the stronger of the
      two; prefer it if the plant can provide it.
- [ ] Verified against physical reality: force or observe a real source change and
      confirm the controller's resolved source follows (§10.3).
- Reason: `source_evidence_unconfigured`.

**7. `grid_policy` — "Grid policy valid"**
- [ ] The persisted Solar-Grid policy is present and valid: policy
      (zero-export / limited-export / minimum-import) `____`, meter orientation
      `____` (from §1.3 — **backwards inverts the loop**), and the associated
      limit `____`.
- Reason: `grid_policy_invalid`.

**8. `generator_limits` — "Generator limits commissioned"**
- [ ] `generator_rated_kw` positive and finite, and
      `generator_minimum_loading_percent` in `(0, 100]`. **Both from §1.2, from
      the genset documentation.** Zero means "not commissioned" and holds PV at
      zero; there is no safe default.
- Reasons: `generator_rating_unknown` · `generator_loading_unknown`.

**9. `control_tuning` — "Control tuning valid"**
- [ ] `kp > 0`, `ki >= 0`, `deadband_kw >= 0`, `interval_ms` in `[100, 10000]`,
      and `meter_stale_timeout_ms >= interval_ms`.
- [ ] These are **range checks, not a tuning verdict.** Passing this prerequisite
      does not mean the loop is well tuned; §7.4's no-oscillation observation is
      what tests that.
- Reason: `control_tuning_invalid`.

### 9.3 The scope verdict — the part that matters most

- [ ] `GET /api/commissioning/gate` → `commissioned` `____`, `scope` `____`
- Scope is decided by the **weakest link**, and only ever after every prerequisite
  is otherwise satisfied:
  - `none` — not commissioned. Automatic control inhibited.
  - `lab_simulator_only` — commissioned, but **at least one** commanded inverter is
    a declared Modbus simulator. **Nothing observed is evidence about physical
    equipment.** One declared simulator anywhere makes the entire verdict LAB even
    if every other inverter is production-qualified.
  - `production` — every commanded inverter passed production write qualification.
- [ ] If the verdict is `lab_simulator_only` at a real plant, the plant is **not
      commissioned for production**. Find the declared lab target and revoke it
      (`/api/inverter-profile-assignment`), then re-read the gate. Note that
      declaring or revoking a lab target disables automatic control, exactly as
      changing a profile does — so it can never take effect underneath a running
      loop, and you will need to re-enable control deliberately.
- [ ] Final verdict recorded, with the timestamp and the raw gate response saved
      to the evidence bundle: `____`

### 9.4 Communication loss

Not a gate prerequisite, but required evidence for §8 and worth doing here.

- [ ] With control running at low power, **unplug the inverter's network
      connection** (the least destructive way to induce a comms loss; do not power
      down the inverter).
- [ ] Record what the controller does and how long it takes: `____`
- [ ] Confirm the write confirmation goes to `unverified` (deadline elapsed with
      no usable post-write sample) and that a safe-zero is demanded: `____`
- [ ] Record what the **inverter** does when its master disappears while a limit
      is in force: does it hold the limit, time out to unlimited, or something
      else? `____`
      **This is a safety-relevant behaviour and it is not documented anywhere in
      this repository.** Whatever you observe, record it and report it.
- [ ] Reconnect. Confirm recovery is clean and that control does not resume
      abruptly at a stale setpoint: `____`

---

## 10. Generator-specific checks [WRITE — the highest-consequence section]

This is the part where a mistake damages an engine rather than losing production.
**Do not start it until §7 has been completed cleanly on the grid or on a source
that can absorb error.**

**Generator ratings have NOT been supplied to this project.** Everything below
depends on the values you captured in §1.2. If §1.2 is incomplete, this section
cannot be performed — and the commissioning gate will already be refusing, which
is the correct behaviour, not an obstacle.

- [ ] §1.2 complete and entered into the controller. Confirmed by reading back
      `GET /api/solar-grid/config`: `____`

### 10.1 Minimum loading

- [ ] Genset minimum loading, from §1.2: `____` % of `____` kW = `____` kW
- [ ] Who supplied and authorised that figure: `____`
- [ ] With the generator carrying the plant and PV curtailed to near zero, record
      the generator load: `____` kW
- [ ] Raise PV in **small steps** (use the smallest step the controller allows),
      recording at each step:

  | Step | Commanded PV % | PV kW | Generator load kW | Generator load % of rated | Notes |
  |---|---|---|---|---|---|
  | 1 | `____` | `____` | `____` | `____` | `____` |
  | 2 | `____` | `____` | `____` | `____` | `____` |
  | 3 | `____` | `____` | `____` | `____` | `____` |
  | 4 | `____` | `____` | `____` | `____` | `____` |

- [ ] **Stop and hold** as soon as generator load approaches the minimum-loading
      figure. Confirm the controller stops raising PV **of its own accord** before
      the limit is breached. Did it? `____` At what generator load? `____`
- [ ] If the controller allowed the generator below its minimum loading, **abort
      immediately** (§11) and report it as a defect. Do not continue tuning around
      it.
- [ ] Record any observation from the genset itself — audible load change,
      governor hunting, exhaust temperature if instrumented, engine alarms:
      `____`

### 10.2 Reverse-power margin

- [ ] The generator's own reverse-power protection setting from §1.2:
      `____` kW at `____` s delay. If the genset has **no** reverse-power
      protection, record that — it makes the controller's margin the only defence
      and raises the stakes on everything below.
- [ ] The controller's configured `generator_reverse_power_margin_kw`: `____`
- [ ] Is the controller's margin comfortably **more conservative** than the
      genset's trip setting? `____` If not, resolve with the product owner before
      proceeding. Two things must be true at once: the controller must stop well
      short of the trip, and the genset's protection must remain the last line of
      defence, untouched.
- [ ] **Do not deliberately drive the plant into reverse power to test the trip.**
      Approach the margin under control, in small steps, and confirm the
      controller holds short of it. Closest generator load approached: `____` kW.
      Did the controller hold? `____`
- [ ] Record the minimum generator load the controller permitted across the whole
      session: `____` kW

### 10.3 Generator start / stop and source changeover

Coordinate with whoever operates the genset. Do not start or stop a generator on
your own initiative.

**Generator start (with the plant on grid, PV running):**
- [ ] Controller's resolved source before: `____`
- [ ] Start the generator per site procedure. Time: `____`
- [ ] Time for the controller's resolved source to change: `____` s
- [ ] Did PV stay within limits appropriate to the **new** source throughout the
      transition? `____`
- [ ] Any transient overshoot on either meter: `____`

**Generator stop / return to grid:**
- [ ] Controller's resolved source before: `____`
- [ ] Stop per site procedure. Time: `____`
- [ ] Time for resolved source to change: `____` s
- [ ] PV behaviour during changeover: `____`

**Loss of the carrying source (only if the site's own procedures permit a
controlled test, and only with the genset operator's agreement):**
- [ ] What happened: `____`
- [ ] Did PV reduce fast enough to protect the remaining source? `____`
- [ ] Was the down ramp (§6) the limiting factor? `____`

- [ ] **The rule the firmware enforces, and what you are checking:** PV must never
      be left commanded against the **outgoing** source during a changeover. Was
      that true in every transition observed? `____` If not, abort and report.
- [ ] Record every transition, both directions, at least twice each: `____`

### 10.4 Parallel gensets

- [ ] Do gensets run in parallel at this site? `____`
- [ ] If yes: is minimum loading a **per-engine** or **aggregate** figure, and
      which does the controller's single `generator_rated_kw` represent? `____`
      This is a real ambiguity — the controller holds one rating and one minimum
      loading percentage. If the plant can run 1, 2 or 3 engines, the commissioned
      rating is only correct for one of those cases. **Raise it with the product
      owner rather than picking a case.** Decision and who made it: `____`

---

## 11. Abort and rollback

**Rehearse this before §4. Have it in a terminal you can reach in one keystroke.**

### 11.1 Abort ladder — fastest first

| # | Action | Effect | When |
|---|---|---|---|
| A1 | `POST /api/config` with `control.enabled` false | Controller stops commanding. Inverters hold their last commanded limit. | Any doubt about controller behaviour |
| A2 | Write the recorded original limit (normally raw 1000 = 100 %) directly with `mbpoll` | Removes the curtailment regardless of what the controller thinks | Inverters left curtailed and you need output back |
| A3 | Disable the inverter in `POST /api/inverters/config` | Controller stops addressing it entirely | One machine is misbehaving |
| A4 | Revoke the lab-target declaration / reassign the profile | Automatic control is disabled as a side effect, by design | Wrong authority in force |
| A5 | Site emergency stop / breaker, per **site** procedure | Plant-level | Danger to people or equipment — and this outranks everything above |

**A5 is not the controller's decision and not this document's. Follow the site's
own emergency procedure and the site's own chain of command. Nothing in this
runbook overrides it.**

- [ ] A1 tested this visit: `____`
- [ ] A2 tested this visit, with the original values from §1.5: `____`
- [ ] A5 procedure read and understood; who to call: `____`

**Note on A1:** disabling control does **not** restore the inverters — they hold
the last limit written. Aborting therefore normally means **A1 then A2**. Never
walk away after A1 alone.

### 11.2 Per-stage rollback

| Stage | To roll back |
|---|---|
| §1 (record) | Nothing written. Nothing to roll back. |
| §2–§3 (read-only) | Nothing written. Nothing to roll back. |
| §4 (register decision) | Restore 40125 **and** 40199 to the §1.5 values. Confirm active power recovers. |
| §5 (timing) | Restore the control register to its §1.5 value. |
| §6 (ramp) | If 42017 was written, restore the §1.5 value. Restore the controller's ramp config from the §1.5 `GET /api/config` backup. |
| §7 (first controlled write) | A1, then A2. Restore the config backup. Confirm `control_enabled: false` and PV unlimited. |
| §9 (gate) | Restore each config from the §1.5 backups: meters, inverters, solar-grid, source-detection, main config. |
| §10 (generator) | A1 then A2 immediately. Tell the genset operator what you did and what state you left it in. |

- [ ] After **any** rollback, verify by reading back — never assume a restore took
      effect: setpoint register `____`, active power `____` kW,
      `control_enabled` `____`, alarms compared with the §1.5 baseline `____`.

### 11.3 Leaving the plant safe if commissioning stops halfway

This is the most likely outcome of a first visit and it is a legitimate one. Work
through **all** of it before leaving site.

- [ ] Automatic control disabled: `control_enabled` `____`
- [ ] Every inverter's power-limit register restored to its §1.5 value —
      **verified by reading it back**, per inverter:

  | Inverter | 40125 restored | 40199 restored | 42017 restored | Verified by readback |
  |---|---|---|---|---|
  | 1 | `____` | `____` | `____` | `____` |
  | 2 | `____` | `____` | `____` | `____` |
  | 3 | `____` | `____` | `____` | `____` |

- [ ] Active power at each inverter is at its natural, uncurtailed level: `____`
- [ ] Controller configuration is either (a) fully restored from the §1.5 backups,
      or (b) left in a **deliberate, documented** partial state. Which, and why:
      `____`
- [ ] If (b): the commissioning gate must be **closed**. Confirm
      `commissioned: false` and record `first_unmet` and its reason, so the next
      engineer knows exactly where the procedure stopped: `____`
      A closed gate is the safe state. Do not leave a gate open on partial
      evidence to save the next visit some work.
- [ ] Genset protection settings untouched, and confirmed untouched: `____`
- [ ] Every alarm you caused is either cleared or documented. Note that a
      condition which cleared but was never acknowledged stays visible as
      `rtn_unacknowledged` — that is correct behaviour, not a bug, and the next
      person will see it. List what you leave behind: `____`
- [ ] Site operators told: what state the plant is in, what still works
      (monitoring), what does not (automatic control), and who to call. Told
      whom, at what time: `____`
- [ ] Evidence bundle copied off the laptop to somewhere it will survive: `____`
- [ ] Handover note written, including every blank in this runbook you could not
      fill and why: `____`

---

## 12. What must go in the report

- [ ] Every blank in this runbook, filled or explicitly marked "not measured, and
      why".
- [ ] The §4 verdict on 40125 vs 40199, with the raw transcripts.
- [ ] The measured settle window, the number of trials, the maximum observed, the
      margin chosen, and your measurement resolution floor.
- [ ] The readback semantics verdict (requested vs active).
- [ ] The inverter's ramp gradient and the reconciliation decision, with who made
      it.
- [ ] The generator ratings and minimum loading, **with their source** — these
      were previously unavailable to the project and are the single largest gap
      this visit closes.
- [ ] The final commissioning verdict and its **scope**.
- [ ] Which items in `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §7 you closed and
      which remain open.
- [ ] Any behaviour that contradicted the manual. Say so plainly — a
      contradiction recorded is worth more than a clean report that smoothed it
      over.
- [ ] An explicit statement of whether any profile is now eligible for promotion
      (§8), and if not, precisely what is missing.
- [ ] Anything you did that this runbook did not anticipate, and why.

---

## Appendix A — endpoints used

Engineering-scope endpoints require `POST /api/engineering/login` first and the
resulting `eng_session` cookie on subsequent requests; unauthenticated requests
return 401. The password is **obtained from the product owner** and is not
recorded here.

| Purpose | Endpoint |
|---|---|
| Live status, control authority, gate summary | `GET /api/status` |
| Full config (backup) | `GET /api/config` · `POST /api/config` |
| Solar-Grid policy and generator limits | `GET/POST /api/solar-grid/config` |
| Solar-Grid live status | `GET /api/solar-grid/status` |
| Source detection | `GET/POST /api/source-detection` |
| Meters | `GET/POST /api/meters/config` |
| Inverters (enable, host, port, unit id, rated kW, timeout) | `GET/POST /api/inverters/config` |
| Profile catalogue and qualification levels | `GET /api/inverter-profiles` |
| Profile assignment / lab-target declaration | `POST /api/inverter-profile-assignment` |
| Read-only inverter probe | `POST /api/inverter-probe` |
| Inverter telemetry | `GET /api/inverter-telemetry` |
| **Commissioning gate detail** | `GET /api/commissioning/gate` |
| **Write confirmation state** | `GET /api/inverters/write-confirmation` |
| Alarms | `GET /api/operator/alarms` · `POST /api/operator/alarms/ack` |
| Audit log | `GET /api/system/audit-log` |
| Firmware identity | `GET /api/system/identity` |

## Appendix B — Huawei registers referenced, and their standing

Every address below is **transcribed from the manual and unqualified against
hardware**. Source: `Huawei Inverter Modbus Interface Definitions (V3.0)`, Issue
01 (2023-01-17). Full attribution in
`docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md`.

| Manual ref | Address | FC | Words | Type | Unit | Gain | Standing |
|---|---:|---:|---:|---|---|---:|---|
| reg 1, "Model" | 30000 | 03 | 15 | STR | — | — | Identity probe reads word 0, expects `0x5355` |
| signal 171, active power | 32080 | 03 | 2 | I32 | kW | 1000 | Firmware scale 0.001, word order AB — **verify per §3.3** |
| signal 178, Device Status | 32089 | 03 | 1 | E16 | — | — | **Code table unavailable.** Deliberately unused; state is always UNKNOWN |
| signal 409, % derating | 40125 | 06 | 1 | I16 | % | 10 | Currently commanded by the firmware. **Open — §4** |
| signal 410, fixed derating | 40126 | 06 | 2 | U32 | W | 1 | Not used |
| signal 419, % control | 40199 | 06 | 1 | I16 | % | 10 | Anti-backcurrent interface. **Open — §4** |
| signal 432, active power gradient | 42017 | 06 | 2 | U32 | %/s | 1000 | Not written by the firmware. **Read and reconcile — §6** |

## Appendix C — every value this runbook refuses to supply

Each of these is a blank because **no documented value exists in this repository
or in the available manuals**. If a later revision of this runbook contains a
number in any of these rows, that number needs a citation.

| Value | Why it is blank |
|---|---|
| Which register controls active power (40125 or 40199) | Manual documents both with overlapping purpose; never tested on hardware |
| Setpoint settle time | **No settle or response time is documented anywhere in the manual.** Firmware default 500 ms is a firmware-side window, not a manufacturer value |
| Whether readback reports requested or active value | Manual silent; simulator reports active; hardware untested |
| Readback tolerance appropriate to real hardware | Profile carries 0.2 %, derived from the simulator, not from a manual |
| Inverter's active power gradient (42017) as configured on site | Site-specific setting; must be read |
| U32 word order at 42017 | Not stated; not resolvable from a single read |
| Whether a mode/enable register is required before a limit applies | Manual states none, but silence is not proof |
| Device Status code meanings | Code table is in a document not available to this project |
| Generator rated kW | Never supplied to the project |
| Generator minimum loading % | Never supplied; an engine limit, not a controller preference |
| Generator reserve kW | Product-owner decision, informed by the above |
| Reverse-power margin kW | Product-owner decision; genset trip setting must be read on site |
| Genset reverse-power trip setting and delay | Site equipment; must be read |
| Whether a SmartLogger is in the Modbus path | Unknown; changes unit ids and addressing if present |
| Whether anything else writes the power-limit register | Unknown; must be established on site |
| Control loop tuning (kp, ki, deadband) appropriate to this plant | Gate range-checks them; nothing has tuned them against this plant |
| Inverter behaviour on loss of its Modbus master | Not documented; must be observed |
| Whether minimum loading is per-engine or aggregate on a parallel installation | Ambiguous; the controller holds a single rating |
