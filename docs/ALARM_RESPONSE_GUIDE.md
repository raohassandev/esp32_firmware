# Alarm response guide — Automatrix PV-DG Controller

**Who this is for:** the plant operator, and anyone triaging an alarm on this
controller.

**Scope.** This controller has **four** alarm conditions. That is not an
abbreviation of a longer list — four is all there are, and every one of them is
described below with the identifier, title and severity the controller actually
publishes. Nothing here is invented: the ids, titles, wording and severities are
the controller's own, taken from `components/web_server/operational_api.c`.

Every site-specific number — thresholds, timeouts, contact names — is **recorded
at commissioning** and is not in this document.

Read `docs/OPERATOR_MANUAL.md` first if you have not. This guide assumes you know
what the control-authority states mean.

---

## Part 1 — The four alarms

### How to read each entry

- **Severity** is what the controller publishes. Note that it publishes **two**
  words for the same underlying level: a `severity` word (`critical` / `warning`)
  and a rationalised `priority` word (`high` / `medium`). They are the same
  judgement in two vocabularies, both are shown in the interface, and they will
  never disagree — `critical` always means `high`. Do not read them as two
  separate ratings.
- **Effect on control** describes what the controller does while the condition
  stands. Where a downstream effect depends on your site's configuration, it says
  so rather than guessing.
- **Likely causes** are ordered most likely first, based on what the condition is
  derived from in the source. They are a starting point for your site, not a
  measured failure distribution.

---

### NET-001 — Controller network offline

| | |
|---|---|
| **Identifier** | `NET-001` |
| **Title when active** | Controller network offline |
| **Title when cleared** | Network restored |
| **Severity** | `critical` / priority `high` |
| **Root-cause role** | **Always primary.** This is the deepest cause the controller can observe — it has nothing upstream to blame |

**What the controller says:** "The controller lost its primary network
connection."

**What it means in plant terms.** The controller has lost the network it reaches
everything over. This is *not* merely "I cannot see the web page from my desk".
The grid meter is reached over this same network, so losing it removes the
measurement automatic control depends on — that is the firmware's own stated
reason for rating it high. The plant stops being controllable on measured grid
power.

**Likely causes, most likely first**

1. Wi-Fi coverage lost at the controller — interference, a moved antenna, a new
   obstruction.
2. The access point or router lost power or restarted.
3. Site network or upstream link failure.
4. Wi-Fi credentials or addressing changed on the site network without the
   controller being updated.
5. The controller itself restarted or faulted. Check whether a "Controller
   started" event appeared at the same moment in the event log.

**Check first, in this order**

1. Is the access point powered and its indicators normal?
2. Does anything else on that network still work?
3. In the event log, did a **Controller started** event appear at the same time?
   If so, this is a controller restart, not a network fault, and the priority
   shifts to finding out why it restarted.
4. Once the controller is reachable again, read the Wi-Fi panel: connected SSID,
   IP address, signal strength, and the disconnect/reconnect counts. Rising
   reconnect counts mean a marginal link rather than a clean outage.

**What an operator may do**

- Restore power to a tripped access point or router, if that is within your normal
  duties at this site.
- Check for anything newly placed near the controller or the antenna.
- Record when the alarm appeared, when the connection came back, and what you did.

**What needs an engineer**

- Any change to Wi-Fi settings, addressing, or the recovery access point.
- A link that keeps dropping. That is a site-network engineering problem.
- Investigating an unexplained controller restart.

**What happens to control while it is active**

Automatic control cannot act on measured grid power, because the measurement
arrives over the lost network. Expect the control authority to become `Inhibited`
(or to remain `Monitoring only`) and expect the downstream conditions below to
follow. If the controller had already commanded a power limit, **the inverters
hold that limit** — losing the controller does not restore them. That behaviour,
and what your specific inverters do when they lose their Modbus master, is
**recorded at commissioning**; it is not documented by the manufacturers and had
to be measured on site.

---

### MTR-002 — Meter offline alarm

| | |
|---|---|
| **Identifier** | `MTR-002` |
| **Title when active** | Meter offline alarm |
| **Title when cleared** | Meter offline alarm cleared |
| **Severity** | `critical` / priority `high` |
| **Root-cause role** | Primary **unless** NET-001 is active, in which case it is a consequence of NET-001 |

