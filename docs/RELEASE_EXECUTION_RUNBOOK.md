# AISH-OS Release Execution Runbook

Baseline when this runbook was created: `dev` `10d964237a351a5577e19ff023485064a180843f` after PR #200.

This is the **operator/release-manager execution guide** for the remaining release work. It does not replace the authoritative root `TODO.md`, `BLOCKERS.md`, `PROGRAM_BOARD.md`, `EVIDENCE_INDEX.md`, or Issue #79. Live repository state and genuine physical evidence always override this document.

## 1. Operating model

Use four roles for every physical lane:

- **Release Manager** — freezes identities, controls evidence handoff, checks validators, manages promotion/merge decisions.
- **Physical Executor** — performs the real bench/panel/site actions and records observations. Must be authorized for the equipment involved.
- **Evidence Reviewer / QA** — checks raw logs, photos, meter values, hashes and validator results against the frozen identity.
- **Acceptance Owner** — signs manufacturer/site/SAT approval where required.

Before starting any lane, record the named people filling these roles in the relevant GitHub issue/evidence package. One person may fill more than one role only when the site/customer process allows it.

## 2. Critical path and parallel lanes

### Wave A — start in parallel now

1. **#174 / Industrial UI Waveshare physical acceptance** — frozen PR #179 image.
2. **#80 / Generator source-transition bench** — frozen Draft PR #106 runtime identity.
3. **#81 / Real-site source commissioning** — wiring/source/meter proof on the actual site.
4. **#82 / Inverter manufacturer qualification** — obtain exact official protocol/manuals and progress each deployed model through documented -> read-only -> write-qualified -> production-approved.
5. **Rev-A / #178/#85** — obtain intended-fabricator written DFM/capability acceptance. This hardware-product track is independent unless explicitly coupled to the firmware release.

These lanes should not wait for one another unless they share the same physical equipment/operator and cannot safely be executed concurrently.

### Wave B — after final release identity is stable

6. **#86 / Secure OTA physical qualification** — execute only on the exact intended final OTA-capable release identity after behavior-affecting runtime/profile/site changes are resolved.

### Wave C — integration

7. **#83 / Integrated FAT + endurance + signed SAT** — starts only after #174, #80 (including governed promotion), #81, all deployed #82 profiles, and #86 are accepted.

### Wave D — final release

8. **#91 / Final release traceability** — populate the final release manifest only from accepted evidence digests and the signed SAT record; require zero critical blockers.

### Independent Rev-A continuation

After fabricator DFM acceptance: controlled fabrication -> **#162 H4** physical prototype acceptance.

## 3. Daily execution discipline

At the start of every execution session:

1. Re-fetch live `dev`, relevant PR exact head, issue state and evidence contract.
2. Freeze the exact test identity before touching hardware.
3. Create a dedicated evidence directory outside the repository working tree or under an approved evidence location; never overwrite raw evidence.
4. Capture timestamps with timezone, operator identity, hardware serial/model, firmware/artifact identity, configuration identity and instrument references.
5. Keep automatic PV control disabled whenever the applicable test procedure requires it.
6. Save raw serial/network/HMI/meter evidence before summarizing results.
7. Hash immutable evidence packages with SHA-256.
8. Run the repository validator locally against **independently frozen expected identities**.
9. Attach/link raw evidence + validated JSON + digest to the relevant GitHub issue.
10. Do not promote or merge anything from a failed, partial or cross-identity run.

## 4. Lane #174 — Waveshare Industrial UI physical acceptance

Frozen identity:

- PR: #179
- candidate source SHA: `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree: `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact id: `10293685030`
- artifact digest: `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256: `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256: `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- dependencies lock SHA256: `fd82d3c69b9507f0b4966e1d7a9479a9ee49e7cc58b8199966db75d4af86eb91`
- effective sdkconfig SHA256: `4df1cfd44cd617b35cf3700daf70fc8633ae05132b4d334b68675757f6fed822`
- build container: `espressif/idf:v6.0.1`

Use the existing starter `evidence/candidates/industrial_ui_72a1a82_physical_observations.json`. Do not change its identity fields to another image.

Minimum physical scope includes native 800x480 visual/touch/roles, Network workflow, Grid + Generator 1..3 mappings, topology-correct optional Transfer/ATS and Sync applicability, alarm filters/sorts/Engineering ACK, real board<->bench Modbus request/success/decoded-value proof, and one uninterrupted `>=4 h` / `>=240` one-minute sample run.

Validate with:

```bash
python3 tools/industrial_ui_physical_acceptance.py \
  waveshare-serial.log \
  evidence/candidates/industrial_ui_72a1a82_physical_observations.json \
  --expected-candidate-sha 72a1a82a8fc5ad4406b5bd51fba1f80f9c182884 \
  --expected-tree-sha 3069c65b4234fcd2b6418f9bbbe7859f1cd9abce \
  --expected-artifact-digest sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541 \
  --expected-application-sha256 0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734 \
  --json
