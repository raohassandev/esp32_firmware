# AISH-OS Live TODO v9

Master program: #79. Live repository truth overrides this checklist. **Reconciliation parent:** `dev` `26e9d103462ead5dbc09ff47c30aff3346f7d5a8` after PR #196. This file records the known parent before this reconciliation; every execution cycle must fetch live `dev` before acting.

## RELEASE-CRITICAL EXECUTION QUEUE

- [ ] **P0 / L16 / #174 — execute frozen Industrial UI Waveshare candidate.** Exact PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`, tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`, artifact `10293685030`, digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`. Prove native 800x480/touch/roles, Network workflow, Grid+Gen1..3 mappings, explicit Transfer/ATS and Sync applicability, alarms/role-gated ACK, real board↔simulator Modbus, browser/resource health, and one uninterrupted >=4 h / >=240-sample run. Do not modify PR #179 before disposition.
- [ ] **P0 / L2 / #80 — Generator source-transition physical bench.** Execute exact Draft #106 candidate with authoritative breaker/run/ATS/sync and meter sign/scaling evidence. After genuine PASS replay the identical validated runtime slice onto then-current `dev`, earn fresh exact-head CI and merge zero-behind.
- [ ] **P0 / L5 / #81 — Real site source commissioning.** PR #194 has completed the full external site/config/SLD/channel-map identity gate. Supply real authoritative wiring/manual/mapping, physical before/after toggle, meter raw/scale/sign, stale/recovery and persisted readback evidence. kW sign is never source authority.
- [ ] **P0 / L6 / #82 — Production inverter qualification.** PR #193 has completed the full device/manual/controller/endpoint identity gate. For every deployed model supply exact official manual/model/firmware, observed identity, controlled write/readback/rollback/safe-zero and signed production approval. Pending catalogue entries remain fail-closed.
- [ ] **P0 / L4 / #86 — Secure OTA physical qualification.** On one exact intended final OTA-capable release identity execute authenticated upload, invalid rejection, interrupted upload, power-loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, rollback and NVS persistence.
- [ ] **P0 / L7 / #83 — Integrated FAT/endurance/SAT.** PR #192 has completed the full final-release identity gate. After prerequisites pass, execute complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded peers/network recovery/resource trends and authorized signed SAT on that exact identity.
- [ ] **P0 / L11 / #91 — populate and pass final release evidence manifest.** PR #196 is the latest authority. Fill `evidence/candidates/final_release_traceability_observations.json` only from genuine accepted records. The validator externally locks source/tree/artifact/application/config/site-map/profile-manifest, site/config identity, all six lane evidence digests and the signed SAT digest. A final PASS also requires zero critical blockers.

## REV-A PRODUCT HARDWARE TRACK

- [x] **L9 / #178/#85 — controlled H2 CAD/routing requalification.** H2 freeze `a877e5d844af114a6e4386f6294f514288ca5df6`, tree `782189312aec046338d472858d65d8cb397bd473`, authoritative run `34702074827`, engineering artifact `10300950516`, RFQ/DFM candidate `10300571374` / digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`.
- [ ] **L9 / #178/#85 — intended-fabricator written DFM/capability.** Submit `Automatrix_PVDG_RevA_PROVIDER_RFQ_a877e5d844.zip`; obtain written acceptance for committed 0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge or exact DFM changes. Revalidate any required changes before fabrication authority advances.
- [ ] **L9 H4 / #162 — fabricate controlled lot and execute H4.** PR #187 tooling is merged. Bind exact board lot/serial/BOM/firmware and physically execute power/protection/USB, Ethernet, dual RS485, HMI serial, relays, enclosure, thermal and applicable environmental/EMC acceptance.

## RETIRED HISTORICAL WAVESHARE EVIDENCE

- [x] **L3 / #87/#24/#25/#26/#27 — retired as superseded historical evidence.** Exact source `87841ecee727fe1d814d4186be8c8c26e4afafb4` retains its short physical PASS and interrupted ~2 h / 121-sample record, but the >=4 h gate was never passed. Issues #24/#25/#26/#27/#87 and PRs #20/#57/#67 were closed `not_planned`/unmerged because PR #179/#174 is the sole current Waveshare release path. No historical PASS transfers to PR #179.

## ACTIVE MANAGEMENT / GOVERNANCE

- [ ] **L8 / #84 — governance reconciliation service.** Keep all eight authoritative artifacts synchronized after state changes; known-parent semantics only, with live `dev` re-fetched every cycle.
- [ ] **L13 / #93 — promotion graph hygiene.** Exact-head CI + zero-behind + expected-head guard; preserve frozen physical candidates and retire superseded graphs rather than leaving duplicate active paths.
- [ ] **L14 / #94 — orchestration cycle.** Physical waits do not stop genuinely independent work, but do not manufacture code churn when the remaining dependency is external.

## COMPLETED SOFTWARE / EVIDENCE AUTHORITY

- [x] Core runtime/config/Modbus/safety/OTA software and always-on regressions.
- [x] Industrial UI software chain and exact PR #179 build/package candidate; physical #174 remains pending.
- [x] PR #181/#183 complete #174 evidence authority including topology-correct Transfer/Sync applicability.
- [x] PR #184 retired the obsolete historical Waveshare graph without rewriting evidence.
- [x] PR #185 + run #224 requalified Rev-A H2 without weakening declared minima.
- [x] PR #187 merged fail-closed Rev-A H4 evidence tooling.
- [x] PR #188 introduced fail-closed final traceability tooling.
- [x] PR #192 hardened #83 FAT/SAT evidence to the full exact final release identity.
- [x] PR #193 hardened #82 inverter qualification to exact external device/manual/controller/endpoint identity with measured readback/rollback evidence.
- [x] PR #194 hardened #81 site commissioning to exact external site/config/SLD/channel-map identity with independent state/meter/recovery checks.
- [x] PR #196 hardened #91 final traceability to the full external release identity, all six lane evidence digests and signed SAT digest.

## GLOBAL DONE GATE

Do not claim 100% until every required physical/site/manufacturer/FAT/SAT gate genuinely passes against exact identities, all behavior-affecting promotions are merged with fresh exact-head CI, the final traceability manifest passes against the exact final release, signed SAT exists, and no critical blocker remains. CI and validators enforce evidence quality but cannot create hardware/site PASS.
