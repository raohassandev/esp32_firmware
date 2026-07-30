# Site acceptance test — Automatrix PV-DG Controller

**Audience:** the commissioning engineer and the site's own operations staff,
at the plant, with the plant live.
**Executed:** **after** commissioning, i.e. after
`docs/SITE_COMMISSIONING_RUNBOOK.md` has been worked through and its report
written. This document assumes that happened.
**Purpose:** demonstrate that the installed system does what was promised.

---

## 0. What this document is, and what it is not

A SAT **verifies**. It does not configure, tune, decide a register, measure a
settle time, or fill in a generator rating. All of that is commissioning, it lives
in `docs/SITE_COMMISSIONING_RUNBOOK.md`, and if any of it is still outstanding the
SAT cannot start — go back to the runbook.

Every step is tagged:

| Tag | Meaning |
|---|---|
| **[RO]** | Read-only. Observes the plant. Cannot move power. |
| **[MOVE]** | **Moves real power.** Requires §1.4's abort path in place and the named authority present. |
| **[PLANT]** | Requires a plant action (start/stop a genset, open a breaker, pull a cable) that is **not** the controller's and **not** this document's to authorise. |

**This document contains no generator ratings, no tolerances, no timing values and
no register addresses of its own.** Every quantity is either read from the
commissioning report or **measured and recorded here**. If a later revision of
this document contains a number in a blank, that number needs a citation.

### 0.1 The precondition that decides which half of this SAT you can run

`docs/RELEASE_READINESS.md` §1: **zero inverter profiles are production-approved**,
so automatic control is structurally inhibited against physical equipment. That is
by design.

Consequently this document has two parts, and which one applies is decided by the
commissioning report, not by preference:

| | Runs when | What it demonstrates |
|---|---|---|
| **Part A — §§2–4** | Always. No profile qualification needed. | Monitoring, metering, source detection, protection refusal, alarms, hand-over. This is a complete and legitimate SAT for a **monitoring, commissioning and protection** installation. |
| **Part B — §§5–11** | **Only** after at least one profile reached `Production approved` through `docs/SITE_COMMISSIONING_RUNBOOK.md` §8, with the evidence bundle archived. | Closed-loop control: grid mode, generator mode, ramp, minimum loading, reverse power, comms loss, controller loss, source transition. |

- [ ] Which part is in scope for this visit? **A only / A and B:** `______`
- [ ] If A and B: name the production-approved profile, the commit that promoted
      it, and where the §8 evidence bundle is archived:
      profile `______`, commit `______`, bundle `______`
- [ ] If A only: record that closed-loop control is **not accepted** by this SAT
      and that the plant is handed over as monitoring-only. Recorded: `______`
      **This is not a failure.** A plant running monitoring-only on honest
      evidence is a correct outcome.

### 0.2 What must exist before the SAT starts

- [ ] The signed commissioning report, with every blank in
      `docs/SITE_COMMISSIONING_RUNBOOK.md` either filled or explicitly marked
      "not measured, and why". Report reference: `______`
- [ ] The signed factory acceptance test for **this unit**
      (`docs/FACTORY_ACCEPTANCE_TEST.md`). Serial: `______`, FAT verdict: `______`
- [ ] `GET /api/system/identity` — the firmware commit running on the unit:
      `______`. **It must be the commit the FAT was executed against.** Match:
      `______`
- [ ] The commissioning gate reads `commissioned: true` with a recorded scope:
      `______`
      **If `scope` is `lab_simulator_only` at a real plant, stop.** A declared lab
      target on real equipment is a false statement about physical reality; find it
      and revoke it (runbook §9.3) before any part of this SAT.
- [ ] The §1.5 configuration and inverter-register backups from the runbook exist
      as files. Location: `______`
- [ ] Independent measurement available — a clamp meter or the plant's own
      metering, not the controller. What is being used: `______`
- [ ] The Engineering password, **obtained from the product owner**. Confirmed
      available: `______` (do not write it here or anywhere in this repository)

### 0.3 Baseline — record before touching anything [RO]

- [ ] `GET /api/status` → `control_enabled` `______`, `inhibit_reason` `______`,
      `commissioned` `______`, `commissioning_scope` `______`
- [ ] `GET /api/solar-grid/status` saved to file: `______`
- [ ] `GET /api/operator/alarms` — every condition already standing **before** you
      arrived, so nothing pre-existing is later attributed to this SAT:
      `______`