```

Do not lower the default `14400 s`, `240 samples`, `20 page cycles` or `20000` internal-DMA-free thresholds to obtain PASS.

**Exit condition:** validator PASS + raw evidence review + Issue #174 accepted against this exact candidate. Only then disposition/promote PR #179 under the governed replay/current-base process.

## 5. Lane #80 — Generator source-transition bench

Frozen runtime candidate:

- PR #106 source SHA: `a1620789235d21b515f9f245f2329fab88b50558`

Before execution freeze the exact flashed artifact digest, bench/site ID, persisted configuration identity, approved topology/SLD digest, approved source-map digest and approved Grid/Generator meter-map digest. Never invent these values from the validator example.

Start from:

```bash
cp tools/generator_transition_physical.example.json generator_transition_physical.json
```

Required physical matrix includes Grid -> Transfer -> Generator, Generator -> Transfer -> Grid, island/no-source, stale evidence, breaker/run conflict, transfer asserted, source loss, generator meter scaling/sign proof and fresh recovery dwell. Synchronism is executed only when supported by the authoritative topology; otherwise record authoritative `not_supported` evidence.

Validate with:

```bash
python3 tools/generator_transition_physical_verify.py generator_transition_physical.json \
  --expected-firmware-sha a1620789235d21b515f9f245f2329fab88b50558 \
  --expected-artifact-digest sha256:<exact-artifact-digest> \
  --expected-site-id <exact-bench-or-site-id> \
  --expected-config-identity <exact-config-identity> \
  --expected-topology-digest sha256:<approved-topology-digest> \
  --expected-source-map-digest sha256:<approved-source-map-digest> \
  --expected-meter-map-digest sha256:<approved-meter-map-digest> \
  --json
```

**Exit condition:** genuine #80 PASS on PR #106 identity. Then replay only the validated runtime slice onto then-current `dev`, prove equivalence, obtain fresh exact-head CI, require `behind_by=0`, and merge with expected-head guard. Do not merge stale PR #106 directly.

## 6. Lane #81 — Real-site source commissioning

Freeze exact flashed firmware/artifact, site ID, persisted configuration identity, approved site SLD digest and approved source-channel-map digest.

Start from:

```bash
cp tools/site_source_commissioning.example.json site-source-commissioning.json
```

For each Grid/Generator/ATS/breaker/run/sync evidence channel, capture actual manufacturer/model/manual or wiring reference, physical terminal or Modbus endpoint/Unit-ID/function/address/mask/polarity, raw before/after, runtime before/after, stale behavior, recovery timing, persistence readback and meter CT/PT/datatype/word-order/scale/sign proof. Power sign may corroborate direction but may not create source authority.

Validate with:

```bash
python3 tools/site_source_commissioning_verify.py \
  site-source-commissioning.json \
  --expected-firmware-sha <exact-flashed-sha> \
  --expected-artifact-digest sha256:<exact-artifact-digest> \
  --expected-site-id <exact-site-id> \
  --expected-config-identity <exact-config-identity> \
  --expected-site-sld-digest sha256:<approved-sld-digest> \
  --expected-channel-map-digest sha256:<approved-channel-map-digest> \
  --json