**What the controller says:** "The primary grid meter is not communicating."

**What it means in plant terms.** The grid meter is not answering, or is answering
so unreliably that the controller has judged it unusable. Without a grid
measurement, export protection cannot be verified and automatic control must
inhibit — the firmware's own stated reason for rating it high.

Note that "offline" here covers two situations. The meter may be silent, or it may
be answering **too unreliably**: the controller judges quality over the last 20
requests and marks the meter degraded below 80 % success (with at least 5 requests
needed before it will judge at all). A degraded meter is treated as not online and
blocks control input.

**Likely causes, most likely first**

1. Loss of the network path to the meter — most often the same event as NET-001,
   which is why the grouping exists.
2. The meter or its gateway lost power.
3. Communication wiring, a serial-to-Ethernet gateway, or a converter failed.
4. Marginal link quality pushing success below the threshold — a gateway that
   stalls on some transactions, or a request timeout configured too tight for the
   real worst-case latency. The timeout that must clear your site's measured worst
   case is **recorded at commissioning**.
5. Meter addressing changed — unit id, IP address or register address altered at
   the meter without the controller being updated.
6. The meter itself failed.

**Check first, in this order**

1. Is NET-001 also active? If it is, deal with the network first. This alarm is
   very probably a consequence and will clear on its own.
2. Is the meter powered, and is its display or indicator normal?
3. Is the gateway or converter powered, and are its link lights normal?
4. On the Grid power page, read the **response errors** count and the data age. A
   climbing error count with occasional good reads is a *quality* problem; a
   frozen age with no reads at all is a *connectivity* problem. They have
   different remedies.

**What an operator may do**

- Restore power to a tripped meter or gateway, if that is within your duties.
- Visually inspect for disconnected or damaged communication cabling.
- Record the response-error count and the data age before and after anything you
  do.

**What needs an engineer**

- Any change to meter host, port, unit id, register address, scaling, poll
  interval or timeout.
- Diagnosing a degraded-but-answering meter. Retuning the timeout is a measured
  activity, not a guess.
- Replacing the meter, and taking the alarm out of service while it is away.

**What happens to control while it is active**

Automatic control must inhibit — the controller will not command PV without a
trustworthy grid measurement. Monitoring, alarm handling and the journal all
continue. Any previously commanded inverter limit is still held by the inverters.

---

### MTR-003 — Meter data stale

| | |
|---|---|
| **Identifier** | `MTR-003` |
| **Title when active** | Meter data stale |
| **Title when cleared** | Meter data freshness restored |
| **Severity** | `warning` / priority `medium` |
| **Root-cause role** | Primary **unless** NET-001 or MTR-002 is active, in which case it is a consequence of whichever of those is live |

**What the controller says:** "The latest grid measurement exceeded the allowed
freshness window."

**What it means in plant terms.** Measurements are still arriving, but they are
arriving too late to act on. The firmware's own reason for rating this medium
rather than high is exact: "control degrades before it fails", and you have time
to act. This is the early-warning version of MTR-002.

The freshness window is your site's configured `meter_stale_timeout_ms` and is
**recorded at commissioning**. It must be at least the control period and should
exceed the measured worst-case acquisition latency, or fresh data gets
intermittently judged stale and control is inhibited for no real reason.

**Likely causes, most likely first**

1. Network or gateway latency has risen — congestion, a weakening Wi-Fi link, or a
   gateway under load. One site gateway measured during this project sustained
   only about 11 requests per second and stalled for roughly 300 ms on about a
   quarter of transactions.
2. Intermittent request failures stretching the gap between successful reads.
3. A freshness window configured too tight for this site's real latency. If this
   alarm appears repeatedly with no other symptom, suspect the setting before the
   plant.
4. The poll interval was widened, or the meter is being polled more slowly because
   it is degraded — the controller deliberately backs off a failing meter.
5. Onset of the same fault that would eventually raise MTR-002.

**Check first, in this order**

1. Are NET-001 or MTR-002 also active? If so, this is downstream. Work on those.
2. On the Grid power page, read the data age. Is it hovering just past the limit,
   or far past it? Just past suggests tuning or latency; far past suggests a real
   communication failure developing.