- [ ] `GET /api/operator/alarms/journal` — `stored` `______`,
      `next_sequence` `______` (the SAT's own journal window starts here)
- [ ] Per inverter: present power-limit register value(s), read directly:
      `______`
- [ ] Present plant state: source carrying the plant `______`, PV output `______`
      kW (independent measurement `______` kW), load `______` kW,
      wall-clock `______`
- [ ] Weather/irradiance conditions, because they bound what can be demonstrated
      today: `______`

### 0.4 Authority to stop — fill this in before §5

Every **[MOVE]** and **[PLANT]** step below requires all four of these to be true
at the moment it is executed. A step executed without them is void.

| | |
|---|---|
| Person authorised to halt the work, on site, by name | `______` |
| Their contact at the moment of test | `______` |
| Person who operates the genset (start/stop) | `______` |
| Site emergency procedure read and understood by the tester | `______` |
| Who must be **told before** each power-moving step | `______` |

- [ ] The abort ladder in `docs/SITE_COMMISSIONING_RUNBOOK.md` §11.1 is open in a
      terminal, **rehearsed on this visit**, not merely read: `______`
- [ ] Specifically rehearsed: **A1** (disable control) followed by **A2** (write
      the recorded original limit back). Both tested: `______`
      **A1 alone does not restore the inverters** — they hold the last limit
      written. Never walk away after A1.
- [ ] **A5** — the site's own emergency stop / breaker procedure — outranks
      everything in this document and is not the controller's decision. Confirmed
      understood, and by whom: `______`

### 0.5 Abort criteria — stop immediately, no discussion

Abort any step and fall back to A1 then A2 if **any** of these occurs:

- [ ] Generator load falls below its minimum-loading figure from the commissioning
      report.
- [ ] Any reverse power is observed at the generator.
- [ ] Write confirmation reports `mismatched` or `unverified`.
- [ ] The setpoint hunts or oscillates.
- [ ] Any new alarm the tester cannot immediately explain.
- [ ] Any genset alarm, audible load change, or governor hunting.
- [ ] Any person on site asks for it to stop.

- [ ] Aborts used during this SAT, with time, reason and what was restored:
      `______`

---

# Part A — monitoring, metering and protection

Runs regardless of profile qualification. Nothing in Part A commands an inverter.

## 1. Acquisition and metering integrity [RO]

- [ ] `GET /api/status` — per meter record: online `______`, quality `______`,
      success rate over the quality window `______`, data age `______` ms,
      `last_response_time_ms` `______` ms
- [ ] Compare the controller's reading against the independent measurement, at
      **three** different plant output levels several minutes apart:

  | # | Controller kW | Independent kW | Difference | Wall-clock |
  |---|---|---|---|---|
  | 1 | `______` | `______` | `______` | `______` |
  | 2 | `______` | `______` | `______` | `______` |
  | 3 | `______` | `______` | `______` | `______` |

- [ ] **Pass criterion:** the tester and the product owner agree an acceptable
      difference **before** taking the readings, and record it here:
      agreed tolerance `______`, agreed by `______`.
      No metering accuracy figure exists in this repository, so this document
      supplies none.
- [ ] **Sign convention.** Confirm import reads with the sign the configuration
      expects and export reads with the other. Observed: import `______`,
      export `______`.
      **Pass:** the sign matches `meter_orientation` as commissioned. A reversed
      sign inverts the control loop, and it is the error class
      `docs/SAMPLE_CONFIGURATION.md` §3 records as having bitten this project more
      than once. A reversed sign is a **stop**, not an observation.
- [ ] Record the **measured** mean data age and the worst age seen over a
      continuous 10-minute observation: mean `______` ms, worst `______` ms
      **Compare, do not copy:** `docs/ACQUISITION_TIMING_MEASUREMENTS.md` recorded
      ~11 samples/s and ~93 ms mean **from a PC on the site network**, with a
      bimodal tail near 290–300 ms — and §4 of that document says the controller's
      own figures are unmeasured and expected to be worse. This step is that
      missing measurement. Record what the controller reports; do not reconcile it
      to the PC figures.
- [ ] Confirm the configured `timeout_ms` clears the worst age you just measured:
      configured `______` ms, worst measured `______` ms. Clears it: `______`
      **Why:** a timeout inside the latency tail records healthy responses as
      failures, feeds the quality window, and a degraded meter **blocks control
      input**. That defect is documented at
      `docs/ACQUISITION_TIMING_MEASUREMENTS.md` §3. Note that a unit commissioned
      earlier **keeps its stored timeout** until reconfigured — check the stored
      value, not the default.
- [ ] Confirm `meter_stale_timeout_ms` exceeds the worst measured age:
      `______` vs `______`. Exceeds: `______`

## 2. Source detection against physical reality [RO][PLANT]

Coordinated with the genset operator. No inverter is commanded in this section.

- [ ] Controller's resolved source now: `______`. Physically true source:
      `______`. Agree: `______`
- [ ] Observe a **real** source change (the genset operator's action, or a natural
      transition). Record for each direction, at least **twice each**:

  | # | From → to | Wall-clock of physical change | Wall-clock controller resolved | Δt (s) | Evidence used (explicit Modbus / measured) |
  |---|---|---|---|---|---|
  | 1 | `______` | `______` | `______` | `______` | `______` |
  | 2 | `______` | `______` | `______` | `______` | `______` |
  | 3 | `______` | `______` | `______` | `______` | `______` |
  | 4 | `______` | `______` | `______` | `______` | `______` |

- [ ] **Pass:** the resolved source follows physical reality in both directions,
      every time, and the Δt values are **recorded**. No detection-latency target
      exists in this repository; the criterion is correctness and a recorded
      figure, and whether the recorded figure is acceptable is a product-owner
      judgement — record it: acceptable to `______`, decision `______`
- [ ] The controller never reported a source that was not physically carrying the
      plant, at any moment: `______`
      **A wrong resolved source is a stop.** Every power limit downstream depends
      on it.

## 3. Protection and refusal behaviour [RO]

This is the part of Part A that demonstrates the product's current central claim:
that it refuses to act on insufficient evidence.

- [ ] If Part B is **out of scope**: `GET /api/status` shows
      `control_enabled: false` with a stated `inhibit_reason`, and the reason is
      the message for `commissioning_first_unmet`. Reason verbatim: `______`
- [ ] Show the operator, on the interface, where that reason is displayed and what
      it means. Shown to `______` at `______`
- [ ] Induce a **meter loss** by disconnecting the meter's network path (least
      destructive method; record the method): `______`
