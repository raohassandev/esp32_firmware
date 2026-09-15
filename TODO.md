# AISH-OS Live TODO v11

Master program: #79. **Known reconciliation parent before this revision:** `dev` `ac436b18f1f5e0437a2ad523c352ac8fac0591c6` after PR #202. The `feature/software-product-consolidation` follow-up closes remote UI/reporting ownership and resilience gaps. This parent is historical identity for the reconciliation only; **live `dev` always overrides this document and must be fetched before acting.**

## RELEASE-CRITICAL EXECUTION QUEUE

The remaining release-critical queue is physical/external. Software CI, validators, simulations and browser evidence do not satisfy these gates.

- [ ] **P0 / L16 / #174 — execute frozen Industrial UI Waveshare candidate.** Exact PR #179 head `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`, artifact `10293685030`. Physically prove native 800x480/touch/roles, Network workflow, Grid+Gen1..3 source mapping, topology-correct Transfer/ATS and Sync applicability, alarms/ACK, real board↔bench Modbus evidence and one uninterrupted >=4 h / >=240-sample run.
- [ ] **P0 / L2 / #80 — execute Generator source-transition physical bench.** Draft PR #106 remains the frozen runtime candidate. PR #195 hardens exact firmware/artifact/site/config/topology/source-map/meter-map identity, meter scaling/sign evidence and recovery chronology. Genuine bench PASS is required before governed current-`dev` replay/promotion.
- [ ] **P0 / L5 / #81 — execute real site source commissioning.** PR #194 evidence hardening is merged; collect exact wiring/manual/channel-map/SLD identity, physical toggles, stale/recovery and meter CT/PT/type/word-order/scale/sign proof.
- [ ] **P0 / L6 / #82 — qualify every deployed production inverter profile.** Generic framework, manual-source discovery and assignment backup/restore are complete. PR #198 inventories immutable manual-source SHAs; PR #199 provides fail-closed 12-channel compiled-profile assignment backup/restore. Still obtain exact installed manufacturer/model/firmware/connection + accepted official manual applicability, extract only documented mappings, then physical identity/read-only proof, controlled write/readback/failure/rollback/safe-zero and signed production approval. Pending profiles stay fail-closed.
- [ ] **P0 / L4 / #86 — execute Secure OTA physical qualification.** Software OTA workflow/backend is complete and rollback-safe by design, but one exact intended release identity must physically prove valid/invalid/interrupted/power-loss/previous-slot/pending-verification/mark-valid/rollback/NVS behavior.
- [ ] **P0 / L7 / #83 — execute integrated FAT/endurance/SAT.** PR #192 exact-final-release evidence hardening is merged. After prerequisites pass, run complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded peers/network/resource endurance and obtain authorized signed SAT.
- [ ] **P0 / L11 / #91 — populate and pass final release evidence manifest.** PRs #188, #190 and #196 lock full source/tree/artifact/application/config/site-map/profile identity, all six mandatory lane evidence digests and signed SAT digest. Populate only from genuine accepted records and require zero critical blockers.

## REV-A PRODUCT HARDWARE TRACK

- [x] **L9 / #178/#85 — H2 CAD/routing requalification.** Clean freeze `a877e5d844af114a6e4386f6294f514288ca5df6`; ERC/DRC/unconnected/SI/STEP/mechanical/manufacturing gates clean; engineering artifact `10300950516`; RFQ/DFM artifact `10300571374`.
- [ ] **L9 / #178/#85 — intended-fabricator written DFM/capability.** Obtain written acceptance for the committed geometry/minima or exact DFM changes; rerun H2 if changes are required.
- [ ] **L9 H4 / #162 — fabricate controlled lot and execute H4.** PRs #187 and #191 provide fail-closed evidence tooling and exact binary/evidence chronology checks; no physical H4 PASS exists yet.

## RETIRED HISTORICAL WAVESHARE EVIDENCE