3. Read the response-error count. Rising errors point to link quality.
4. Check the `suppressed_transitions` figure on this alarm. A rising number means
   the condition is flapping across its threshold, which is itself a finding —
   normally that the threshold is set too close to normal behaviour.

**What an operator may do**

- Record the data age, the response-error count and how long the alarm stood.
- Report a pattern — for example, "always around midday", or "only when the pump
  house is running". Patterns are the most useful thing you can supply.

**What needs an engineer**

- Any change to the freshness window, poll interval or timeout. All three are
  measured values, and getting them wrong in either direction causes false alarms
  or missed real ones.
- Investigating gateway or network latency.

**What happens to control while it is active**

The control engine treats a stale measurement as not usable, so expect control to
inhibit while it stands. It normally recovers by itself as soon as fresh data
arrives. A stale alarm that flaps repeatedly is the case the on-delay and
off-delay exist to absorb — see Part 2.

---

### MTR-001 — Grid measurement unavailable

| | |
|---|---|
| **Identifier** | `MTR-001` |
| **Title when active** | Grid measurement unavailable |
| **Title when cleared** | Grid measurement restored |
| **Severity** | `warning` / priority `medium` |
| **Root-cause role** | Primary **unless** NET-001, MTR-002 or MTR-003 is active. In practice it is usually a consequence of one of the other three |

**What the controller says:** "Fresh grid power data is not available for
monitoring or control."

**What it means in plant terms.** This is the controller's **derived** view: the
single answer to "can I use a grid measurement right now?" It becomes active when
any of four things is untrue at once — the meter is not online, or no reading has
ever arrived, or the newest reading is older than five seconds, or the value did
not decode to a real number.

It is deliberately rated **medium** even though its practical consequence can be
the same as MTR-002's. The firmware's stated reason: it is the derived view of
whatever NET-001, MTR-002 or MTR-003 already says, and it must not compete for
attention at the same priority as its own cause. The project's own notes record
that the one arguable case for promoting it — when it stands alone with no live
cause — was considered and deliberately left at medium.

**Likely causes, most likely first**

1. One of the other three conditions. Look at those first, always.
2. The reading arrived but did not decode to a usable number — a data type, word
   order or scaling problem in the meter configuration. This is the case where
   MTR-001 can appear with no live cause upstream of it, and it is an engineering
   fault, not a plant fault.
3. No grid meter role is resolved at all — the site has not designated which
   meter is the grid meter, or has designated more than one ambiguously. In that
   case the dashboard also shows the measurement source as nothing rather than
   naming an instrument.
4. The five-second freshness window used by this specific condition being crossed
   while your site's configured stale timeout is longer. Near the boundary this
   condition and MTR-003 can briefly disagree, because they are judged against
   different windows.

**Check first, in this order**

1. Is any of NET-001, MTR-002 or MTR-003 active? The alarm row will tell you — it
   names its cause in a `caused_by` field. If it names one, work on that alarm and
   ignore this one.
2. If it names **nothing** — it is primary and standing alone — this is the case
   that needs an engineer soonest. It usually means the reading is arriving and is
   not usable, which is a configuration fault.
3. On the dashboard, does the grid power reading show a **source**? If not, no
   grid meter role is resolved.
4. Is the grid power value absurd — a huge number, or the wrong sign for what the
   plant is obviously doing? That points straight at data type or word order.

**What an operator may do**

- Establish whether it has a cause named or is standing alone, and report which.
- Record the grid power value and whether it looks physically plausible.
- Nothing else. There is no operator remedy for this condition on its own.

**What needs an engineer**

- Everything, once the upstream alarms are eliminated. Meter role assignment, data
  type, word order, scaling and register addressing are all engineering settings,
  and a wrong sign on grid power is a known serious error class on this project —
  an unsigned type decodes export as a large import, which is the wrong sign at
  exactly the moment the controller must reduce PV.

**What happens to control while it is active**

This is the condition that most directly maps to "no usable grid measurement", so
automatic control cannot act. Expect `Inhibited`, with an inhibit reason to match.

---

### What is deliberately **not** an alarm