```

**Exit condition:** every required site channel qualified or authoritatively `not_supported`, configuration persistence accepted, meter sign/scaling proven, fail-closed behavior observed, and Issue #81 accepted.

## 7. Lane #82 — Production inverter qualification

Do this separately for **every deployed manufacturer/model/firmware/connection identity**. Manual inventory in PR #198 is discovery only; no pending profile may be unlocked from a filename or adjacent-family map.

Progression is mandatory and sequential:

1. `documented`
2. `read_only_qualified`
3. `write_qualified`
4. `production_approved`

Start from:

```bash
cp tools/inverter_profile_physical_evidence.example.json inverter-profile-evidence.json
```

For every profile freeze manufacturer, exact model, inverter firmware, profile source SHA, controller firmware SHA/artifact digest, exact official applicable manual revision + SHA256, connection topology, endpoint and Unit ID.

Example final validation:

```bash
python3 tools/inverter_profile_physical_evidence_verify.py \
  inverter-profile-evidence.json \
  --expected-stage production_approved \
  --expected-manufacturer '<EXACT_MANUFACTURER>' \
  --expected-model '<EXACT_MODEL>' \
  --expected-inverter-firmware '<EXACT_INVERTER_FW>' \
  --expected-profile-sha '<40-hex-profile-source-sha>' \
  --expected-controller-sha '<40-hex-controller-sha>' \
  --expected-controller-artifact-digest sha256:<controller-artifact-digest> \
  --expected-manual-revision '<EXACT_MANUAL_REVISION>' \
  --expected-manual-digest sha256:<downloaded-official-manual-digest> \
  --expected-topology <qualified-topology> \
  --expected-endpoint '<endpoint>' \
  --expected-unit-id <unit-id> \
  --json
```

Repeat with `--expected-stage documented`, `read_only_qualified` and `write_qualified` as those stages are reached. Automatic control stays disabled during controlled write qualification.

**Exit condition:** signed `production_approved` evidence exists for every deployed profile. Only then may a separate governed current-`dev` PR enable production writes for that exact profile identity.

## 8. Lane #86 — Secure OTA real-controller qualification

Do not start this lane until one immutable **intended final OTA-capable release identity** is frozen after required runtime/profile/site changes.

Start from:

```bash
cp tools/ota_physical_evidence.example.json ota_physical.json
```

Required scenarios include valid authenticated upload, invalid image rejection before write/selection, interrupted upload, controlled power loss, partial-image non-selection, previous-slot recovery, authenticated reboot into target, rollback-pending first boot, mark-valid only after stabilization, deliberate rollback, fail-closed control throughout uncertainty and exact NVS/config persistence.

Validate with:

```bash
python3 tools/ota_physical_evidence_verify.py ota_physical.json \
  --expected-source-sha <exact-release-source-sha> \
  --expected-tree-sha <exact-release-tree-sha> \
  --expected-artifact-digest sha256:<exact-package-digest> \
  --expected-app-digest sha256:<exact-application-digest> \
  --expected-config-identity <exact-config-identity> \
  --json
```

**Exit condition:** Issue #86 accepted for the same identity that will enter FAT/SAT.

## 9. Lane #83 — Integrated FAT / endurance / signed SAT

Preconditions: accepted #174; accepted #80 plus governed runtime promotion; accepted #81; every deployed #82 profile production-approved and promoted as required; accepted #86.

Freeze final source SHA/tree/artifact digest, config identity, site ID/site-map digest and approved profile-manifest digest. Then execute Grid, Generator and mixed-source FAT, all three Modbus connection modes, degraded peer/network/resource endurance and signed SAT.

Start from:

```bash
cp tools/integrated_fat_sat_evidence.example.json integrated-fat-sat-evidence.json
```

Validate with:

```bash
python3 tools/integrated_fat_sat_evidence_verify.py integrated-fat-sat-evidence.json \
  --expected-firmware-sha <40-hex-source-sha> \
  --expected-firmware-tree-sha <40-hex-tree-sha> \
  --expected-artifact-digest sha256:<artifact-digest> \
  --expected-config-identity <exact-config-identity> \
  --expected-site-id <exact-site-id> \
  --expected-site-map-digest sha256:<site-map-digest> \
  --expected-profile-manifest-digest sha256:<profile-manifest-digest> \
  --json
