# Release checklist — Automatrix PV-DG Controller

**What this is:** the gate between *the code is done* and *this ships*. It is a
list of **checkable facts**, not opinions. Every item below is either satisfied by
a command whose output can be pasted in, or by a signed document, or by a named
person's recorded decision.

**How to use it:** one instance of this checklist per release, filled in and
archived. An unfilled box is an unsatisfied item. There is no "probably fine".

**What this is not:** a restatement of the other documents. It references them.

| Document | Role |
|---|---|
| `docs/RELEASE_READINESS.md` | The honest state of the product: what is demonstrated on hardware, in software, and not at all |
| `docs/FACTORY_ACCEPTANCE_TEST.md` | Bench acceptance, per unit, before shipping |
| `docs/SITE_ACCEPTANCE_TEST.md` | Plant acceptance, after commissioning |
| `docs/SITE_COMMISSIONING_RUNBOOK.md` | How the plant is configured and how a profile is qualified |
| `docs/ACQUISITION_TIMING_MEASUREMENTS.md` | Measured latency, and the defect a wrong timeout caused |
| `docs/ALARM_MANAGEMENT_RESEARCH.md` | ISA-18.2 / EEMUA 191 targets and the remaining gaps |
| `docs/SAMPLE_CONFIGURATION.md`, `config-samples/` | The shipped lab sample and the deliberately incomplete site template |

### Release header

| Field | Value |
|---|---|
| Release identifier | `______` |
| Commit being released | `______` |
| Branch | `______` |
| Binary SHA-256 | `______` |
| Release manager | `______` |
| Date | `______` |
| Scope claimed (see §8) | `______` |

---

## 1. CI is green, on this exact commit

- [ ] The `Firmware and web checks` workflow (`.github/workflows/esp-idf-build.yml`)
      has a **successful** run for the commit named above — not for an ancestor,
      not for a rerun of a different tree. Run URL: `______`
      Commit SHA the run reports: `______` (must equal the header)
- [ ] Both jobs succeeded: `web` `______`, `build` `______`
- [ ] The `build` job produced `build/automatrix_pvdg.bin` and the metrics artefact
      records **`Compiler warnings: 0`**. The workflow fails the build on any
      `warning:`, so a green build already proves this — paste the line: `______`
- [ ] Application binary size from the metrics artefact: `______` bytes.
      Free space in the app partition: `______`
- [ ] Every Python source contract in the `web` job passed. Number of contract
      steps in the workflow at this commit: `______`
- [ ] Every gcc-compiled unit test in the `web` job passed. Number of test steps:
      `______`

### 1.1 The production release gate

`tests/production_release_gate.py` is the only CI step that changes behaviour for
a release. On a normal push it **prints** the active blockers; on a
`workflow_dispatch` run with the `production_release` input set it **fails** if any
blocker is present.

- [ ] A `workflow_dispatch` run of the workflow was made for this commit with
      `production_release` = **true**. Run URL: `______`
- [ ] That run's `Enforce production release gate` step **passed**, printing
      `production release compile-time safety gate passed`. Pasted: `______`
- [ ] **If it failed:** paste the `PRODUCTION RELEASE BLOCKED` list verbatim and
      stop. The release does not proceed.
      `______`

The blockers that gate enforces, so they can be checked by hand as well:

- [ ] `AUTH_TEMPORARY_FIELD_BYPASS` in
      `components/web_server/engineering_auth.c` is 0 or absent: `______`
- [ ] `DEV_DEFAULT_ENGINEERING_PASSWORD` in `web/product-mode.js` is empty:
      `______`
- [ ] `PVDG_APPLY_BUILD_WIFI_PROVISIONING` defaults to `n` in
      `main/Kconfig.projbuild`: `______`
- [ ] `sdkconfig` does not enable build Wi-Fi provisioning: `______`
- [ ] `inverter_profiles.c` still excludes simulator-only profiles from writes and
      still carries the production-approved qualification gate: `______`

---

## 2. Zero profiles are production-approved — unless physical readback evidence exists and is cited

This is the product's central safety claim. `docs/RELEASE_READINESS.md` §1 states
it: **write-qualified or production-approved profiles: 0**, and automatic control
is structurally inhibited against physical equipment.

### 2.1 The default case: zero

