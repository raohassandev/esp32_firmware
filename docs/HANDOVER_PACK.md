# Handover pack — Automatrix PV-DG Controller

**What this is.** The index and checklist for handing a commissioned plant over to
the people who will operate it. It says which documents exist, who each is for,
what must be recorded and attached, and what the operator must be able to
*demonstrate* before anyone signs.

**What this is not.** It does not restate the procedures. Every section points at
the document that owns the detail, and that document is the authority. If this
pack and a source document disagree, the source document wins.

**Rule for this pack:** a blank is not a failure, but an *unmarked* blank is.
Anything that could not be measured or supplied must be written down as "not
measured, and why". Every value that is site-specific is **recorded at
commissioning** and lives in the commissioning report — never in this repository.

---

## 1. The documents, and who each one is for

### For the operator

| Document | What it is for |
|---|---|
| `docs/OPERATOR_MANUAL.md` | What the controller does and does not do; how to read the operator view and a measurement's provenance; the three control-authority states; lab-simulator mode; commissioned vs production-qualified; what an operator may change; when to escalate |
| `docs/ALARM_RESPONSE_GUIDE.md` | One entry per alarm condition, and how the alarm system works — ISA-18.2 states, root-cause grouping, the three suppression states, the EEMUA rate metrics |

Both are written for a plant operator. Leave both on site, in a form that works
without site internet.

### For the commissioning or test engineer

| Document | What it is for |
|---|---|
| `docs/SITE_COMMISSIONING_RUNBOOK.md` | The ordered, executable procedure for the site visit. Every step tagged read-only, write or configuration. Contains the abort ladder |
| `docs/SITE_ACCEPTANCE_TEST.md` | The acceptance test at the plant, split into Part A (monitoring, metering, protection, alarms) and Part B (closed-loop control). Contains the operator hand-over section |
| `docs/FACTORY_ACCEPTANCE_TEST.md` | The bench test, before anything travels — boot, provisioning, authentication, endpoint refusal, persistence, migration, alarm lifecycle, journal durability, the commissioning gate |
| `docs/SAMPLE_CONFIGURATION.md` and `config-samples/` | The two reference configurations, and the fields that are easy to get wrong. The site template is deliberately incomplete |

### For the product owner and for the release decision

| Document | What it is for |
|---|---|
| `docs/RELEASE_READINESS.md` | The honest state: what has been demonstrated on hardware, what only in software, what not at all. Which inverter profiles are refused and exactly why. The open product decisions. The inputs still required |
| `docs/RELEASE_CHECKLIST.md` | What must be true before a release is signed, including that no credential is in the repository and that the shipped claim matches the compiled profile catalogue |
| `docs/ALARM_MANAGEMENT_RESEARCH.md` | The ISA-18.2 and EEMUA 191 basis for the alarm design, and the original gap analysis. **Dated 2026-07-29 and not since updated** — it still lists as missing behaviour that has since been implemented. Read it for *why*, not for *what exists* |

### Supporting evidence, referenced rather than summarised

| Document | What it holds |
|---|---|
| `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md` | Line-by-line attribution of the Huawei register map to its manual, plus the open items |
| `docs/SMARTLOGGER_PATH_ANALYSIS.md` | The plant-level command path through a SmartLogger, and what confirming on measured power can and cannot prove |
| `docs/ACQUISITION_TIMING_MEASUREMENTS.md` | Measured acquisition latency, and why the timeout must clear the measured worst case |
| `docs/BRAND_REGISTER_EVIDENCE.md`, `docs/BRAND_REGISTER_EVIDENCE_ROUND2.md` | Per-brand register transcription and its standing |
| `docs/EM500_DMG610_REGISTER_CATALOG.md` | Meter register reference |

**The live API is always a better authority than any list of profiles in a
document.** `GET /api/inverter-profiles` reports the current write permission and
the reason, per profile, generated from the same rule the firmware enforces.

---

## 2. Before hand-over can start

- [ ] The **factory acceptance test** is signed, or its omission is explicitly
      accepted in writing by the product owner: `______`
