# Operator manual — Automatrix PV-DG Controller

**Who this is for:** the person who watches this plant every day. No firmware or
Modbus knowledge is assumed.

**What this document is:** a description of what the controller shows you, what
each state means, and what you should do about it. It is written from the
controller's own source code and from the project's own honesty documents. It is
**not** a test record and it proves nothing about your plant. Anything specific to
your site — ratings, limits, timings, contacts — is **recorded at commissioning**
and belongs in your site's commissioning report, not here.

**Companion documents**

| Document | What it is for |
|---|---|
| `docs/ALARM_RESPONSE_GUIDE.md` | One entry per alarm, and how the alarm system works |
| `docs/HANDOVER_PACK.md` | The checklist for taking over a commissioned plant |
| `docs/SITE_COMMISSIONING_RUNBOOK.md` | The engineer's procedure. Not an operator document |
| `docs/RELEASE_READINESS.md` | What has and has not been demonstrated, honestly |

---

## 1. What this controller does, and what it does not do

### What it does

- **It measures.** It reads your grid meter (and any other meters configured on
  site) over the network and reports grid active power in kilowatts, with the age
  and the quality of that reading shown next to it.
- **It works out which source is carrying the plant** — utility grid or diesel
  generator — from the evidence the site was wired to give it.
- **It watches for a small number of named fault conditions** and keeps them in a
  proper alarm list with a lifecycle, an acknowledgement record and a history that
  survives a restart.
- **It refuses to command anything it cannot verify.** This is its most important
  behaviour and most of this manual is about it.
- **It records history** — a rolling power trend, an event log, and a persistent
  alarm journal.

### What it does not do

- **It does not, on this release, control your inverters against real equipment.**
  Automatic power-limit control is *structurally inhibited* — the firmware refuses
  it — because no manufacturer inverter profile has been qualified against
  physical hardware. `docs/RELEASE_READINESS.md` records this as the decisive
  constraint and states plainly: "Write-qualified or production-approved profiles:
  **0**."
- **It is not a protection relay.** Your generator's own protection (reverse
  power, under-load, over-current) is separate equipment and is not part of this
  controller. Nothing in this controller may be relied on instead of it.
- **It does not know what state your inverters are in.** Every inverter reports
  its operating state as *unknown*, because the code table needed to decode it was
  not available to the project. It is not a fault display for the inverters.
- **It has no clock.** Every time on every screen is "how long ago", measured from
  the last controller restart — not a calendar date and time. A restart resets the
  time base. Journal record numbers do keep counting, so the order of events
  survives a restart even though the wall-clock time does not.
- **It records no operator identity.** When something is acknowledged, the
  controller can say that *an authenticated engineering session* did it. It cannot
  say **who**. If you need to know who, your site must record that itself.

---

## 2. Reading the operator view

### The status strip

Across the top of every page:

| Field | What it tells you |
|---|---|
| **Controller** | Whether this browser is talking to the controller at all |
| **Network** | Whether the controller has its network connection |
| **Meter** | Whether grid measurements are arriving and are fresh |
| **Control** | The control-authority state — see section 3. This is the field to read first |
| **Alarms** | Outstanding alarm work |
| **Updated** | How long ago this page last heard from the controller |

If **Updated** stops advancing, do not trust anything else on the screen. You are
looking at a frozen picture. Refresh; if it stays frozen, treat it as a
loss-of-communication event and escalate (section 8).

### Plant overview

- **Grid power** — the measured grid active power, with a provenance line beneath
  it (section 4). Negative means the site is exporting.
- **Meter health** — communication quality for the grid meter.
- **Requested PV** — what the controller *would ask* the solar fleet for. It is
  labelled on screen as "Control request, not measured PV production", and that
  wording is exact. It is not a measurement.
- **Applied PV** — the controller's own applied output figure. Also labelled "not
  inverter telemetry". It is what the controller decided, not what the inverters
  did.
- **Power flow** — the site energy balance, with a line stating which quantities
  this site actually measures. Read that line. A flow arrow for a quantity the
  site does not measure is inferred, not observed.
- **Meter age** — time since the last valid grid sample.

### Two banners you must never scroll past

- **Lab simulator mode** (section 5). If this banner is showing, nothing on the
  screen is evidence about your real equipment.
- **Engineering session active**, with a countdown. This means someone is signed
  in with write authority from this browser. If you did not expect to see it, find
  out who is working on the controller.

