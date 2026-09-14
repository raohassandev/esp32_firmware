# AISH-OS v9 Program Board

**Reconciliation parent:** `dev` `3129f7a17550ae5262e4d51c381dda12dedacc16`. This board records the known parent before reconciliation; live `dev` must be re-fetched before decisions, merges or physical verdicts.

## Executive state

Known product runtime, Industrial UI software, generic safety/Modbus/OTA logic, evidence validators and release identity hardening are complete for the current scope. **The program is not release-complete.** Remaining work is real physical/site/manufacturer/fabricator execution plus signed acceptance and post-PASS promotion where required.

| Lane | Deliverable | State | Authority / next gate |
|---|---|---|---|
| L16 | Industrial UI Waveshare release | SOFTWARE GREEN / PHYSICAL PENDING | PR #179 frozen candidate; execute #174 exact-image matrix. |
| L2 | Generator transition | SOFTWARE GREEN / PHYSICAL PENDING | Draft PR #106 runtime; PR #151 + #195 evidence authority; execute #80. |
| L5 | Site source commissioning | PHYSICAL SITE PENDING | PR #156 + #194 evidence authority; execute #81. |
| L6 | Production inverter profiles | MANUFACTURER + PHYSICAL + SIGNED PENDING | PR #158 + #193 evidence authority; execute #82 per model. |
| L4 | Secure OTA | SOFTWARE COMPLETE / PHYSICAL PENDING | Execute #86 on exact intended final release identity. |
| L7 | Integrated FAT/SAT | PREREQUISITES + PHYSICAL + SIGNED PENDING | PR #160 + #192 evidence authority; execute #83. |
| L11 | Final release traceability | TOOLING COMPLETE / INPUTS PENDING | PR #188 + #190 + #196; populate #91 only from accepted evidence. |
| L9 | Rev-A custom PCB | H2 PASS / DFM + FABRICATION + H4 PENDING | PR #19, H2 `a877e5d...`, PR #187 + #191 H4 evidence tooling. |
| L3 | Historical Waveshare release | RETIRED / SUPERSEDED EVIDENCE ONLY | Short PASS preserved; incomplete final gates do not transfer. |

PR #179 is the sole current Waveshare release candidate. The historical `87841ece...` graph is evidence-only and cannot become an alternate release path.

## Current integration/tooling state

The latest safety/evidence audit chain is merged on `dev`:
- PR #190 — externally lock final release SHA/tree/artifact identity.
- PR #191 — harden Rev-A H4 binary identity, DFM chronology and immutable physical evidence.
- PR #192 — bind integrated FAT/SAT to exact final release identity.
- PR #193 — bind inverter qualification to exact manufacturer/model/firmware/manual/controller/endpoint identity and measured readback/rollback evidence.
- PR #194 — bind site commissioning to exact site/config/SLD/channel-map identity and observed physical state changes.
- PR #195 — bind generator-transition evidence to exact physical identity, meter scaling/sign proof and recovery chronology.
- PR #196 — bind final traceability to complete application/config/site/profile identity, all mandatory lane evidence digests and signed SAT digest.

These PRs are evidence/tooling hardening only; none claims a physical PASS.

## Release dependency order

1. Execute #174 on frozen PR #179.
2. Execute #80 on frozen Draft PR #106; after PASS perform governed current-`dev` runtime replay/equivalence and merge.
3. Execute #81 real-site source/meter commissioning.
4. Complete #82 exact deployed inverter approvals.
5. Execute #86 exact-release OTA matrix.
6. Execute #83 integrated FAT/endurance and obtain authorized signed SAT.
7. Populate/pass #91 final traceability with zero critical blockers.

Independent Rev-A track: obtain intended-fabricator DFM/capability, fabricate the controlled lot, then execute #162 H4 using PR #187/#191 tooling.

## Release discipline

- Exact-head CI and `behind_by=0` are mandatory before governed merges.
- Frozen physical candidates are not rebased merely to make them current.
- Physical PASS never transfers silently across source/artifact/config/profile/site identities.
- CI, validators and simulators can reject bad evidence; they cannot manufacture physical evidence.