- [ ] Control input is blocked and the plant is left in a state the site
      considers safe. What the controller did: `______`. What the plant did:
      `______`
- [ ] Restore. Recovery is clean, no manual intervention needed: `______`,
      time to recover `______` s
- [ ] `GET /api/inverter-profiles` — record every profile's reported permission
      and reason, and confirm **zero** report production authority unless Part B
      is in scope: count reporting production `______`

## 4. Alarm behaviour under a real upset — measured against EEMUA 191 [RO]

`docs/ALARM_MANAGEMENT_RESEARCH.md` §2 records EEMUA 191's quantitative targets,
which is what makes them testable:

| Metric | EEMUA 191 target |
|---|---|
| Average rate, steady state | fewer than 1 alarm per operator per 10 minutes |
| Peak rate after a major upset | **no more than 10 alarms in the first 10 minutes** |
| Priority distribution | roughly 5 % high, 15 % medium, 80 % low |

### 4.1 Steady state

- [ ] Observe for a continuous period with the plant running normally.
      Duration observed: `______` minutes.
- [ ] Count alarm **raises** in that period (use
      `GET /api/operator/alarms/journal`, filtering `raised` transitions between
      the two sequence numbers): count `______`, sequence window `______`–`______`
- [ ] Normalise to alarms per 10 minutes: `______`
- [ ] **Pass:** fewer than 1 per 10 minutes. Result: `______`
- [ ] If it exceeds the target, list the offending conditions and their
      `suppressed_transitions` counts — a chattering signal is the usual cause:
      `______`

### 4.2 A real upset

Use the **most consequential upset the site is willing to permit**, agreed with
the named authority in §0.4. Do not manufacture a fault the site has not agreed
to, and do not induce a fault on the genset to collect data.

- [ ] Upset used, and who authorised it: `______` / `______`
- [ ] Wall-clock of the upset: `______`
- [ ] Journal sequence number immediately before: `______`
- [ ] Count of **distinct alarms raised in the first 10 minutes**: `______`
- [ ] **Pass:** ≤ 10. Result: `______`
- [ ] From `GET /api/operator/alarms` at the peak, record:
      `summary.active` `______`, `summary.primary_active` `______`,
      `summary.consequential_active` `______`,
      `summary.unacknowledged` `______`, `summary.primary_unacknowledged` `______`
- [ ] **Pass:** `primary_active` is materially smaller than `active`, and every
      consequential row names a `caused_by`. This is the root-cause grouping that
      `docs/ALARM_MANAGEMENT_RESEARCH.md` gap A5 exists to provide: one physical
      event should read as one fault plus explained detail.
- [ ] Rows whose `caused_by` is empty but which the tester believes are
      consequential — record them, because that is a rationalisation finding:
      `______`