Three things the controller records are **events**, not alarms. They appear in the
event log, they are never added to the alarm list, and you never acknowledge them.
That is an outcome of rationalisation, not an omission — an operator does not
acknowledge a controller start, so it is not an alarm.

| Event | Why it is not an alarm |
|---|---|
| **Controller started** | Something that happened, not a condition that persists |
| **Solar fleet available / attention required** | Informational. Investigate on the Solar inverters page |
| **Automatic control enabled / disabled** | A deliberate mode change, not a fault |

They still matter. "Controller started" appearing unexpectedly is how you tell a
restart apart from a network fault. "Automatic control disabled" appearing when
nobody told you is worth a phone call.

---

## Part 2 — How the alarm system itself works

This controller's alarm handling follows **ISA-18.2** (also published as IEC
62682) for the state model and **EEMUA 191** for the quantitative targets. Both
standards exist largely because of post-incident investigations into badly managed
alarms. The relevant behaviours are described below in operator terms.

### The states

| State the controller reports | What it means for you |
|---|---|
| `normal` | Nothing wrong, nothing outstanding. Rows in this state are not shown as work |
| `unacknowledged` | The condition is present **and nobody has taken responsibility for it** |
| `acknowledged` | The condition is **still present**, and somebody has accepted it |
| `rtn_unacknowledged` | The condition **cleared itself**, and nobody ever acknowledged it. Still outstanding work |

### Acknowledging does not clear anything

Acknowledgement and clearing are tracked separately, on purpose.
**Acknowledgement means "I have seen this and taken responsibility".** Only the
plant can clear a condition. An acknowledged condition that is still present stays
in the list, at full prominence, until the plant fixes it.

And if a condition goes away and **comes back**, the previous acknowledgement is
discarded. A fresh occurrence demands fresh attention.

### Why an alarm that cleared itself still needs acknowledging

This is the state most often mistaken for a bug, so it is worth being precise.

Consider a meter that drops off the network at 02:00 and comes back at 02:20.
Nobody was watching. By morning nothing is wrong.

If the controller simply removed that alarm when it cleared, **you would arrive to
an empty alarm list and no trace of the fault at all.** On an unattended PV-DG
site that is precisely the fault pattern that matters most: intermittent, at
night, invisible by morning, and repeating until it becomes a real failure.

So the controller keeps it as **`rtn_unacknowledged`** — "returned to normal,
never acknowledged". It stays visible until a person accepts it. That
acknowledgement is what moves it to `normal`.

**What to do with one:**

1. **Do not just discharge it.** Read the occurrence count, the duration and how
   long ago it last happened. A condition that raised once for four seconds is a
   different finding from one that raised eleven times overnight.
2. **Look for the pattern.** Repeated `rtn_unacknowledged` on the same condition
   is a developing fault, and it is the earliest warning you will get.
3. **Check the journal** (below) for the full sequence, which survives restarts.
4. **Then have it acknowledged** — which needs an engineering session — and record
   what you found.

`rtn_unacknowledged` rows you inherit at hand-over are correct behaviour, not a
defect, and the hand-over documents require them to be listed and explained. See
`docs/HANDOVER_PACK.md`.

### Root-cause grouping: why you triage from `primary_active`, not `active`

Losing the site network raises **four** alarms for **one** physical event: network
offline, meter offline, meter data stale, and grid measurement unavailable.

EEMUA 191 puts an operator's realistic absorption rate at about one alarm per
minute and caps the first ten minutes of a major upset at ten alarms. Four alarms
for one cause spends 40 % of that budget on a single fault. That is an alarm flood
in miniature, and the whole reason floods are dangerous is that they render the
operator useless at exactly the moment they are needed.

The controller's remedy is **attribution, not deletion.** Every condition still
exists, is still listed in full, and is still individually acknowledgeable —
suppressing a real condition is how alarm systems decay. What changes is that a
condition with a live upstream cause is marked **`consequential`** and **names the
alarm that explains it** in a `caused_by` field. Anything with no live upstream
cause is **`primary`**.

The causality is physical, deepest cause first:

```
NET-001  Controller network offline        (always primary)
   └── MTR-002  Meter offline
          └── MTR-003  Meter data stale
                 └── MTR-001  Grid measurement unavailable
```