---

## 3. Control authority: three states, and what to do about each

The controller publishes exactly one authoritative statement of what it is
allowed to do. It appears as **Control** in the status strip and as **Control
authority** on the PV-DG control page. There are three values.

### `Monitoring only`

**Meaning:** automatic control is switched off. The controller is watching and
recording. It issues no inverter commands.

**On this release this is the normal, expected and correct state**, because no
inverter profile is qualified for production. A monitoring-only plant is a
complete and legitimate installation — the site acceptance test explicitly allows
signing off a monitoring-only plant.

**What to do:** nothing. Confirm that this matches what your commissioning report
says the plant was accepted as. If your report says the plant was accepted as
closed-loop controlled and you are seeing `Monitoring only`, that is a change and
needs an engineer.

### `Inhibited`

**Meaning:** automatic control is switched **on**, but the controller is refusing
to command right now. Something it needs is missing or untrustworthy.

Next to it the controller publishes an **inhibit reason** — one sentence in the
firmware's own words. The interface shows that sentence exactly as written and
never paraphrases it.

**What to do:**

1. Read the inhibit reason and write it down verbatim.
2. Look at the alarm list. An inhibit is very often the direct consequence of an
   active alarm — most commonly the grid measurement being unavailable or stale,
   because the controller will not command PV without a trustworthy grid
   measurement.
3. If the alarm is one you can act on (see `docs/ALARM_RESPONSE_GUIDE.md`), act on
   it. Control resumes by itself once the reason goes away.
4. If the reason names a commissioning prerequisite, that is an engineer's job.
   Do not attempt to satisfy it yourself.

**Inhibited is a safe state.** The controller is doing what it was built to do.
It is not an emergency by itself.

### `Commanding`

**Meaning:** the controller currently has authority to write power limits to
inverters and is doing so.

**What to do:**

1. **Check the commissioning scope on the same panel.** If it says
   `lab_simulator_only`, you are watching a lab exercise, not plant control
   (section 5).
2. Watch the setpoint-confirmation panel on the Solar inverters page and
   understand what "confirmed" rests on (section 6). This is the single most
   misreadable thing on the interface.
3. If PV output moves in a direction you did not expect, or the generator load
   moves toward its limits, escalate immediately (section 8). Do not wait to
   diagnose it.

### If control has to stop

The fastest safe action is to have automatic control disabled. That is an
**engineering action** — it needs a signed-in session — and it is item **A1** on
the abort ladder in `docs/SITE_COMMISSIONING_RUNBOOK.md` §11.1.

**Know this before you ever need it:** disabling control does **not** put the
inverters back. They hold the last power limit that was written to them.
Restoring output is a separate step (A2 on that ladder) and needs an engineer with
the original recorded register values. Never assume that stopping the controller
has restored the plant.

Your site's own emergency stop and breaker procedure **outranks everything
above**, and nothing in this manual overrides it.

---

## 4. Provenance: where a number came from, how old it is, and whether to trust it

Every live measurement on the operator view carries three facts alongside the
value. This exists for a specific reason recorded in the source: without it, the
same signal read at two different instants looks like two disagreeing values, and
you have no way to tell which one the controller is acting on.

### Quality

The controller publishes one word for the grid measurement, and the interface
shows it as written rather than re-scoring it:

| Quality | Meaning |
|---|---|
| `good` | The meter is answering, the value decoded, and it is fresh |
| `degraded` | The meter is answering, but too many recent requests failed. The controller is saying something about the instrument that the screen has no business softening. **A degraded meter blocks control input** |
| `stale` | A real value arrived, but too long ago to act on |
| `unavailable` | No usable value at all — the meter is not answering, or nothing decoded |

"Degraded" is judged over a rolling window of the last 20 requests, and needs at
least 5 requests before it will be declared at all; below 80 % success the meter
is marked degraded. Those are firmware values. The **timeout** and **poll
interval** that decide whether a request succeeds are site values and are
**recorded at commissioning** — set too tight, they record healthy reads as
failures and mark a healthy meter degraded.

### Age

How long ago the value was read, in plain language ("Just now", "14 s ago").
Freshness limits are configured per site (`meter_stale_timeout_ms`) and are
**recorded at commissioning**.

