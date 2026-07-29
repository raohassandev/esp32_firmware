# Alarm management: standards research and gap analysis

**Date:** 2026-07-29
**Purpose:** measure the controller's alarm module against the standards the industry
actually holds SCADA and DCS alarm systems to, and record what is missing.

Alarm handling is one of the most codified areas in industrial control, because badly
managed alarms have killed people. The two reference works are **ANSI/ISA-18.2** (adopted
internationally as IEC 62682) and **EEMUA 191**. Both exist largely because of post-incident
investigations — Texaco Milford Haven in 1994 is the canonical example, where operators
received far more alarms than any human could act on during the upset.

---

## 1. ISA-18.2 alarm state model

The standard defines a **ten-state** lifecycle, not the three states this controller
currently implements. The states that matter here:

| State | Meaning |
|---|---|
| Normal | Condition not present, nothing outstanding |
| Unacknowledged | Condition present, nobody has taken responsibility |
| Acknowledged | Condition present, an operator has accepted it |
| **RTN Unacknowledged** | **Condition cleared itself, but was never acknowledged** |
| Latched | Condition cleared but the alarm is held until manually reset |
| Shelved | Temporarily suppressed **by an operator**, time-bounded and audited |
| Suppressed by design | Silenced automatically by plant state or logic |
| Out of service | Disabled for maintenance, under management control |

The transition that this controller gets wrong is **RTN Unacknowledged**. A condition that
appears and clears again while nobody is watching must stay visible until someone
acknowledges it. Otherwise a fault that comes and goes overnight leaves no trace an
operator will see in the morning — which is precisely the fault pattern that matters most
on an unattended PV-DG site.

The three suppression states are deliberately distinct and must not be collapsed into one
"disabled" flag:

- **Shelved** is the operator's own decision, is time-limited, and expires automatically.
- **Suppressed by design** is the system's decision, driven by plant state.
- **Out of service** is a maintenance action requiring authorisation.

Conflating them removes the audit trail that makes suppression safe.

## 2. EEMUA 191 quantitative targets

EEMUA 191 is unusual in giving hard numbers, which makes it directly testable:

| Metric | Target |
|---|---|
| Average alarm rate, steady state | **fewer than 1 alarm per operator per 10 minutes** (~150/day) |
| Peak rate after a major upset | **no more than 10 alarms in the first 10 minutes** |
| Priority distribution | roughly **5% high, 15% medium, 80% low** |

The peak figure is the one that catches systems out. During a real trip a naive alarm
system emits hundreds of alarms in seconds — an "alarm flood" — and the operator, who can
realistically absorb about one alarm per minute, is rendered useless at exactly the moment
they are needed.

**Implication for this controller:** losing the grid will simultaneously raise meter
offline, meter stale, grid measurement unavailable and control inhibited. That is four
alarms for one physical event. Under EEMUA that is already a flood in miniature, and it
argues for one root-cause alarm with the rest as related detail.

## 3. Nuisance alarms: chattering, fleeting, stale

| Term | Definition | Remedy |
|---|---|---|
| Chattering | Repeatedly cycles on and off in quick succession | Deadband, on-delay |
| Fleeting | Brief, but does not immediately repeat | On-delay |
| Stale | Active for a long period (**conventionally >24 h**) with no operator action | Rationalisation; often means the alarm should not exist |

Deadband is the change back past the setpoint required to clear an alarm. On-delay defers
raising until the condition has persisted; off-delay ("debounce") defers clearing. Both
standards warn that on-delay directly consumes the operator's response time, so it is
bounded by process dynamics rather than chosen for convenience.

**This controller already applies the right idea in the wrong place.** Source detection has
a configurable debounce, but the alarm conditions have none: a meter flapping at the edge
of its timeout would chatter the alarm list freely.

## 4. What established SCADA products provide

Ignition is a useful reference because its alarm model is documented publicly. Its feature
set is essentially the ISA-18.2 model made concrete:

- Priority levels (Critical, High, Medium, Low, Diagnostic)
- Deadband, time-on and time-off delays per alarm
- **Shelving** — time-limited and fully audited, explicitly distinguished from disabling
- **Acknowledge notes** — an operator can be *required* to type a justification
- **Alarm journal** — complete history of activation, acknowledgement and clearing, retained
  for performance analysis and regulatory evidence
- Associated data carried with the alarm for context
- Filtering and sorting on the status table

The distinction Ignition draws between *shelving* and *disabling* is the important one, and
it is the same distinction ISA-18.2 makes.

---

## 5. Gap analysis for this controller

What exists today, after the condition-table work:

- Condition state with presence, severity, identifier, first and last raise, occurrence
  count, duration, acknowledgement
- Acknowledgement requires an authenticated session
- Acknowledgement does not clear a condition
- A recurrence clears a previous acknowledgement
- Conditions present at boot are raised, not silently missed

Measured against the standards, the following are missing. Ordered by how much harm the
absence can do:

| # | Gap | Why it matters |
|---|---|---|
| A1 | **RTN Unacknowledged not retained** | A fault that clears itself overnight leaves no trace the operator will see. This is the most consequential gap and the cheapest to fix. |
| A2 | **No alarm journal** | The 96-entry ring is volatile and small; it is a buffer, not a history. No post-incident evidence survives a reboot. |
| A3 | **No shelving** | Operators will otherwise silence nuisance alarms by disabling them permanently, which is how real alarm systems decay. |
| A4 | **No on-delay / deadband on alarms** | A meter at the edge of its timeout can chatter the list without limit. |
| A5 | **Four alarms for one root cause** | Grid loss raises four conditions; EEMUA's peak-rate target argues for one alarm plus related detail. |
| A6 | **No priority rationalisation** | Severities were assigned ad hoc, never checked against the 5/15/80 distribution. |
| A7 | **No stale-alarm detection** | An alarm standing >24 h with no action is a design defect the system should surface. |
| A8 | **No operator identity** | "Acknowledged by whom" cannot be answered; the record can only say an authenticated session did it. |
| A9 | **No suppressed-by-design / out-of-service** | Less urgent on a single-purpose controller, but needed before multi-site fleets. |
| A10 | **No alarm-rate metrics** | Without measuring against EEMUA targets there is no evidence the alarm system is usable. |

## 6. Recommended order of work

1. **A1 — RTN Unacknowledged.** Small, and closes the worst hole.
2. **A4 — on-delay and deadband** on alarm conditions, reusing the debounce pattern already
   proven in source detection.
3. **A5 — root-cause grouping** so one physical event produces one alarm.
4. **A2 — persistent journal**, once storage strategy is decided; the `storage` partition
   exists and is unused.
5. **A3 — shelving**, time-bounded and audited, explicitly not "disable".
6. **A6/A10 — rationalise priorities and measure rate** against EEMUA targets.
7. **A8 — operator identity**, which needs a user model and is properly a P1 product item.

A1 is fixed in the commit that accompanies this document. The rest are recorded as
outstanding rather than silently deferred.

---

## Sources

- [ISA — Understanding and Applying the ANSI/ISA-18.2 Alarm Management Standard](https://www.isa.org/getmedia/55b4210e-6cb2-4de4-89f8-2b5b6b46d954/PAS-Understanding-ISA-18-2.pdf)
- [ANSI/ISA-18.2-2016 Management of Alarm Systems for the Process Industries](https://18817087.s21i.faiusr.com/61/ABUIABA9GAAgyZfj5AUozIu7wwI.pdf)
- [Siemens — Setting a new standard in alarm management (ISA 18.2)](https://support.industry.siemens.com/cs/attachments/109772836/WP_Alarm_Management_ISA_18.pdf)
- [EEMUA Publication 191 — Alarm Systems: A Guide to Design, Management and Procurement](https://www.eemua.org/getattachment/9d3f8071-55c3-49bf-a74a-3bf6ad4a2e0f/Contents-EEMUA-Publication-191-Edition4-November-2024.pdf)
- [ABB — Alarm Management for SCADA control rooms](https://library.e.abb.com/public/72f20c70c7b44d889d463db81df5c38d/SCADA%20Alarm%20Management%20White%20Paper.pdf)
- [exida — Alarms: prevention is better than cure](https://www.exida.com/images/uploads/769alarmmngt.pdf)
- [Hexagon — Fixing Chattering and Fleeting Alarms](https://aliresources.hexagon.com/operations-maintenance/shut-up-fixing-chattering-and-fleeting-alarms)
- [ISA blog — How to Solve Common Problems with Industrial Alarm Systems](https://blog.isa.org/solve-common-problems-industrial-automation-alarm-systems)
- [Inductive Automation — Ignition Alarming platform documentation](https://www.docs.inductiveautomation.com/docs/8.1/platform/alarming)
- [Inductive Automation — Alarm Status Table component](https://www.docs.inductiveautomation.com/docs/7.9/appendix/components/alarming/alarm-status-table)
- [Seqent — Alarm Rationalization Explained: ISA-18.2, EEMUA 191](https://seqent.com/blog/alarm-rationalization-explained/)