```

**Exit condition:** complete FAT/endurance PASS, zero fatal/resource-collapse counters, authorized signed SAT tied to the same exact release identity.

## 10. Lane #91 — Final traceability and release closure

Populate `evidence/candidates/final_release_traceability_observations.json` only after all six release lanes are genuinely accepted and SAT is signed.

Final validator requires independent expected values for release SHA/tree, artifact/application/config/site-map/profile-manifest digests, site/config identity, all six lane evidence digests and signed SAT digest:

```bash
python3 tools/release_traceability_verify.py \
  evidence/candidates/final_release_traceability_observations.json \
  --expected-release-sha <release-sha> \
  --expected-release-tree <release-tree> \
  --expected-artifact-digest sha256:<artifact-digest> \
  --expected-application-digest sha256:<application-digest> \
  --expected-config-digest sha256:<config-digest> \
  --expected-site-map-digest sha256:<site-map-digest> \
  --expected-profile-manifest-digest sha256:<profile-manifest-digest> \
  --expected-site-id <site-id> \
  --expected-configuration-identity <config-identity> \
  --expected-industrial-ui-evidence-digest sha256:<digest> \
  --expected-generator-transition-evidence-digest sha256:<digest> \
  --expected-site-commissioning-evidence-digest sha256:<digest> \
  --expected-inverter-profiles-evidence-digest sha256:<digest> \
  --expected-ota-evidence-digest sha256:<digest> \
  --expected-fat-sat-evidence-digest sha256:<digest> \
  --expected-signed-sat-digest sha256:<digest> \
  --json
```

**Exit condition:** validator PASS, signed SAT present, zero critical blockers, exact final release identity frozen and approved. Only here may master Issue #79 close and the program be called release-complete.

## 11. Independent Rev-A DFM / H4 path

Current clean H2 identity:

- H2 SHA: `a877e5d844af114a6e4386f6294f514288ca5df6`
- engineering artifact: `10300950516`
- RFQ/DFM artifact: `10300571374`
- provider artifact digest: `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`
- declared minima: 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge

First obtain written intended-fabricator DFM/capability acceptance or exact requested changes. If changes are required, rerun the controlled H2 pipeline and freeze the new accepted identity before fabrication.

After controlled fabrication, validate H4 evidence with:

```bash
python3 tools/reva_h4_physical_acceptance_verify.py reva-h4-evidence.json \
  --expected-h2-sha <accepted-h2-sha> \
  --expected-provider-digest sha256:<accepted-provider-package-digest> \
  --expected-firmware-sha <exact-flashed-firmware-sha> \
  --expected-firmware-artifact-digest sha256:<exact-firmware-artifact-digest> \
  --json
```

## 12. Stop / escalation rules

Stop the affected lane immediately when any of the following occurs:

- firmware/artifact/config/site/profile/manual identity changes mid-run;
- required raw evidence is missing or cannot be tied to the tested identity;
- an unsafe electrical condition appears;
- automatic control becomes enabled when the procedure requires it disabled;
- WDT, panic, `NO_MEM`, unexplained reboot or resource collapse occurs;
- meter sign/scaling cannot be independently proven;
- source state depends on kW sign instead of authoritative contacts/topology;
- inverter write/readback/rollback exceeds documented manufacturer limits/tolerance;
- an evidence validator fails for a substantive contract reason;
- a test would require guessing a manufacturer register, site mapping, topology or fabricator capability.

Record the FAIL factually. Fix the root cause, freeze the new identity where required, and rerun only the affected evidence scope under the governing issue rules.

## 13. Completion dashboard

| Lane | Can start now? | Dependency | Exit artifact |
|---|---|---|---|
| #174 Industrial UI | Yes | Frozen PR #179 hardware available | accepted physical evidence digest |
| #80 Generator transition | Yes | PR #106 bench + approved topology/maps | accepted physical evidence digest + governed replay |
| #81 Site commissioning | Yes | authorized site access + SLD/channel maps | accepted site evidence digest |
| #82 Inverter profiles | Yes, per model | exact official manual + inverter access | signed production-approved evidence per profile |
| #86 Secure OTA | Later | intended final release identity | accepted OTA evidence digest |
| #83 FAT/SAT | Later | #174/#80/#81/#82/#86 | FAT evidence + signed SAT digest |
| #91 Final traceability | Last | all six lane digests + signed SAT | release traceability PASS |
| Rev-A DFM/H4 | Yes, parallel | fabricator -> fabricated controlled lot | accepted H4 evidence digest |

The professional objective is to keep all Wave-A work moving in parallel, prevent cross-identity evidence contamination, and make every handoff validator-ready the first time.