Any of the upper three explains MTR-001; NET-001 or MTR-002 explains MTR-003;
NET-001 explains MTR-002.

This is evaluated **live, every time the list is read.** So a meter that fails on
its own, with a perfectly healthy network, is correctly reported as **primary** —
the grouping is not a fixed hierarchy that hides real independent faults.

The summary therefore gives you two counts, and they answer different questions:

| Count | Question it answers |
|---|---|
| `active` | How many conditions are present |
| **`primary_active`** | **How many distinct faults are present** |
| `consequential_active` | How much of the list is explained detail |

**Triage from `primary_active`.** It is the number of things actually wrong. If
`active` is 4 and `primary_active` is 1, you have one fault and three
descriptions of it — start at the one. The same applies to the unacknowledged
counts: `primary_unacknowledged` is the work, and consequential rows are
deliberately kept out of it.

### The three suppression states, and why they are not one switch

An alarm can be made to stop pressing for your attention in three different ways.
ISA-18.2 is explicit that they must **never** be collapsed into a single
"disabled" flag, and this controller keeps them as three separate facts, plus one
"effective" state for display.

Why it matters: six months later, *"somebody turned this off"*, *"the controller
stopped raising this because the network it depends on was down"* and *"this
instrument is out for replacement, authorised, reason recorded"* are three
completely different findings — and **only the first is a defect.** One boolean
cannot tell them apart.

| | **Shelved** | **Suppressed by design** | **Out of service** |
|---|---|---|---|
| **Who decided** | An operator, deliberately | The **system**, from plant state | A technician, as a maintenance action |
| **Does it end by itself?** | **Yes — it expires** | **Yes — when the cause clears** | **No. Somebody must end it** |
| **Duration** | Between 1 minute and 8 hours (one shift), and it is **required**, never defaulted | Exactly as long as the fault that explains it | Indefinite |
| **Reason recorded?** | Not required | The cause is named automatically | **Required**, from a fixed list |
| **Can you lift it?** | Yes — unshelve | **No.** It clears when the explaining fault clears | Yes — return to service |
| **Needs an engineering session** | Yes | Not settable by anyone | Yes |

**Shelved.** You have a known nuisance and you do not want to be pressed by it for
a while. The expiry is the entire safety argument: an indefinite shelf is a
disabled alarm wearing a different name — it outlives the shift that created it,
nobody remembers it, and the alarm system quietly decays until an incident finds
the gap. Eight hours is one shift: long enough to work through a nuisance, short
enough that nobody inherits a shelf they never agreed to. One minute is the floor,
because below that a shelf is a mis-click, not a decision. The expiry is checked
both on the controller's own timer **and** every time the alarm list is read, so a
shelf whose time has run out can never be shown as still in force. The
auto-unshelve is recorded in the journal, because "the suppression ended and
nobody was told" is exactly the hole audited shelving exists to close.

**Suppressed by design.** This is the controller's own decision, and no endpoint
can set it and no operator can lift it. It uses the same causality table as the
root-cause grouping: if a condition has a live upstream cause, the controller
suppresses it, names the cause, and journals both edges. It **releases the instant
the cause clears** — a suppression can never outlive the plant state that
justified it. If you see this state, the correct action is to work on the named
cause.

**Out of service.** The strongest and hardest to reach, because it is the only one
with **no expiry**. It is correct for an instrument that has been physically
removed. Because it is also exactly the shape of the "disabled alarm" that rots
alarm systems, three things are mandatory rather than optional: an authenticated
session, a **recorded reason** from a fixed list, and a journal record on both
edges. The published reasons are: field device maintenance, field device
replacement, site commissioning work, awaiting repair, and plant change pending
rationalisation. What replaces the expiry is permanent prominence: every alarm
listing carries the count, the reason and how long it has been in force, so it
cannot be forgotten the way a disabled alarm can.

### What suppression does and does not do

**A suppressed alarm is still fully detected, fully recorded and still in the
list.** It keeps its state, its duration, its cause attribution, its occurrence
count and its acknowledgement. The **only** thing it gives up is its claim on your
attention — it leaves the triage counts.

The summary reports each suppression separately as well as together, because
"three alarms are quiet" is not a reviewable fact, whereas "one was shelved by an
operator, one is a consequence of a live network fault, and one instrument is out
of service for replacement" is.