- [ ] The number of profiles reporting production write authority from the
      compiled catalogue is **0**. Evidence: the `Check the release document
      matches the compiled profile catalogue` CI step passed
      (`tests/release_doc_catalogue_source_contract.py`), which **compiles and runs
      the real `inverter_profile_write_permission()` over the real catalogue** and
      asserts that no profile passes the production write gate. Step result:
      `______`
- [ ] `tests/lab_target_write_gate_source_contract.py` passed: a lab-simulator
      declaration can never reach production authority. Result: `______`
- [ ] `tests/inverter_write_permission_test.c` passed over the real catalogue:
      FORBIDDEN is zero, NULL is forbidden, no shipped profile can command
      production, simulator profiles never reach production, readback is mandatory
      even in the lab. Result: `______`
- [ ] `docs/RELEASE_READINESS.md` still contains the sentence asserting
      **production-approved profiles: 0** — the contract above fails the build if
      it does not: `______`

### 2.2 The exception: promoting a profile

A profile may be production-approved **only** if every one of the following is
true. If any is missing, the profile stays where it is and this release ships with
zero.

- [ ] Profile id being promoted: `______`
- [ ] Commit that changed its qualification level in
      `components/inverter_manager/inverter_profiles.c`: `______`
- [ ] The **complete** evidence list in `docs/SITE_COMMISSIONING_RUNBOOK.md` §8.1
      is satisfied, item by item, with a **file or photograph** for each — not a
      recollection. Bundle location: `______`
      Specifically, and each must be cited to a file in that bundle:
      - [ ] Exact manual + revision, matching the exact model and firmware on site:
            `______`
      - [ ] Identity register read back and matching the nameplate: `______`
      - [ ] Active power decoded and cross-checked against an **independent**
            measurement, ≥ 3 samples at different output levels: `______`
      - [ ] Control register determined **by observation**, ≥ 3 trials: `______`
      - [ ] Settle time measured over ≥ 5 trials, maximum recorded, margin stated:
            `______`
      - [ ] Readback semantics established — requested vs active: `______`
      - [ ] Ramp reconciliation recorded with the decision and who made it:
            `______`
      - [ ] A commanded write confirmed by readback **and** by independent power
            measurement: `______`
      - [ ] A commanded return to unlimited, confirmed the same way: `______`
      - [ ] Behaviour on loss of communication mid-command, observed and recorded:
            `______`
      - [ ] The safe-zero path exercised: `______`
      - [ ] Timestamped raw transcripts for every measurement: `______`
      - [ ] Every open item in `docs/HUAWEI_SUN2000_REGISTER_EVIDENCE.md` §7 (or
            the equivalent brand evidence document) either closed with evidence or
            **explicitly carried forward as still open**, with a statement of what
            the plant does in the meantime: `______`
- [ ] `docs/SITE_COMMISSIONING_RUNBOOK.md` §8.2 confirmed **not** relied upon:
      no simulator behaviour, no other site/model/firmware, no single trial, no
      "it seemed to work", no assumed Device Status mapping. Confirmed by:
      `______`
- [ ] The runbook §8.3 sign-off table is signed by all three roles: commissioning
      engineer, firmware reviewer, product owner. Signed: `______`
- [ ] `docs/RELEASE_READINESS.md` was updated to record the promotion **and the
      commit that made it** — the catalogue contract fails the build if the
      document and the code disagree. Updated: `______`
- [ ] The release document's headline claim was updated honestly: it can no longer
      say zero. What it says now: `______`

> **The default is zero.** Absence of an entry in §2.2 means no profile was
> promoted, which is the expected and correct state for this product today.

---

## 3. Factory acceptance test signed

- [ ] `docs/FACTORY_ACCEPTANCE_TEST.md` was **executed against the commit named in
      the header**, per unit. Number of units in this release: `______`
- [ ] For each unit, a completed record sheet exists with an overall verdict:

  | Unit serial | FAT verdict | Record sheet location | §14.4 signed by all three roles |
  |---|---|---|---|
  | `______` | `______` | `______` | `______` |