- [ ] Record the priority distribution of the alarms that fired:
      high `______` %, medium `______` %, low `______` %.
      **Compare with 5/15/80 and record the comparison.** This is an observation
      about the shipped rationalisation, not a pass/fail on the operator.
- [ ] Have the operator triage the list in front of you. Ask them, and record
      their answer verbatim: *"which single alarm would you act on first, and
      why?"* `______`
      **Pass:** the operator identifies the primary cause from the interface
      without the tester's help. If they cannot, the alarm presentation has failed
      its purpose regardless of the counts.

### 4.3 A condition that clears while nobody is watching

- [ ] Let a condition raise and clear **without acknowledging it**. Which
      condition, and how it was raised/cleared: `______`
- [ ] It remains visible with `state: rtn_unacknowledged`: `______`
- [ ] The operator finds it, unprompted, on the interface the next time they look:
      `______`
      **Pass:** yes. This is ISA-18.2's RTN-unacknowledged state and gap A1 —
      the fault pattern that matters most on an unattended PV-DG site.
- [ ] It can then be acknowledged and moves to `normal`: `______`

### 4.4 The journal survived the site

- [ ] `stored` and `next_sequence` at the end of the SAT: `______` / `______`
- [ ] `invalid_skipped` `______`, `write_failures` `______`.
      **Pass:** both 0. Any non-zero value is a finding to report with the counts.
- [ ] If the controller was reset at any point during the SAT, confirm
      `next_sequence` continued rather than restarting: `______`

---

# Part B — closed-loop control

**Do not start Part B unless §0.1 recorded a production-approved profile with an
archived evidence bundle.** Every step is **[MOVE]** unless marked otherwise, and
every one requires §0.4 to be satisfied at the moment of execution.

Before each numbered step below: tell the named person, confirm the abort terminal
is live, and record the pre-step state. A step without those three is void.

## 5. End-to-end behaviour in grid mode [MOVE]

Grid mode is the lower-consequence source and is done first.

- [ ] Confirm the plant is on **grid** and the controller's resolved source agrees:
      `______`
- [ ] Record the commissioned grid policy: policy `______`
      (zero-export / limited-export / minimum-import), limit `______`,
      `meter_orientation` `______`
- [ ] Record `grid_ramp`: enabled `______`, up `______` %/s, down `______` %/s.
      Note that `grid_ramp` disabled means the command reaches the allowed target
      in a **single cycle** — that is the grid-mode requirement, and it removes a
      *rate limit only*: the export target and every safety clamp are applied
      before the rate limiter and still hold.
- [ ] Told, and by whom: `______` / `______`. Abort terminal live: `______`
- [ ] Pre-step state: PV `______` kW, grid meter `______` kW, setpoint register
      `______`, wall-clock `______`
- [ ] Enable control. **Watch, do not touch**, for at least one full settling
      period plus the profile's settle window.

Record the loop's behaviour:

| Observation | Value | Pass criterion |
|---|---|---|
| Grid meter settles at the commissioned target | `______` kW | Within the commissioned `deadband_kw`; record the deadband: `______` |
| Overshoot past the target in the export direction | `______` kW | **None.** Any export beyond a limited-export allowance is a stop |
| Time to settle | `______` s | Recorded (no documented target) |
| Setpoint hunting after settling | `______` | **None.** Observe at least `______` minutes and record how long |
| Write confirmation state | `______` | `confirmed`; never `mismatched` or `unverified` |
| Independent measurement agrees with the controller's PV figure | `______` kW | Agrees within the §1 agreed tolerance |
| New alarms vs the §0.3 baseline | `______` | None unexplained |

- [ ] Step the target and repeat, at least **three** different targets, recording
      the same rows each time. Targets used: `______`
- [ ] Restore the commissioned target and confirm the loop returns to it: `______`
- [ ] **If the setpoint hunts: abort and report.** Do not tune by feel on a live
      plant.

## 6. Generator mode: ramp, and the measured rate [MOVE][PLANT]

**Highest-consequence section after §7 and §8.** A mistake here damages an engine
rather than losing production.

- [ ] Genset operator present and informed: `______`
- [ ] From the commissioning report, record — do **not** re-derive:
      generator rated kW `______`, minimum loading % `______` (= `______` kW),
      reserve kW `______`, reverse-power margin kW `______`,
      genset's own reverse-power trip setting `______` kW at `______` s
- [ ] Source of the minimum-loading figure and who authorised it: `______`
      **If this is blank, §§6–8 cannot be performed.** The commissioning gate will
      already be refusing, which is correct behaviour and not an obstacle.
- [ ] Record the controller's `generator_ramp`: enabled `______`,
      up `______` %/s, down `______` %/s