Be aware that the word "stale" is decided against **three different windows** in
three different parts of the controller — a fixed five-second window for the
"grid measurement unavailable" condition and for the quality word on the
dashboard, and your site's configured stale timeout for the "meter data stale"
alarm. Near the boundary you may briefly see the dashboard and the alarm list
disagree about whether data is stale. That is a known consequence of how the
thresholds are defined, not a broken screen. If they disagree persistently,
report it.

### Source

Which instrument the number came from — the meter's configured name, its position
in the meter list and its Modbus unit id. If the site has not resolved a single
grid meter, the controller reports the source as **nothing at all** rather than
naming an arbitrary instrument. An unnamed source is a configuration problem for
an engineer.

Note also the **kind** of every value: `measured` means an instrument read it.
"Requested PV" and "Applied PV" are not measured and are labelled as such on
screen. Never quote a requested figure as production.

### Why a stale value is shown as stale rather than hidden

Because hiding it would be worse. If the controller blanked an old reading, you
would see an empty field and could not tell "the meter has stopped answering"
apart from "the page has not loaded yet" apart from "this site does not measure
that". Showing the last real value with an honest age and quality label tells you
three things at once: what the plant was doing when it was last known, how long
ago that was, and that it is no longer current. A blank tells you nothing.

The rule that follows from this: **a value with a bad quality label is history,
not status.** Read it as "this is what it was", never as "this is what it is".

---

## 5. Lab-simulator mode, and why it must never be ignored

The controller can be told that a particular inverter connection is a **Modbus
simulator** — a piece of software standing in for an inverter. An engineer has to
declare that deliberately. That declaration, and nothing else, lets the
controller issue commands through an inverter profile that has never been proven
against real hardware.

It exists for a practical reason: the site's inverters are a long way from the
engineers, and without this the control loop could not be exercised at all before
anyone travelled.

**While it is in force, every screen shows a banner reading "Lab simulator mode —
this is not production control", and the commissioning scope reads
`lab_simulator_only`.**

### Why this matters to you

**Nothing observed in lab-simulator mode is evidence about your physical
equipment.** A simulator does whatever it was written to do. It can accept a
command perfectly, report it back perfectly, and prove absolutely nothing about
what a real inverter on your site would have done with the same command.

One declared simulator anywhere makes the **whole** commissioning verdict
`lab_simulator_only`, even if every other inverter is fully qualified. The
weakest link decides, deliberately.

### What to do

- **If you see the lab banner on a running plant, escalate.** Ask an engineer why
  a simulator is declared on a live site and when it will be revoked.
- **Never report a lab result as a plant result.** If someone asks whether control
  works and the banner is showing, the answer is "it worked against a simulator",
  not "yes".
- **Never accept a hand-over with a lab target declared.** The site acceptance
  test lists "No lab target declared anywhere" as a required condition of the
  state a plant is left in, and lists "a lab target was declared on real
  equipment at any point" as an outright test failure.

The project's own documents are blunt about the one weakness here: a human could
falsely declare real equipment to be a simulator. Nothing in the design can
prevent that. The banner is loud precisely because a person is the only defence.

---

## 6. "Confirmed" commands: two kinds of evidence, and only one of them proves a limit

When the controller writes a power limit to an inverter, it does not treat the
write as successful merely because it was accepted. It looks for evidence
afterwards and reports one of four states, all shown separately on the Solar
inverters page:

| State | Meaning |
|---|---|
| `pending` | Written; not yet enough time to judge. Temporary, and deliberately **not** styled as success |
| `confirmed` | Evidence was found. **Read on — there are two very different kinds** |
| `mismatched` | Evidence disagreed with what was asked. This is a **fault**. The controller drives that inverter to a safe zero and drops it from the commandable fleet |
| `unverified` | Confirmation is impossible or ran out of time. Neither success nor failure — and it is **never** upgraded to confirmed |

### The two kinds of evidence

The controller also publishes **what the verdict rests on**, so that "confirmed"
is never read without knowing what confirmed it.

**1. A setpoint echo (`setpoint_readback`).** The controller wrote a number to the
inverter, read that number back, and it matched.

**This proves only that the device accepted the value. It does not prove the
limit is in force.**

This is not a theoretical worry. It is the single most dangerous failure mode this
project found, and it is documented at length. On several inverter brands the
power-limit register **must** be unlocked by a separate register first — and until
it is, the power-limit register still accepts the write, and still echoes it back
perfectly, while the inverter carries on generating at full output. A controller
that trusted the echo would report the plant limited when it was not.