- [ ] No unit in this release has a FAT verdict of FAIL: `______`
- [ ] Every FAT blocking rule in `docs/FACTORY_ACCEPTANCE_TEST.md` §14.2 is clear
      for every unit — in particular:
      - [ ] no panic, backtrace or reboot loop (§1): `______`
      - [ ] no credential or SSID in any unauthenticated response (§5): `______`
      - [ ] no mutating endpoint reachable unauthenticated (§5): `______`
      - [ ] configuration survived a power cycle (§6): `______`
      - [ ] **commissioned Wi-Fi credentials survived a firmware update (§7)** —
            or §7 is recorded NOT RUN and carried into §5 of this checklist:
            `______`
      - [ ] automatic control was **not** enabled after an upgrade (§7.2): `______`
      - [ ] an alarm that cleared unacknowledged stayed visible (§8.3): `______`
      - [ ] journal records and sequence continuity survived a reset (§9): `______`
      - [ ] no profile reported production authority (§10): `______`
      - [ ] the gate never reported production scope (§11.4): `______`
      - [ ] **no unit ships with a lab target declared (§12)**: `______`
- [ ] Every FAT **NOT RUN** section, across all units, is listed in §5 of this
      checklist. Listed: `______`

---

## 4. Site acceptance test signed

- [ ] `docs/SITE_ACCEPTANCE_TEST.md` was executed at the plant, after
      commissioning, against the commit in the header. Site: `______`
- [ ] Which parts were in scope, per §0.1 of that document: **A only / A and B:**
      `______`
- [ ] The commissioning report it depends on exists and is signed: `______`
- [ ] The SAT record sheet is complete and §13.4 is signed by every applicable
      role — test engineer, site representative authorised to halt the work,
      genset operator (if Part B ran), plant operations accepting hand-over, and
      the product owner. Signed: `______`
- [ ] No SAT failure condition in §13.2 of that document is open: `______`
- [ ] Operator hand-over §12.1 was completed **by the operator unaided**: `______`
- [ ] The values in SAT §13.3 — every quantity the project did not have and the
      site supplied — are transcribed into §5 below or into
      `docs/RELEASE_READINESS.md` §6: `______`
- [ ] **If Part B was out of scope:** the release is described as monitoring,
      commissioning and protection firmware only (§8), and nothing in the release
      notes describes it as a closed-loop controller. Confirmed: `______`
- [ ] **If Part B ran:** §10.2 (loss of the controller) demonstrated a safe plant
      state, or the product owner accepted the observed behaviour in writing.
      Which, and the reference: `______`

---

## 5. The open owner decisions are closed

`docs/RELEASE_READINESS.md` §5 lists decisions deliberately not made
unilaterally, and §6 lists inputs still required. **A decision is "closed" when a
named person has recorded a decision, with a date.** "Current behaviour is X" is
not a closure; it is a description.

### 5.1 Decisions

| # | Decision (see `docs/RELEASE_READINESS.md` §5) | Closed? | Decided by | Date | Decision recorded where |
|---|---|---|---|---|---|
| D1 | Should a disabled generator ramp block commissioning? | `______` | `______` | `______` | `______` |
| D2 | Should one unqualified inverter block the whole plant? | `______` | `______` | `______` | `______` |
| D3 | Was deleting `inverter_command_policy.{c,h}` correct? | `______` | `______` | `______` | `______` |
| D4 | Settle window per profile / global 5000 ms deadline | `______` | `______` | `______` | `______` |
| D5 | Repeated-mismatch policy (no "N strikes and out" latch) | `______` | `______` | `______` | `______` |
| D6 | `.eyebrow` brand orange at 2.23:1 on light, 10 px | `______` | `______` | `______` | `______` |
| D7 | Is the lab-simulator write authority acceptable? | `______` | `______` | `______` | `______` |
| D8 | Which register for Huawei percentage control? | `______` | `______` | `______` | `______` |
| D9 | Should the controller write the inverter's own ramp gradient? | `______` | `______` | `______` | `______` |

- [ ] Every row above has a name and a date, or the release does not proceed:
      `______`
- [ ] Read `docs/RELEASE_READINESS.md` §5 at the release commit and confirm the
      list has not grown since this table was written. Rows in that table now:
      `______`. Any row missing above: `______`

### 5.2 Inputs still required

From `docs/RELEASE_READINESS.md` §6. Each is either **supplied** (with a source)
or **explicitly accepted as still missing** by the product owner.