- [ ] The **site acceptance test** is signed — Part A at minimum. A Part-A-only
      SAT is a complete and honest acceptance of a monitoring, commissioning and
      protection installation, and must not be described as anything more:
      `______`
- [ ] The **commissioning report** (`docs/SITE_COMMISSIONING_RUNBOOK.md`, filled)
      exists, with every blank filled or marked "not measured, and why": `______`
- [ ] The accepted **scope** of the plant is written in one plain sentence —
      monitoring-only, or closed-loop with the named production-approved profile:
      `______`

> On the current release the second option is not reachable: no manufacturer
> inverter profile has passed physical qualification, so automatic control is
> structurally inhibited. See `docs/RELEASE_READINESS.md` §1. If the accepted
> scope says closed-loop, the profile that was qualified must be named, and the
> physical readback evidence must be attached.

---

## 3. What must be recorded and attached

These are the values this project did **not** have and that the site is where they
came from. Each is **recorded at commissioning**; none may be filled from memory
or from another site. `docs/SITE_ACCEPTANCE_TEST.md` §13.3 and
`docs/SITE_COMMISSIONING_RUNBOOK.md` Appendix C are the authoritative lists — this
is the hand-over view of them.

### Plant and equipment

- [ ] Equipment inventory — every inverter, meter, gateway and genset, by model and
      serial: `______`
- [ ] **Generator rated kW, minimum loading %, reserve kW, reverse-power margin
      kW**, each **with its source**: `______`
- [ ] Genset reverse-power trip setting and delay, read from the genset itself:
      `______`
- [ ] Whether minimum loading is per-engine or aggregate on this installation:
      `______`
- [ ] Metering topology, and which meter is the grid meter: `______`
- [ ] Network topology, and specifically **whether a data logger sits in the Modbus
      path**: `______`
- [ ] Whether anything else at the site already writes the inverter power-limit
      registers — plant SCADA, EMS, logger export limitation, grid-code
      curtailment. Two masters on one register will fight: `______`

> The generator figures are called out in `docs/RELEASE_READINESS.md` §6 as the
> single largest gap a site visit closes. A zero rating is treated as "not
> commissioned" and holds PV at zero. **No default may be invented.**

### Measured on site because no documented value existed

- [ ] Controller acquisition latency, mean and worst: `______`
- [ ] Source-detection latency, both directions: `______`
- [ ] Which register the inverter honours for percentage control, with raw
      transcripts: `______`
- [ ] Setpoint settle time — trials, maximum observed, margin chosen, and your
      measurement resolution floor: `______`
- [ ] Whether the readback reports the *requested* or the *active* value: `______`
- [ ] Readback tolerance appropriate to this hardware: `______`
- [ ] The inverter's own ramp gradient, and the reconciliation decision, **with who
      made it**: `______`
- [ ] Measured controller ramp rate, up and down: `______`
- [ ] Alarm rate in steady state and after an upset, and the priority distribution
      as it actually fired: `______`
- [ ] Lowest generator load the controller permitted: `______`
- [ ] Closest approach to the reverse-power margin: `______`
- [ ] Inverter behaviour on loss of its Modbus master: `______`
- [ ] Inverter comms-loss timeout and fall-back power limit, **read and recorded**
      — if the fallback is 100 %, losing the controller *raises* the plant's limit
      rather than lowering it, which is the opposite of fail-safe from a
      generator's point of view: `______`
- [ ] Plant behaviour on loss of the controller: `______`
- [ ] Source-transition time and PV excursion: `______`
- [ ] Control tuning actually used, and what it was tuned against: `______`

### The state the plant is left in

- [ ] `control_enabled`: `______`  `commissioned`: `______`  `scope`: `______`
- [ ] Per inverter, the power-limit register value left in place, **verified by
      reading it back**: `______`
- [ ] **No lab target declared anywhere**: `______`
- [ ] Genset protection settings untouched, and confirmed untouched: `______`
- [ ] Configuration backed up off the controller after the test, somewhere it will
      survive: `______`
- [ ] Every alarm left behind, and why — including any `rtn_unacknowledged`
      condition, which is correct behaviour and which the next person will see:
      `______`
- [ ] Anything **out of service**, with its recorded reason, and who is responsible
      for returning it: `______`