- [ ] Record the **inverter's own** ramp gradient as read on site, and the
      reconciliation decision from runbook §6, and who made it:
      gradient `______` %/s, decision `______`, decided by `______`
      **Two rate limiters in series: the slower dominates.** A controller ramp
      faster than the inverter's gradient will simply not be achieved and will
      look like a tracking failure.

### 6.1 Measure the ramp, both directions

For each trial, command a step change and log PV against time, using the
independent measurement as well as the controller.

| Trial | Direction | Step from → to | Wall-clock start | Wall-clock settled | **Measured rate (%/s of fleet capacity)** | Slower limiter appeared to be |
|---|---|---|---|---|---|---|
| 1 | up | `______` | `______` | `______` | `______` | controller / inverter: `______` |
| 2 | up | `______` | `______` | `______` | `______` | `______` |
| 3 | down | `______` | `______` | `______` | `______` | `______` |
| 4 | down | `______` | `______` | `______` | `______` | `______` |
| 5 | down | `______` | `______` | `______` | `______` | `______` |

- [ ] **Pass:** the measured rate never **exceeds** the configured rate in either
      direction. Exceeded on any trial: `______`
- [ ] Record the measurement resolution floor — your logging interval bounds your
      precision: `______` ms. Do not report a precision you did not have.
- [ ] **The down direction is the generator-protecting direction.** Record the
      measured down rate and state explicitly whether the site accepts it as fast
      enough to protect the engine from a PV loss: measured `______` %/s,
      accepted by `______`, decision `______`
      If the inverter's own gradient is slower than the generator can tolerate,
      that is a **plant-level finding for the product owner**, not something to
      tune away.
- [ ] At least **five** trials total, spanning small and large steps. Trials
      recorded: `______`

## 7. PV is reduced as the generator approaches minimum loading [MOVE][PLANT]

- [ ] Genset carrying the plant, PV curtailed to near zero. Generator load:
      `______` kW = `______` % of rated
- [ ] Raise PV in the **smallest steps the controller allows**, recording at each:

  | Step | Commanded PV % | PV kW (independent) | Generator load kW | Gen load % of rated | Margin to minimum loading | Notes |
  |---|---|---|---|---|---|---|
  | 1 | `______` | `______` | `______` | `______` | `______` | `______` |
  | 2 | `______` | `______` | `______` | `______` | `______` | `______` |
  | 3 | `______` | `______` | `______` | `______` | `______` | `______` |
  | 4 | `______` | `______` | `______` | `______` | `______` | `______` |
  | 5 | `______` | `______` | `______` | `______` | `______` | `______` |

- [ ] **Pass, and this is the central criterion of Part B:** the controller stops
      raising PV **of its own accord** before the generator reaches its minimum
      loading. Did it: `______`. At what generator load: `______` kW.
      Margin it held: `______` kW
- [ ] The controller **reduced** PV when the generator approached the limit from
      the other direction — e.g. site load fell while PV was high. How this was
      produced, and what happened: `______`
- [ ] Record any observation from the genset itself: audible load change, governor
      hunting, exhaust temperature if instrumented, engine alarms: `______`
- [ ] **If the controller allowed the generator below its minimum loading: abort
      immediately (A1 then A2) and report it as a defect.** Do not continue tuning
      around it. Occurred: `______`
- [ ] Record the **lowest generator load the controller permitted** across the
      entire SAT: `______` kW

## 8. Reverse-power protection behaviour [MOVE][PLANT]

- [ ] Controller's configured `generator_reverse_power_margin_kw`: `______`
- [ ] Genset's own reverse-power trip setting and delay: `______` kW / `______` s.
      If the genset has **no** reverse-power protection, record that — it makes the
      controller's margin the only defence and raises the stakes on everything
      here: `______`
- [ ] Is the controller's margin comfortably **more conservative** than the trip
      setting? `______`. If not, stop and resolve with the product owner before
      proceeding; record the decision and who made it: `______`
- [ ] **Do not deliberately drive the plant into reverse power to test the trip.**
      Approach the margin under control, in small steps, and confirm the controller
      holds short of it.
- [ ] Closest generator load approached: `______` kW. Controller held short:
      `______`
- [ ] Any reverse power observed at the generator at any moment: `______`
      **Pass:** none. Any observed reverse power is a stop.
- [ ] The genset's protection settings are confirmed **untouched** by this SAT:
      `______`
      Nobody is authorised to relax a protection setting to make a test convenient.

## 9. Loss of communication — pull the cable [MOVE][PLANT]

Two distinct losses, tested separately, because they fail differently.

### 9.1 The controller loses the inverter