- [x] **L3 / #87/#24/#25/#26/#27 — retired as superseded historical evidence.** Exact historical source `87841ecee727fe1d814d4186be8c8c26e4afafb4` retains only its own short physical PASS and interrupted ~2 h / 121-sample record. Issues #24/#25/#26/#27/#87 and PRs #20/#57/#67 were closed as superseded; the >=4 h, backend-parity and persistence/ARM matrices were never completed and no evidence transfers to PR #179.

## ACTIVE MANAGEMENT / GOVERNANCE

- [ ] **L8 / #84 — governance reconciliation service.** Keep `EXECUTION_TREE.yaml`, `AGENT_REGISTRY.yaml`, `GATES.yaml`, `REQUIREMENTS_MATRIX.md`, `TODO.md`, `BLOCKERS.md`, `PROGRAM_BOARD.md`, and `EVIDENCE_INDEX.md` synchronized after every genuine state change. This v11 reconciliation records the software-product completion work after PR #202 without changing any physical evidence state.
- [ ] **L13 / #93 — promotion graph hygiene.** Require exact-head CI, zero-behind, expected-head merge guard, frozen-candidate preservation and explicit evidence equivalence on post-PASS replay.
- [ ] **L14 / #94 — orchestration cycle.** Physical waits do not justify fake software churn; continue only genuinely independent work.

## COMPLETED SOFTWARE / EVIDENCE AUTHORITY

- [x] Core runtime/config/Modbus/safety/OTA software and always-on regressions.
- [x] Industrial UI software chain and exact PR #179 package/build tooling.
- [x] Physical/evidence validators for Industrial UI, generator transition, site commissioning, inverter qualification, OTA, integrated FAT/SAT, Rev-A H4 and final release traceability.
- [x] Safety/identity hardening PRs #190/#191/#192/#193/#194/#195/#196 merged.
- [x] PR #198 — immutable inverter manual-source inventory + public manufacturer discovery boundary; no production authority inferred.
- [x] PR #199 — fail-closed compiled-profile assignment manifest backup/restore, exact definition fingerprint, atomic persistence, live+persistent control disable, restart required, no register/qualification/approval import.
- [x] PR #201 — release physical-execution runbook for the remaining genuine hardware/site gates.
- [x] PR #202 — professional controller-resident Reports workspace and guided rollback-safe OTA maintenance UX; browser evidence explicitly remains non-authoritative for physical OTA qualification.
- [x] Software product consolidation/deep audit — route alias repair, singular shell/navigation ownership, retirement of `shell-current-fixes`, theme-safe Product Experience, HMI header de-duplication, partial-source Reports resilience, visible/exported data-quality metadata, self-contained HTML evidence export and stronger exact-source regression contracts. See `docs/SOFTWARE_PRODUCT_DEEP_AUDIT_2026-09-15.md`.
- [x] Current `sdkconfig.defaults` already enables N16R8 octal PSRAM and leaves socket headroom (`CONFIG_LWIP_MAX_SOCKETS=16`); no speculative hardware-setting churn was introduced by this audit.
- [x] Historical duplicate Waveshare graph retired without rewriting its incomplete evidence.

## SOFTWARE-ONLY STOP CONDITION

Remote software implementation is considered complete when the software-product-consolidation change is merged to current `dev` with exact-head CI green and zero-behind. At that point, additional churn must be driven by a reproduced software defect or by evidence from the remaining physical gates—not by an arbitrary desire to create more software work.

This software-only stop condition does **not** mean the product/release is 100% complete.

## GLOBAL DONE GATE

Do **not** claim 100% product/release completion until #174, #80, #81, #82, #86 and #83 genuinely pass against exact identities, any required post-PASS runtime promotion is merged with fresh exact-head CI, #91 final traceability passes with signed SAT and all mandatory evidence digests, and no critical blocker remains. CI, validators, manual inventory, reporting exports and simulator tooling enforce quality; they cannot create hardware/site/manufacturer PASS.