- [ ] Every behaviour observed that contradicted a manual. A contradiction recorded
      is worth more than a clean report that smoothed it over: `______`

### Evidence bundle

- [ ] Raw API responses, timestamped logs, independent measurements and
      photographs, archived where they can be found in two years: `______`

---

## 4. What the operator must be shown and be able to demonstrate

Hand-over is a **demonstration by the operator**, not a briefing to them. Tick
only what the operator did without the engineer touching the keyboard.
`docs/SITE_ACCEPTANCE_TEST.md` §12.1 owns this list; the additions here are noted
as such.

### Stated back, in the operator's own words — record verbatim

- [ ] What the controller **does** today: `______`
- [ ] What it **does not** do: `______`
- [ ] That automatic control is inhibited, and why, if that is the accepted scope:
      `______`
- [ ] The difference between a **commissioned** plant and a
      **production-qualified** one: `______`
- [ ] What **lab-simulator mode** means, and that nothing seen in it is evidence
      about real equipment: `______`

### Demonstrated, unaided

- [ ] Opened the interface and found the live plant state: `______`
- [ ] Found the current source and the current PV output: `______`
- [ ] Read a measurement's **provenance** — quality, age and source — and said
      correctly whether the value could be acted on: `______`
- [ ] Explained why a **stale** value is shown as stale rather than hidden:
      `______`
- [ ] Found the **control-authority** state and the **inhibit reason**, and read
      the reason aloud correctly: `______`
- [ ] Found the alarm list and identified the **primary** alarm among several,
      using `primary_active` rather than `active`: `______`
- [ ] Found a **`rtn_unacknowledged`** condition and explained what it means:
      `______`