- [ ] With control running at low power, **unplug the inverter's network
      connection** — the least destructive way to induce a comms loss. Do not
      power down the inverter. Method used: `______`, wall-clock `______`
- [ ] What the controller did, and how long it took: `______`
- [ ] Write confirmation goes to `unverified` (the deadline elapsed with no usable
      post-write sample) and a **safe-zero is demanded**: `______`,
      measured time to `unverified` `______` ms
- [ ] Alarms raised, and whether they name the right cause: `______`
- [ ] **What the inverter did when its master disappeared while a limit was in
      force** — hold the limit, time out to unlimited, or something else:
      `______`
      **This is safety-relevant and is not documented anywhere in this
      repository.** Whatever you observe, record it verbatim and report it.
- [ ] What the **generator** did while that happened: load `______` kW,
      any reverse power `______`, any genset alarm `______`
- [ ] **Pass:** the plant ended in a state the site considers safe, and the site
      says so by name: safe per `______`, at `______`
- [ ] Reconnect. Recovery is clean and control does **not** resume abruptly at a
      stale setpoint: `______`

### 9.2 The controller loses the meter

- [ ] Unplug the grid/generator meter's network path. Method: `______`
- [ ] Control input is blocked — a degraded or stale meter must inhibit control
      rather than let the loop act on an old sample: `______`
- [ ] PV was driven to a safe level rather than left at a setpoint computed from
      stale data: observed `______`
- [ ] Restore. Recovery clean: `______`

## 10. Loss of the controller itself — "fail-safe" must be demonstrated [MOVE][PLANT]

> **Read this before executing.** "Fail-safe" is an assumption until it is
> observed, and the repository contains direct evidence that the assumption can be
> **wrong in the dangerous direction**. `docs/RELEASE_READINESS.md` §1.6 and
> `docs/BRAND_REGISTER_EVIDENCE_ROUND2.md` record that SolarEdge documents a
> comms-loss command timeout and a **fall-back active power limit**, and that the
> manual **states no default for either**. If that fall-back is 100 %, then losing
> this controller **raises** the plant's limit rather than holding or lowering it —
> the opposite of safe from a generator's point of view. The same document records
> that Sungrow/SMA-family equipment has a configurable fallback behaviour
> (maintain values vs apply fallback values) with its own timeout. **This firmware
> writes none of these registers.**
>
> Therefore: the plant's behaviour on controller loss is a property of the
> **inverters as configured on site**, not of this firmware, and it must be
> **read before it is tested and observed when it is**.

### 10.1 Read the fall-back configuration before inducing anything [RO]

- [ ] For each inverter, does its manual document a comms-loss timeout and a
      fall-back limit? `______`
- [ ] If yes, **read both registers and record the values**:

  | Inverter | Timeout register | Timeout value | Fall-back limit register | Fall-back value |
  |---|---|---|---|---|
  | `______` | `______` | `______` | `______` | `______` |

- [ ] Is the fall-back limit **above** the generator's safe ceiling from §6?
      `______`
      **If yes, do not proceed to §10.2.** Resolve it with the product owner
      first, and record the decision and who made it: `______`
- [ ] If the manual documents no comms-loss fall-back at all, record that too —
      it means the inverter's behaviour is **undocumented**, not that it is safe:
      `______`
- [ ] Also record the **keepalive obligation**, if the manual states one (SolarEdge
      documents that the controller's command interval must be at least the
      command timeout divided by two). Stated obligation: `______`.
      Whether this firmware honours it: `______`
      **`docs/BRAND_REGISTER_EVIDENCE_ROUND2.md` records that this firmware does
      not model that keepalive duty.** If the loop goes quiet for longer than the
      timeout while everything is healthy, the inverter can revert to fall-back
      on its own and **no register tells the controller it happened.** Record the
      risk and who accepted it: `______`

### 10.2 Induce the loss and observe [MOVE][PLANT]

- [ ] Told, and by whom: `______`. Genset operator present: `______`.
      Abort terminal live: `______`
- [ ] Plant at **low PV output**, on the source least consequential to disturb.
      Source: `______`, PV `______` kW
- [ ] Command a limit that is **clearly below** unlimited, so a rise to fall-back
      would be unmistakable. Limit commanded: `______`
- [ ] Remove the controller: power it down (or disconnect it from the inverter
      network). Method used, and wall-clock: `______` / `______`
- [ ] Observe and record, at intervals, with the **independent** measurement:

  | Time after loss | PV kW (independent) | Setpoint register (read directly) | Generator load kW | Reverse power? |
  |---|---|---|---|---|
  | +10 s | `______` | `______` | `______` | `______` |
  | +30 s | `______` | `______` | `______` | `______` |
  | +2 min | `______` | `______` | `______` | `______` |
  | +5 min | `______` | `______` | `______` | `______` |
  | +`______` | `______` | `______` | `______` | `______` |