That is worse than a failed command, because the failure is invisible: every layer
above the echo, including you, is told a limit is in force when it is not. It is
the reason four transcribed brands were refused outright and the reason the
inverters page carries a permanent notice saying "A setpoint can read back
perfectly and still be ignored."

**2. Measured power (`measured_power`).** The controller compared the plant's
actual measured output before and after the command.

This is the stronger evidence, and the controller is careful about when it will
claim it:

- A limit counts as **demonstrated** only when output was **above** the new limit
  before the command and at or below it afterwards.
- If output was **already** below the limit beforehand, the verdict is
  **`unverified`**, not confirmed — because "output is below the limit" is equally
  consistent with the limit working and with the sun going behind a cloud. The
  controller reports that ambiguity rather than claiming a limit it never saw take
  effect.
- A command of 100 % can never be demonstrated at all, because output can never
  have been above it. It is permanently ambiguous, correctly.
- Output **above** a limit past the settle window is **unambiguous**: no change in
  sunshine can lift a plant above a limit that is in force. That is reported as
  `mismatched`.

Even a demonstrated limit says only that output is at or below what was asked. On
a plant commanded through a Huawei SmartLogger it says nothing about the logger's
own internal "adjustment coefficient", which has no register at all and cannot be
seen from the network.

### What you should do with this

- **Look at what confirmed it, not just that it says confirmed.** The interface
  shows both.
- **Treat `mismatched` as a real fault** and escalate it. It has already caused the
  controller to drive that inverter to zero.
- **Do not read `unverified` as a failure.** It usually means the controller was
  honest about not being able to tell. It is only a problem if it persists when
  conditions should have allowed a verdict.
- **Look at the enable-register (prerequisite) row on the same page.** An
  unconfirmed enable register is a *different* fault from a wrong setpoint, and it
  is the more dangerous one, because the setpoint looks perfect while being
  ignored. The two are reported as two separate rows on purpose.

---

## 7. Commissioned is not the same as production-qualified

These two words are used precisely on this product and they mean different
things. Confusing them is how a lab result gets reported as a plant result.

**Commissioned** means the controller has everything it needs, and that
everything is self-consistent: nine enumerated prerequisites are all satisfied.
Those cover meter roles, an inverter profile that passes the write gate, readback
capability, fleet capacity, ramp policy, source detection, grid policy, generator
limits and control tuning. If the controller could not *read* the state a
prerequisite depends on, that prerequisite counts as **unmet** — it is never
assumed satisfied. A controller that knows nothing reports "not commissioned",
which is the correct answer.

**A commissioned verdict says nothing about whether the equipment being commanded
is real.** That is a separate field: the **scope**.

| Scope | Meaning |
|---|---|
| `none` | Not commissioned. Automatic control inhibited |
| `lab_simulator_only` | Commissioned — but at least one commanded inverter is a **declared simulator**. Nothing observed is evidence about physical equipment |
| `production` | Every commanded inverter passed production write qualification |

**Production-qualified** is the stronger claim and it has a price: the exact
manufacturer manual, a model-specific register mapping, simulator evidence, a
bench test, and then a physical readback qualification on real hardware — in that
order.

**On this release, no profile has reached that.** `production` scope is not
reachable, and `docs/RELEASE_READINESS.md` states it directly: "A
`lab_simulator_only` verdict is not a release."

**What this means for you:** a plant can be correctly, honestly commissioned and
handed over to you as a **monitoring, commissioning and protection**
installation. That is a real, complete, useful product. It is simply not a
closed-loop control installation, and it must not be described as one. If anyone
tells you the plant does automatic PV curtailment, check the control-authority
state and the scope yourself.

---

## 8. What you may change, what needs an engineer, and when to escalate

### What an operator may safely do

- **Look at everything.** Every read-only operator screen — plant overview, grid
  power, solar inverters, the alarm list, the alarm journal, the history trend —
  is open without a password, and reading changes nothing.
- **Change the range on the history view** (15 minutes / 1 hour / 24 hours).
- **Refresh the page.**
- **Switch the light/dark theme.**
- **Write things down.** Recording what you saw, with the age and quality of the
  readings, is the single most valuable thing you can do before escalating.