### Chatter suppression: the on-delay and the off-delay

A signal sitting right at its threshold could otherwise flap the alarm list
without limit. So a condition must persist before it is raised, and persist before
it is cleared.

- **On-delay: 1000 ms.** Four control cycles. Long enough that one late Modbus
  reply cannot raise an alarm; irrelevant next to any human response time.
- **Off-delay: 2000 ms.** Deliberately *longer* than the on-delay. Re-raising a
  condition costs you nothing, whereas a premature "all clear" on a fault that is
  still flapping is how a chattering signal vanishes from view.

Both values are **published** by the controller rather than buried, because an
on-delay is subtracted directly from your own response time and you are entitled
to know what it is.

Two consequences worth knowing:

- The controller observes every five seconds, so in practice these delays mean
  "the condition must still be there at the next look". A state that does not
  survive one further look is *fleeting*, and belongs in the event log rather than
  the alarm list.
- **A condition already true when the controller boots bypasses the on-delay** and
  is raised immediately. It has no transition to debounce and has already
  persisted across a restart, so a controller that boots into a fault reports it at
  once rather than showing an empty alarm list.

The durations you are shown are measured from when the condition **actually
appeared**, not from when the delay expired, so the delay never quietly shortens a
reported outage.

**`suppressed_transitions`** counts transitions that never survived their delay.
Zero is healthy. A rising number is a chattering signal — which is itself a fault
worth reporting, even though no alarm was raised.

### Stale alarms

An alarm standing for more than **24 hours** with nobody acting on it is flagged
`stale`. The conventional reading — and the controller's own — is that this almost
always means **the alarm is wrong rather than the plant is**: either it should
never have been an alarm, or it needs rationalising away. Surfacing it is the
point. The 24-hour threshold is published alongside the flag so the judgement can
be audited.

Report a stale alarm to an engineer. Do not shelve it repeatedly; that is
treating the symptom.

### The alarm journal

Separate from the live alarm list, the controller keeps a **persistent journal**
of alarm transitions in flash. It survives a restart, and record numbers keep
counting across reboots, so ordering is answerable even though calendar time is
not. Every raise, clear, acknowledgement, shelve, unshelve, shelf expiry, design
suppression, design release, out-of-service and return-to-service is recorded,
with the cause on a design suppression and the reason on an out-of-service.

Two honest limits the journal itself publishes:

- **Times are milliseconds since the controller started**, not calendar times. A
  restart resets the time base.
- Losses are **reported, not hidden**: the journal publishes counts of unreadable
  records skipped, write failures, and records dropped before reaching flash. A
  history that quietly drops records is worse than one that admits it did.

Being able to read a journal entry from before the last restart is part of the
operator hand-over demonstration.

### The rate metrics, and EEMUA 191's targets

EEMUA 191 is unusual among alarm standards in giving hard, testable numbers. The
controller measures itself against them and publishes both the measurement and the
verdict.

| EEMUA target | What the controller reports |
|---|---|
| **Fewer than 1 alarm per operator per 10 minutes**, steady state | Counts over the last 10 minutes, 60 minutes and 24 hours, plus the rate normalised into EEMUA's own unit, plus a pass/fail verdict |
| **No more than 10 alarms in the first 10 minutes** of a major upset | The worst 10-minute count ever observed, when it happened, and whether the limit was breached |
| **Roughly 5 % high, 15 % medium, 80 % low** priority | The actual distribution, the target beside it, and a plain statement of whether it is met |

Four honest properties of these numbers:

1. **Every confirmed raise is counted, including ones that were suppressed at the
   time.** This is deliberate. The metric exists to expose an unusable alarm
   system, so it must not be improvable by suppressing more alarms. What it reports
   is an *upper bound* on the load you can be shown — and an upper bound that
   meets the target is proof, whereas a figure that shrinks every time somebody
   shelves something is evidence of nothing.
2. **A steady-state verdict is only reported once the window has actually
   elapsed.** Before that the answer is "not yet measured", never a number
   extrapolated from a few minutes of uptime.
3. **The peak is reported as "no breach observed", never as a pass.** A flood that
   has already happened is measured; one that has not cannot be disproved by
   waiting.