- [ ] Observe for at least as long as the **longest** documented comms timeout
      found in §10.1, plus margin. Duration observed: `______`
- [ ] **Did PV rise?** `______`
      **A rise is the failure this step exists to catch.** If PV rose, abort by
      restoring the controller or by writing the recorded original limit directly
      (A2), record it as a **plant-level safety finding**, and do not describe the
      installation as fail-safe.
- [ ] Did the generator stay above its minimum loading throughout: `______`
- [ ] Any reverse power at any moment: `______`
- [ ] **Pass, and it must be demonstrated rather than assumed:** the plant ended
      in a state that protects the generator, and a named person on site confirms
      it: confirmed safe by `______` at `______`
- [ ] Restore the controller. It comes back, reads the plant, and does **not**
      resume at a stale setpoint: `______`. Time to a confirmed setpoint after
      restart: `______`
- [ ] Repeat once to show the behaviour is repeatable and not a one-off: `______`

## 11. Source transition, grid ↔ generator, under control [MOVE][PLANT]

Coordinated with the genset operator. Do not start or stop a generator on your own
initiative.

For each direction, at least **twice**:

### 11.1 Grid → generator

| # | Resolved source before | Physical change at | Resolved source changed at | Δt (s) | PV during transition (kW, min/max) | Overshoot on either meter | Any reverse power |
|---|---|---|---|---|---|---|---|
| 1 | `______` | `______` | `______` | `______` | `______` | `______` | `______` |
| 2 | `______` | `______` | `______` | `______` | `______` | `______` | `______` |

### 11.2 Generator → grid

| # | Resolved source before | Physical change at | Resolved source changed at | Δt (s) | PV during transition (kW, min/max) | Overshoot on either meter | Any reverse power |
|---|---|---|---|---|---|---|---|
| 1 | `______` | `______` | `______` | `______` | `______` | `______` | `______` |
| 2 | `______` | `______` | `______` | `______` | `______` | `______` | `______` |

- [ ] **Pass criteria, all of them:**
      - PV was **never left commanded against the outgoing source**: `______`
      - PV stayed within the limits appropriate to the **new** source throughout:
        `______`
      - the generator never went below minimum loading during a transition:
        `______`
      - no reverse power at any moment: `______`
      - no alarm flood: alarms raised per transition `______`
- [ ] Was the **down ramp** the limiting factor in any transition? `______`
      If so, record whether the site accepts the resulting excursion: `______`
- [ ] **Loss of the carrying source** — only if the site's own procedures permit a
      controlled test and the genset operator agrees. Permitted: `______`
      What happened: `______`. Did PV reduce fast enough to protect the remaining
      source: `______`
- [ ] If any transition failed a criterion above: abort, restore, and report. Do
      not retry until the cause is understood.

---

# 12. Operator hand-over

The SAT is not complete until the people who will live with the plant can run it.
Hand-over is a demonstration **by the operator**, not a briefing **to** them.

- [ ] Operators present, by name and role: `______`
- [ ] Confirm what the plant does and does not do, in the operator's own words
      written here. Ask them to state it back; record verbatim:
      - what the controller does today: `______`
      - what it does **not** do: `______`
      - if Part B was out of scope, that automatic control is **inhibited** and
        why: `______`

### 12.1 The operator demonstrates, unaided

Tick only what the **operator** did, without the tester touching the keyboard.

- [ ] Opened the interface and found the live plant state: `______`
- [ ] Found the current source and the current PV output: `______`
- [ ] Found the alarm list and identified the **primary** alarm among several:
      `______`
- [ ] Acknowledged an alarm, and stated correctly that acknowledging does **not**
      clear it: `______`
- [ ] Found a `rtn_unacknowledged` condition and explained what it means: `______`
- [ ] Shelved an alarm, stated that it is time-limited and will come back, and
      distinguished shelving from disabling: `______`
- [ ] Found the alarm journal and read an entry from before the last restart:
      `______`
- [ ] Found `control_enabled` and the `inhibit_reason`, and read the reason aloud
      correctly: `______`
- [ ] Performed **A1** (disable automatic control) and stated that A1 alone does
      **not** restore the inverters: `______`
- [ ] Stated who to call, and in what order, for: a controller fault `______`,
      an inverter fault `______`, a genset fault `______`

### 12.2 Documents left on site