That is the honest list. **This controller gives an operator no plant-changing
control at all.** Everything that alters behaviour is behind an engineering
session.

### What needs an engineer (a signed-in session)

All of the following return "unauthorised" without an authenticated engineering
session:

- **Acknowledging an alarm.** An unattributable acknowledgement is a way to make
  a live fault look attended to, so the controller refuses anonymous ones.
- **Shelving an alarm, unshelving it, or taking it out of service.**
- Enabling or disabling automatic control.
- Any meter, inverter, control, ramp, generator-limit or network setting.
- Declaring or revoking a lab-simulator target.
- Assigning an inverter profile.
- Restarting the controller.
- Exporting or importing configuration.

The interface deliberately does **not** show you a button that could only fail. In
place of an Acknowledge button you will see "Acknowledging requires an engineering
session", and in place of the shelve controls, "Shelving and out-of-service are
engineering actions and need a session."

> **Note on the acceptance test.** `docs/SITE_ACCEPTANCE_TEST.md` §12.1 asks the
> operator to demonstrate acknowledging and shelving an alarm unaided. On this
> firmware both actions require an engineering session, so that demonstration
> cannot be completed by an operator who has not been given engineering
> credentials. Settle this with the product owner before hand-over: either the
> operator is given a session for the demonstration and supervised, or the
> demonstration is recorded as performed by the engineer with the operator
> narrating. Do not quietly tick it.

### Credentials

The engineering password is **obtained from the product owner**. It is
deliberately not written in this repository or in any document here, and it must
not be. If it has been lost, recovery needs physical reflashing and serial-console
access — arrange that with the product owner in advance.

### When to escalate — immediately, without diagnosing first

Escalate at once, using your site's contact list, if any of these is true:

1. **Anything is moving that you did not expect** — PV output, generator load, or
   the source the plant is running on.
2. **The generator is approaching or below its minimum load**, or you see any
   reverse power at the generator. This outranks the controller entirely. Follow
   your site's generator procedure.
3. **A setpoint confirmation reads `mismatched`.**
4. **The lab-simulator banner is showing on a live plant.**
5. **The control-authority state changed and nobody told you.**
6. **The page has stopped updating** and a refresh does not fix it.
7. **You are being asked to change a setting** you do not fully understand. There
   is no operator setting on this controller that has to be changed in a hurry.

### When to escalate at the next opportunity

- A **new** primary alarm you have not seen before.
- An alarm marked **stale** — standing for more than 24 hours with nobody acting
  on it. That usually means the alarm itself is wrong and needs an engineer to
  rationalise it, not that a new fault appeared.
- An alarm whose **`suppressed_transitions` count is rising** — a signal that is
  flapping is itself a fault worth reporting.
- Anything **out of service** that you cannot account for. It does not expire by
  itself, so it can only be ended by a person.
- A **shelf** that keeps being renewed. Repeated shelving of the same alarm means
  the alarm needs fixing, not silencing.

### Who to call

**Recorded at commissioning.** Your escalation list — controller fault, inverter
fault, generator fault, and who on site may halt work — is filled in during
hand-over and is required to be left physically at the controller. If you do not
have it, that is itself a hand-over defect: raise it. See
`docs/HANDOVER_PACK.md`.

---

## 9. Things that are correct behaviour and are often mistaken for faults

| What you see | Why it is correct |
|---|---|
| `Monitoring only`, on a healthy plant | No inverter profile is production-qualified on this release. Automatic control is inhibited by design |
| An inverter's state shown as unknown | No profile configures a status register, because the code table needed to decode it was never available. A guess could report "on grid" while a machine was faulted, so the honest unknown stands |
| An alarm still listed after it cleared itself | ISA-18.2 `rtn_unacknowledged`. It stays visible until someone acknowledges it. See `docs/ALARM_RESPONSE_GUIDE.md` |
| A stale value still displayed, labelled stale | Hiding it would remove the information that something stopped. Section 4 |
| All times shown as "ago" and never as a date | The controller has no real-time clock and will not fabricate one |
| An acknowledgement that names no person | The controller has no operator identity model. It will not invent a name |
| `unverified` after a command | The controller could not honestly tell whether the limit took effect. Section 6 |
| Alarm counters resetting after a restart | The alarm **rate** metrics are not persisted. The alarm **journal** is |
| A setting shown greyed out and read-only | Some settings are deliberately not writable in this milestone |