| Input | Supplied? | Source / who accepted its absence | Date |
|---|---|---|---|
| Generator rated kW | `______` | `______` | `______` |
| Generator minimum loading % (an **engine** limit) | `______` | `______` | `______` |
| Generator reserve kW | `______` | `______` | `______` |
| Reverse-power margin kW | `______` | `______` | `______` |
| Engineering password (to drive the loop through the firmware at all) | `______` | `______` | `______` |
| GoodWe manual, and any manual intended for write qualification | `______` | `______` | `______` |
| Huawei *Inverter Key Signal Extension Description* (Device Status code table) | `______` | `______` | `______` |
| `SmartLogger ModBus Interface Definitions` for the installed model | `______` | `______` | `______` |
| Confirmation that nothing else writes the inverter power-limit registers | `______` | `______` | `______` |

- [ ] **A missing generator rating is not a blocker to shipping** — the
      commissioning gate treats zero as "not commissioned" and holds PV at zero —
      but it **is** a blocker to claiming closed-loop control. Which claim is being
      made: `______`

### 5.3 Known-open items carried forward

Every "Not demonstrated" item in `docs/RELEASE_READINESS.md` §4 and every NOT RUN
from the FAT and SAT is listed here with an owner, or it is not carried forward at
all — it is forgotten, which is the failure this section prevents.

| Item | Source | Owner | Accepted for this release by | Date |
|---|---|---|---|---|
| `______` | `______` | `______` | `______` | `______` |

- [ ] Count of items in `docs/RELEASE_READINESS.md` §4 at the release commit:
      `______`. All listed above: `______`

---

## 6. No credential is in the repository

**The repository is public.** A credential committed here is disclosed
permanently, including in history.

- [ ] `git log -p` for this release's diff contains no password, key, token or
      certificate: `______`
- [ ] `sdkconfig` in the released tree does not set
      `CONFIG_PVDG_PRIMARY_WIFI_PASSWORD` or `CONFIG_PVDG_DEFAULT_WIFI_PASSWORD`
      to a non-empty value, and does not enable
      `CONFIG_PVDG_APPLY_BUILD_WIFI_PROVISIONING`. `tests/production_release_gate.py`
      checks all three (§1.1). Confirmed: `______`
- [ ] `web/product-mode.js` `DEV_DEFAULT_ENGINEERING_PASSWORD` is empty (§1.1):
      `______`
- [ ] No document added or edited in this release records an Engineering password,
      a Wi-Fi passphrase, or a site credential. Every place one is needed says
      **obtain it from the product owner**. Checked in: `docs/FACTORY_ACCEPTANCE_TEST.md`,
      `docs/SITE_ACCEPTANCE_TEST.md`, `docs/SITE_COMMISSIONING_RUNBOOK.md`,
      `docs/SAMPLE_CONFIGURATION.md`, `config-samples/`. Confirmed: `______`
- [ ] No FAT or SAT record sheet being archived contains a password. Confirmed:
      `______`
- [ ] `scripts/lab_run.py` takes the Engineering password as an argument and does
      not write it to disk. Confirmed unchanged: `______`

### 6.1 The known shipped default — a decision, not a check

`components/config_manager/config_manager.c` `defaults()` sets a **hard-coded
default fallback-AP SSID and password**. It is committed in a public repository, so
it is public, and it is the same on every unit until an operator changes it. That
is a shipped credential by any reasonable definition.

- [ ] The product owner has recorded a decision on it — change it per unit, force
      a change on first use, remove the default, or accept it. Decision: `______`
      Decided by: `______` Date: `______`
- [ ] If accepted: the FAT and hand-over both tell the operator to change it, and
      the record sheet shows it was changed on each unit: `______`
- [ ] **Do not transcribe the value into this checklist.** Cite the file and the
      function.

---

## 7. The release document's profile table matches the compiled catalogue

This is already enforced in CI, and the contract exists because the drift already
happened: the table listed 7 profiles when the catalogue held 9, silently omitting
two — one of them added in the very commit the document was assessing.

- [ ] `tests/release_doc_catalogue_source_contract.py` passed in the CI run cited
      in §1, as the step `Check the release document matches the compiled profile
      catalogue`. Result: `______`
- [ ] Understand what it proves, because it is stronger than a text comparison: it
      **compiles `components/inverter_manager/inverter_profiles.c` and
      `inverter_status.c` together with `tests/support/profile_authority_probe.c`
      and executes the real `inverter_profile_write_permission()`** over the real
      catalogue, then asserts that
      `docs/RELEASE_READINESS.md`'s table agrees with what the firmware decided.
      An earlier version of that test reimplemented the rule in Python, the copy
      drifted, and the test passed while the document was wrong. Confirmed the
      probe-based version is the one in the tree: `______`