4. **A restart resets these counters.** There is no clock and no persisted rate
   history. The journal persists; the rate metrics do not.

### The priority distribution is not met — and cannot be

This is stated plainly because it looks like a defect and is not.

EEMUA's 5 / 15 / 80 split is **not met** on this controller, and the controller
says so in its own payload rather than rounding toward the answer somebody wants.
It also publishes *why*, in a field called `target_representable`, and the reason
is arithmetic rather than carelessness:

**With four alarm conditions, a 5 % band cannot exist.** One condition out of four
is 25 %. The smallest non-zero share this population can express is far above
5 %. Below about twenty conditions the distribution is *arithmetically
unreachable*, not badly assigned — one condition out of nineteen is already 5.3 %.

The actual distribution is two high and two medium, and no low. That is what four
alarms rationalised honestly gives you.

**The low band is not missing — it lives in the event log.** Controller start,
control-mode changes and solar fleet availability are recorded as events and
deliberately never enter the alarm list, because you do not acknowledge them. That
is itself an outcome of rationalisation. The controller reports the census two
ways for exactly this reason: over the four alarms (the population EEMUA's
distribution is about) and over all seven records (where the low band actually
is). Neither meets the target.

Every one of the four was rationalised individually against a single test: *does
this stop the plant being controlled safely (high), does it degrade control or need
attention soon (medium), or is it informational (low)?* The one candidate for
change — MTR-001, arguably high when it stands alone with no live cause — was
considered and **deliberately left at medium**, because promoting it would move the
distribution *further* from the target, not closer. The project's own note calls
that the clearest possible sign that the distribution is the wrong thing to
optimise on a controller with four alarms.

**So: `meets_target: false` here is not work to be done.** The correct response is
never to demote a condition that genuinely stops the plant being controlled
safely. If someone proposes changing an alarm's priority to improve this number,
that is a decision for the product owner and it should be refused for the reasons
above.

---

## Part 3 — Quick reference

### Triage order

1. Read **`primary_active`**, not `active`. That is how many things are wrong.
2. Deal with the **highest-priority primary** alarm first. If NET-001 is present,
   that is where you start, always.
3. Ignore rows marked **`consequential`** until their named cause is resolved.
   They will clear on their own.
4. Then work the **`rtn_unacknowledged`** rows: read the occurrence count and
   duration, look for a pattern, check the journal, and have them acknowledged.
5. Note anything **`stale`**, anything **out of service** you cannot account for,
   any **shelf** that keeps being renewed, and any rising
   **`suppressed_transitions`**. All four are reports for an engineer rather than
   actions for you.

### Priority and cause at a glance

| Id | Title (active) | Severity / priority | Cause chain |
|---|---|---|---|
| `NET-001` | Controller network offline | `critical` / high | Always primary |
| `MTR-002` | Meter offline alarm | `critical` / high | Consequence of NET-001 |
| `MTR-003` | Meter data stale | `warning` / medium | Consequence of NET-001 or MTR-002 |
| `MTR-001` | Grid measurement unavailable | `warning` / medium | Consequence of NET-001, MTR-002 or MTR-003 |

### What needs an engineering session

Acknowledge · Shelve · Unshelve · Take out of service · Return to service.

All five are engineering actions. The interface will tell you so rather than
showing a button that could only fail. See `docs/OPERATOR_MANUAL.md` §8.

---

## A note on this document's sources

The alarm ids, titles, wording, severities, priority rationales, cause chains,
delay values, shelf bounds, thresholds and out-of-service reasons in this document
are taken from `components/web_server/operational_api.c`,
`components/web_server/include/alarm_suppression.h` and
`components/web_server/include/alarm_metrics.h`. The standards framing is from
`docs/ALARM_MANAGEMENT_RESEARCH.md`.

Be aware that `docs/ALARM_MANAGEMENT_RESEARCH.md` is a **gap analysis dated
2026-07-29** and lists most of the behaviour described above as *missing*. It has
not been updated since the work was done. The source code is the authority on what
the controller does today; that document is the authority on *why* it does it.

**Nothing in this guide has been verified against a running controller by its
author.** It is a description of the code, not a test record.
