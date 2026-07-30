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

---

## 7. Status of the gaps, updated

The table above is the ORIGINAL analysis, kept as written so the reasoning that
justified each item is not lost. It is no longer the current state.

| # | Gap | Status |
|---|---|---|
| A1 | RTN Unacknowledged not retained | **Closed.** Verified on hardware: `rtn_unacknowledged` observed live on two conditions. |
| A2 | No alarm journal | **Closed.** 16384-record ring on the `storage` partition. Verified across a hard reset: records survived, sequence continued rather than resetting. |
| A3 | No shelving | **Closed.** Time-bounded 60 s to 8 h, audited, expiry enforced on read as well as on the tick. |
| A4 | No on-delay / deadband | **Closed.** 1000 ms on-delay, 2000 ms off-delay; suppressed transitions counted. |
| A5 | Four alarms for one root cause | **Closed.** Causality table; `primary_active` is the triage number. |
| A6 | No priority rationalisation | **Closed as a measurement, NOT as a pass.** See §8. |
| A7 | No stale-alarm detection | **Closed.** 24 h threshold, reported in the summary. |
| A8 | **No operator identity** | **STILL OPEN**, and it has a consequence bigger than it looks. See §9. |
| A9 | No suppressed-by-design / out-of-service | **Closed.** Three independent suppression states, never collapsed. Verified on hardware from the journal: `design_suppressed` on all three downstream conditions when the network was lost, `design_released` when the cause cleared. |
| A10 | No alarm-rate metrics | **Closed.** Rolling 10 min / 60 min / 24 h plus worst-ever, in EEMUA's unit. |

## 8. A6: the distribution is measured, and it does not meet the target

Measured on hardware from the same table the alarm list is served from:

| Population | high | medium | low | total |
|---|---|---|---|---|
| Alarms an operator acknowledges | 50 % | 50 % | 0 % | 4 |
| All tracked conditions | 29 % | 29 % | 43 % | 7 |

EEMUA's 5/15/80 is **not met**, and below 20 conditions it is **not representable** —
the smallest non-zero share of four alarms is 25 %. No severity was changed to
improve the figure. The payload reports `meets_target: false` and
`target_representable: false`, and a contract asserts the miss so that demoting an
alarm to hit the number is caught rather than accepted.

That is the honest reading of EEMUA on a small controller: the distribution is a
target for a plant with hundreds of alarms, and a 12-condition controller cannot
express it. Reporting the miss is worth more than arranging the number.

## 9. A8 is still open, and it blocks the operator's own action

Acknowledgement is, in ISA-18.2, the **operator's** act: it records that a human has
taken responsibility. In this firmware every alarm action — acknowledge, shelve,
unshelve, out-of-service — requires an authenticated **engineering** session, and the
interface offers an operator a "sign in to acknowledge" link rather than a button.

**RESOLVED. Acknowledgement is now an operator action.** What follows records the
decision and the reasoning, because the reasoning is the part that will be
questioned later.

The old behaviour had a concrete, observed consequence: the development board carried
four unacknowledged alarms for an entire working session because nothing without
engineering credentials could acknowledge them. A1 was closed in mechanism and inert
in practice.

The three resolutions previously offered here were:

1. **Implement A8** (operator accounts) and make acknowledgement an operator action.
   The correct fix, and the largest.
2. **Allow unauthenticated acknowledgement** and accept that the journal records the
   act without an actor. Cheap; weakens the audit trail A3 exists to protect.
3. **Keep it as it is**, accepting that alarms accumulate unacknowledged between
   engineering visits.

**(2) was chosen, with the objection to it engineered out rather than accepted.** The
stated cost of (2) was that the journal would record an act with no actor. It does
not: the journal's detail word now carries an actor CLASS -- `operator` or
`engineering_session` -- written to flash, rendered by the journal endpoint and the
acknowledgement reply, and verified on hardware to survive a hard reset (sequence 298,
MTR-001, `acknowledged_by: operator`). What is still absent is a PERSON, which is A8
and remains open. So the audit trail is weakened only in the sense that it always was
weak: it names a class, not a name, and says so plainly rather than implying more.

Why not (3), which was the conservative-looking option: it guaranteed the outstanding
list would never be maintained, and an alarm list nobody can discharge stops being
read. That is not a conservative outcome. Between a record that is attributed to a
class and a record nobody ever updates, the first is worth more.

The asymmetry that makes this safe is that **only acknowledgement was opened**.
Shelving, unshelving and out-of-service still require an engineering session, because
those REMOVE a live condition from the operator's view -- a suppression is a decision
someone must be accountable for. Acknowledgement suppresses nothing, silences nothing,
and cannot clear a fault the plant still has; it records that somebody looked. The two
were never comparable risks and no longer share a gate.
`tests/alarm_ack_authority_source_contract.py` asserts both halves together, so a
future change cannot quietly open all four or re-close all four.

`docs/SITE_ACCEPTANCE_TEST.md` §12.1 asks an operator to acknowledge an alarm unaided.
**That is now possible and the test can pass as written.** Its shelving step still
requires an engineering session, by design, and the test says so.

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