- [ ] The commissioning report (`docs/SITE_COMMISSIONING_RUNBOOK.md`, filled):
      `______`
- [ ] This SAT record sheet, signed: `______`
- [ ] The abort ladder, printed, physically at the controller: `______`
- [ ] The escalation contact list: `______`
- [ ] A plain statement of the plant's accepted scope — monitoring-only, or
      closed-loop with the named production-approved profile: `______`
- [ ] Every alarm left behind, and why. Note that a condition which cleared but
      was never acknowledged stays visible as `rtn_unacknowledged` — correct
      behaviour, not a bug, and the next person will see it. List: `______`

### 12.3 State the plant is left in

- [ ] `control_enabled` `______`, `commissioned` `______`, `scope` `______`
- [ ] Per inverter, the power-limit register value left in place, **verified by
      reading it back**: `______`
- [ ] No lab target declared anywhere: `______`
- [ ] Genset protection settings untouched and confirmed untouched: `______`
- [ ] Configuration backed up off the controller after the SAT: `______`

---

# 13. Verdict and sign-off

## 13.1 Results

| § | Section | Scope | PASS / FAIL / NOT RUN | Notes |
|---|---|---|---|---|
| 1 | Acquisition and metering integrity | A | `______` | `______` |
| 2 | Source detection vs reality | A | `______` | `______` |
| 3 | Protection and refusal | A | `______` | `______` |
| 4 | Alarms under a real upset (EEMUA) | A | `______` | `______` |
| 5 | Grid mode end to end | B | `______` | `______` |
| 6 | Generator ramp, measured | B | `______` | `______` |
| 7 | Minimum loading | B | `______` | `______` |
| 8 | Reverse power | B | `______` | `______` |
| 9 | Loss of communication | B | `______` | `______` |
| 10 | Loss of the controller | B | `______` | `______` |
| 11 | Source transition | B | `______` | `______` |
| 12 | Operator hand-over | A | `______` | `______` |

## 13.2 The SAT fails if any of these is true

- [ ] The controller reported a source that was not physically carrying the plant.
- [ ] Meter sign convention was reversed.
- [ ] The generator went below its minimum loading at any moment.
- [ ] Any reverse power was observed at the generator.
- [ ] PV **rose** when the controller was lost (§10.2), and the site has not
      accepted that behaviour in writing.
- [ ] PV was left commanded against an outgoing source during a transition.
- [ ] Write confirmation reported `mismatched` or `unverified` outside the
      deliberate tests of §9.
- [ ] More than 10 alarms in the first 10 minutes of the §4.2 upset, or the
      operator could not identify the primary cause.
- [ ] An alarm that cleared unacknowledged disappeared from the list.
- [ ] A lab target was declared on real equipment at any point.
- [ ] The operator could not complete §12.1 unaided.
- [ ] A genset protection setting was changed.

## 13.3 Everything measured on site that had no documented value

Copy each of these into the report and into `docs/RELEASE_CHECKLIST.md` §5 —
they are the values this project did not have, and the site is where they came
from.

| Value | Measured / recorded | Section |
|---|---|---|
| Controller's own acquisition latency (mean, worst) | `______` | §1 |
| Source-detection latency, both directions | `______` | §2 |
| Alarm rate, steady state and after an upset | `______` | §4 |
| Priority distribution as it actually fired | `______` | §4.2 |
| Measured ramp rate, up and down | `______` | §6.1 |
| Lowest generator load the controller permitted | `______` | §7 |
| Closest approach to the reverse-power margin | `______` | §8 |
| Inverter behaviour on loss of its Modbus master | `______` | §9.1 |
| Inverter comms-loss timeout and fall-back limit | `______` | §10.1 |
| Plant behaviour on loss of the controller | `______` | §10.2 |
| Source-transition Δt and PV excursion | `______` | §11 |

## 13.4 Signatures

| Role | Name | Date | Signature |
|---|---|---|---|
| Commissioning / test engineer (executed this SAT) | `______` | `______` | `______` |
| Site representative authorised to halt the work (§0.4) | `______` | `______` | `______` |
| Genset operator (for §§6–11) | `______` | `______` | `______` |
| Plant operations, accepting hand-over (§12) | `______` | `______` | `______` |
| Product owner (accepting the residual risk and every NOT RUN) | `______` | `______` | `______` |

- [ ] Evidence bundle — raw API responses, timestamped logs, independent
      measurements, photographs — archived where it can be found in two years:
      `______`

**A Part-A-only SAT is a complete and honest acceptance of a monitoring,
commissioning and protection installation. Do not sign a Part B section that was
not executed, and do not describe the plant as closed-loop controlled on the
strength of Part A.**