- [ ] Stated that acknowledging an alarm does **not** clear it: `______`
- [ ] Distinguished **shelving** (operator, expires) from **suppressed by design**
      (the system's own decision, releases with the plant) from **out of service**
      (maintenance, no expiry) — and from simply disabling an alarm: `______`
- [ ] Found the **alarm journal** and read an entry from before the last restart:
      `______`
- [ ] Explained what a **`confirmed`** setpoint rests on, and that a setpoint echo
      proves only that the device accepted the value — **not** that the limit is in
      force: `______`
- [ ] Stated who to call, and in what order, for a controller fault `______`, an
      inverter fault `______`, a genset fault `______`
- [ ] Performed **A1** (disable automatic control) and stated that A1 alone does
      **not** restore the inverters — they hold the last limit written: `______`

> **Resolve this before hand-over.** Acknowledging, shelving, unshelving and
> out-of-service all require an authenticated **engineering session** on this
> firmware. An operator without engineering credentials cannot perform the
> acknowledge and shelve demonstrations above. Agree with the product owner which
> applies — the operator is given a supervised session for the demonstration, or
> the engineer performs the action while the operator narrates what is happening
> and why — and **record which was done**. Do not tick it silently.
> Choice made: `______`

### Also walked through before signing

- [ ] The **abort ladder** — printed, and physically at the controller — and the
      fact that the site's own emergency stop procedure outranks everything in it:
      `______`
- [ ] Which screens and actions are **read-only for an operator**, and which need
      an engineer. `docs/OPERATOR_MANUAL.md` §8 is the list: `______`
- [ ] The behaviours that are **correct and often mistaken for faults** —
      `Monitoring only` on a healthy plant, inverter state shown as unknown, times
      shown only as "ago", acknowledgements that name no person:
      `docs/OPERATOR_MANUAL.md` §9: `______`

---

## 5. Documents left on site

- [ ] `docs/OPERATOR_MANUAL.md`: `______`
- [ ] `docs/ALARM_RESPONSE_GUIDE.md`: `______`
- [ ] The filled commissioning report: `______`
- [ ] The signed SAT record sheet: `______`
- [ ] The signed FAT record sheet, if one exists: `______`
- [ ] The **abort ladder, printed, physically at the controller**: `______`
- [ ] The **escalation contact list** (section 7): `______`
- [ ] A plain statement of the plant's accepted scope: `______`
- [ ] The list of alarms left behind, and why: `______`

Leave them in a form that works with **no site internet**.

---

## 6. Credentials

**No credential is written down in this repository, and none may be added to it.
That includes this document.** The repository is public.

- The **engineering password** is **obtained from the product owner**. It is not in
  any document here, and a release check exists specifically to block a release
  while a password is present in the source tree.
- If the engineering password has been lost, recovery requires physical reflashing
  and serial-console access. Arrange that with the product owner **in advance**,
  not during a visit.
- **Wi-Fi and recovery-access-point passwords** are entered into the controller and
  are never exported. Configuration exported from the controller has passwords
  masked, and unauthenticated responses have credentials and network names
  stripped.

Hand-over checklist:

- [ ] Who holds the engineering password, and who may issue it, recorded **in the
      site's own credential system** — not here: `______`
- [ ] Confirmed that the operator does **not** need the engineering password for
      anything in their normal duties: `______`
- [ ] If the operator has been given engineering credentials (see section 4),
      recorded who authorised it and why: `______`
- [ ] The exported configuration backup was checked to contain no plaintext
      password before being stored: `______`
- [ ] Recovery procedure understood by whoever will need it: `______`

---

## 7. Who to contact for what

**Recorded at commissioning.** Fill this in, leave a copy physically at the
controller, and do not treat hand-over as complete without it.

| Situation | Who | Contact | Escalate to |
|---|---|---|---|
| Danger to people or equipment | Site emergency procedure — **outranks everything below** | `______` | `______` |
| Generator below minimum load, or any reverse power | Genset operator / owner | `______` | `______` |
| Authority to halt work on the controller | Named site representative | `______` | `______` |
| Controller fault, or a screen that stops updating | `______` | `______` | `______` |
| Inverter fault, or a `mismatched` setpoint confirmation | `______` | `______` | `______` |
| Meter or communication fault | `______` | `______` | `______` |
| Site network, Wi-Fi or access point | `______` | `______` | `______` |
| Lab-simulator banner showing on a live plant | Commissioning engineer, then product owner | `______` | `______` |
| Any request to change a controller setting | Commissioning engineer | `______` | `______` |
| Alarm priorities, rationalisation, or removing an alarm | **Product owner** — this is a product decision, not a site one | `______` | `______` |
| The engineering password | **Product owner** | `______` | — |

---

## 8. Sign-off

Hand-over is complete when every box above is either ticked or explicitly marked
as not applicable **with a reason**.

| Role | Name | Date | Signature |
|---|---|---|---|
| Commissioning / test engineer | `______` | `______` | `______` |
| Plant operations, accepting hand-over | `______` | `______` | `______` |
| Site representative authorised to halt work | `______` | `______` | `______` |
| Genset operator | `______` | `______` | `______` |
| Product owner, accepting the residual risk and every open item | `______` | `______` | `______` |

### Do not sign if any of these is true

- [ ] A **lab target is declared** anywhere on the controller.
- [ ] The accepted scope describes the plant as closed-loop controlled without a
      **named production-approved profile and its physical readback evidence
      attached**.
- [ ] A generator rating, minimum loading, reserve or reverse-power margin is
      blank and not marked "not measured, and why".
- [ ] The operator could not complete the demonstrations in section 4.
- [ ] The escalation contact list is not filled in and physically at the
      controller.
- [ ] Any credential has been written into a document that will be committed to
      this repository.
- [ ] A genset protection setting was changed during commissioning.
- [ ] An inverter's power-limit register was left curtailed without being recorded
      and read back.

### Open items carried forward

Every item that remains open must be listed here, owned by name, rather than left
implicit. `docs/RELEASE_READINESS.md` §4 and §6 are the standing list of what the
project as a whole has not demonstrated; this section is for what **this site**
carries.

| # | Open item | Owner | Agreed by |
|---|---|---|---|
| 1 | `______` | `______` | `______` |
| 2 | `______` | `______` | `______` |
| 3 | `______` | `______` | `______` |

---

**A monitoring, commissioning and protection installation is a complete and honest
product.** Hand it over as exactly that. Do not describe the plant as a
closed-loop PV-DG control installation until at least one manufacturer profile has
passed physical readback qualification on this site's own equipment, and the
evidence for it is attached to this pack.