- [ ] Number of profiles the contract reports: `______`. It must equal the number
      of catalogue entries in `inverter_profiles.c`: `______`
- [ ] Every profile in the catalogue appears in the document's table, with a
      matching qualification level, a matching authority, and — for every refused
      profile — a stated reason. Enforced by the contract; confirmed: `______`
- [ ] **Never satisfy this item by editing the document to match a broken
      catalogue.** If the contract fails, decide which side is wrong and fix that
      side. Which side was wrong, if it failed: `______`

---

## 8. The claim being shipped

The release notes and any customer-facing description must match
`docs/RELEASE_READINESS.md` §7. Tick exactly one.

- [ ] **Monitoring, commissioning and protection firmware.** Automatic control is
      documented as inhibited pending profile qualification. This is the
      recommendation in `docs/RELEASE_READINESS.md` §7 and is the correct claim
      whenever §2.2 above is empty.
- [ ] **Closed-loop PV-DG control**, with at least one manufacturer profile that
      has passed physical readback qualification per §2.2, and the
      `docs/RELEASE_READINESS.md` §4 items closed. Profile: `______`

- [ ] The release notes do **not** use the words *qualified*, *verified*,
      *validated* or *production-ready* about anything the repository does not have
      evidence for. Reviewed by: `______`
- [ ] The release notes state plainly which of the two claims above applies:
      `______`
- [ ] `docs/RELEASE_READINESS.md` at the release commit is internally consistent —
      no section says a capability is refused while the table says it is
      permitted, and every cross-reference resolves to a section that exists.
      Reviewed by: `______`. Discrepancies found and how they were resolved:
      `______`

---

## 9. Sign-off

No single person may sign this checklist alone. Each role signs for a distinct
question, and each signature means the signer **read the evidence**, not that they
were told it exists.

| Role | Signs for | Name | Date | Signature |
|---|---|---|---|---|
| Release manager | §1 CI, §7 catalogue contract, the header facts, and that this checklist is complete | `______` | `______` | `______` |
| Firmware reviewer | §2 profile authority, §6 no credential, and that the code changes in this release were reviewed | `______` | `______` | `______` |
| Test engineer | §3 FAT, for every unit in this release | `______` | `______` | `______` |
| Commissioning engineer | §4 SAT, and the commissioning report it rests on | `______` | `______` | `______` |
| Product owner | §5 open decisions and inputs, §6.1 the shipped default, §8 the claim being made, and the residual risk of every item carried forward | `______` | `______` | `______` |

### 9.1 Hard stops

The release **does not proceed** while any of these is true. Each is a fact, not a
judgement.

| # | Stop condition | Checked by | Clear? |
|---|---|---|---|
| 1 | CI is not green on the exact release commit | §1 | `______` |
| 2 | The `production_release` gate run failed or was not made | §1.1 | `______` |
| 3 | Any profile passes the production write gate without a complete, cited §2.2 evidence bundle | §2 | `______` |
| 4 | The FAT is unsigned, or any unit has a FAT verdict of FAIL, or any §14.2 blocking rule is open | §3 | `______` |
| 5 | The SAT is unsigned, or any §13.2 failure condition is open | §4 | `______` |
| 6 | Any decision in `docs/RELEASE_READINESS.md` §5 has no named decision-maker and date | §5.1 | `______` |
| 7 | Any credential is present in the repository, in a record sheet, or in an unauthenticated API response | §6 | `______` |
| 8 | §6.1's shipped default has no recorded owner decision | §6.1 | `______` |
| 9 | The release-document catalogue contract is failing, or was satisfied by editing the document to match a broken catalogue | §7 | `______` |
| 10 | The release notes claim more than §8 permits | §8 | `______` |
| 11 | Any signature row in §9 is blank | §9 | `______` |
| 12 | A unit ships with a lab target declared, or with `commissioning_scope` anything other than `none` at the factory | §3 | `______` |

- [ ] All twelve clear: `______`
- [ ] This completed checklist, both acceptance-test record sheets, the
      commissioning report, and the CI run links are archived together where they
      can be found in two years: `______`

**If an item cannot be checked, it is not satisfied. Shipping while an item on
this list is open is a decision to ship on an unverified claim, and it must be
recorded here, by name, as exactly that:** `______`
