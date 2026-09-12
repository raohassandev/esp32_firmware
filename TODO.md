# AISH-OS Live TODO v8

Master program: #79. Live repository truth overrides this checklist. **Reconciliation parent:** `dev` `c333db0752be89bdcb9bfbda01e2f19a1f05709f` (PR #188 merge). This file deliberately records the known parent before this reconciliation; every execution cycle must fetch live `dev` before acting.

## RELEASE-CRITICAL EXECUTION QUEUE

- [ ] **P0 / L16 / #174 — execute frozen Industrial UI Waveshare candidate.** Exact PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`, tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`, artifact `10293685030`, digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`. Physically prove native 800x480/touch/roles, Network workflow, Grid+Gen1..3 mappings, explicit Transfer/ATS and Sync applicability, alarm filters/sorts/Engineering ACK, real board↔simulator Modbus evidence, browser/resource health and one uninterrupted >=4 h / >=240-sample run. Do not modify PR #179 before disposition.
- [ ] **P0 / L2 / #80 — Generator source-transition physical bench.** Execute the exact Draft #106 candidate with authoritative breaker/run/ATS/sync and meter sign/scaling evidence. After genuine PASS replay the identical validated runtime slice onto then-current `dev`, earn fresh exact-head CI and merge zero-behind.
- [ ] **P0 / L5 / #81 — Real site source commissioning.** Execute exact Grid/Gen/ATS/sync mapping, address/contact/mask/polarity/manual/wiring provenance, meter mapping/sign/scaling, physical toggle, stale and recovery evidence. kW sign is never source authority.
- [ ] **P0 / L6 / #82 — Production inverter qualification.** For every deployed model obtain exact applicable official manual/model/firmware, physically verify identity/telemetry/status, controlled write/readback/failure/rollback/safe-zero and signed production approval. Pending catalogue entries remain fail-closed; no guessed maps.
- [ ] **P0 / L4 / #86 — Secure OTA physical qualification.** On one exact intended final OTA-capable release identity execute authenticated upload, invalid rejection, interrupted upload, power-loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, rollback and NVS persistence.
- [ ] **P0 / L7 / #83 — Integrated FAT/endurance/SAT.** After prerequisites pass, execute complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded peers/network resets/recovery/resource trends and authorized signed SAT.
- [ ] **P0 / L11 / #91 — populate and pass final release evidence manifest.** PR #188 tooling is merged. Fill `evidence/candidates/final_release_traceability_observations.json` only from genuine accepted records, bind final SHA/tree/artifact/application/config/site-map/profile-manifest/UI/generator/site/inverter/OTA/FAT-SAT identities, require signed SAT and zero critical blockers, then validate with `tools/release_traceability_verify.py`.

## REV-A PRODUCT HARDWARE TRACK

- [x] **L9 / #178/#85 — controlled H2 CAD/routing requalification.** Fresh KiCad 10.0.5 run `34702074827` earned immutable H2 freeze `a877e5d844af114a6e4386f6294f514288ca5df6`, tree `782189312aec046338d472858d65d8cb397bd473`, engineering artifact `10300950516`, and RFQ/DFM candidate artifact `10300571374` / digest `sha256:27b1709537715f08e928e65137262553791a95f957c19703da8dc3a104db0d30`. ERC/DRC/unconnected/SI/STEP/mechanical/manufacturing gates are clean. No global rule relaxation created this PASS.
- [ ] **L9 / #178/#85 — intended-fabricator written DFM/capability.** Submit `Automatrix_PVDG_RevA_PROVIDER_RFQ_a877e5d844.zip` to the selected fabricator and obtain written acceptance for the committed geometry/minima (0.20 mm drill / 0.18 mm hole clearance / 0.25 mm copper-edge) or exact DFM changes. If changes are required, commit them narrowly and rerun H2 before fabrication authority advances.
- [ ] **L9 H4 / #162 — fabricate controlled lot and execute H4.** PR #187 fail-closed H4 tooling is merged. Bind exact board lot/serial/BOM/firmware and physically execute power/protection/USB, Ethernet, dual RS485, HMI serial, relays, enclosure, thermal and applicable environmental/EMC acceptance. The shipped starter remains deliberately unexecuted.

## RETIRED HISTORICAL WAVESHARE EVIDENCE

- [x] **L3 / #87/#24/#25/#26/#27 — retired as superseded historical evidence.** Exact source `87841ecee727fe1d814d4186be8c8c26e4afafb4` retains its short physical PASS and interrupted ~2 h / 121-sample record, but the >=4 h gate was never passed. Issues #24/#25/#26/#27/#87 and PRs #20/#57/#67 are closed/superseded. No historical PASS transfers to PR #179.

## ACTIVE MANAGEMENT / GOVERNANCE

- [ ] **L8 / #84 — governance reconciliation service.** Keep all eight authoritative artifacts synchronized after state changes. A reconciliation file records its known parent baseline, not an unknowable future merge SHA; live `dev` must be fetched every cycle.
- [ ] **L13 / #93 — promotion graph hygiene.** Exact-head CI + zero-behind + expected-head merge; preserve frozen physical candidates and retire superseded graphs rather than leaving duplicate active paths.
- [ ] **L14 / #94 — orchestration cycle.** Physical waits do not stop genuinely independent software/governance/hardware work.

## COMPLETED SOFTWARE / EVIDENCE AUTHORITY

- [x] Core runtime/config/Modbus/safety/OTA software and always-on regressions.
- [x] Industrial UI software chain PR #165/#167/#169/#172/#173/#176.
- [x] PR #179 exact Waveshare Industrial UI candidate software/build/package is GREEN and frozen; physical #174 remains pending.
- [x] PR #181/#183 complete #174 evidence authority including topology-correct optional Transfer/Sync semantics.
- [x] PR #184 retired the obsolete historical Waveshare release graph without rewriting evidence.
- [x] PR #185 + authoritative run #224 repaired/requalified Rev-A H2 without weakening declared minima.
- [x] PR #187 merged fail-closed Rev-A H4 evidence tooling and an intentionally unexecuted starter.
- [x] PR #188 merged fail-closed final release traceability validator, regression suite and intentionally unexecuted release manifest starter.
- [x] Physical/evidence validators for source transition, OTA, site source, inverter profiles and integrated FAT/SAT are implemented and fail closed on incomplete evidence.

## GLOBAL DONE GATE

Do not claim 100% until every required physical/site/manufacturer/FAT/SAT gate genuinely passes against exact identities, all behavior-affecting promotions are merged with fresh exact-head CI, the final traceability manifest passes against the exact final release, signed SAT exists, and no critical blocker remains. CI and validators enforce evidence quality but cannot create hardware/site PASS.
