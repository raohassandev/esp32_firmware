# AISH-OS v6 Program Board

Authoritative master: #79. **Reconciliation parent:** `dev` `f924726db9c24ab55ebe4f43fe28caf75a9b2c3d` after PR #183. Live repository and genuine physical evidence override this board. This board intentionally records the known parent before its own revision rather than claiming an unknowable future merge SHA.

## Executive board

| Lane | Scope | State | Execution owner / next gate |
|---|---|---|---|
| L0/L8 | Program management/governance | CONTINUOUS / LIVE RECONCILIATION | ChatGPT / #84 |
| L1 | Modbus modes/deadlines | SOFTWARE COMPLETE | #83 physical endurance |
| L2 | Generator source transition | SOFTWARE GREEN / BENCH PENDING | #80; Draft #106; PR #151 |
| L3 | Historical Waveshare release | RETIRED / SUPERSEDED EVIDENCE ONLY | #87/#24/#25/#26/#27 closed `not_planned`; PR #20/#57/#67 closed unmerged |
| L4 | Secure OTA | SOFTWARE COMPLETE / PHYSICAL PENDING | #86; PR #152 |
| L5 | Real site source commissioning | TOOLING COMPLETE / SITE EXECUTION PENDING | #81; PR #156 |
| L6 | Production inverter profiles | GENERIC CORE + TOOLING COMPLETE / MODEL QUALIFICATION PENDING | #82; PR #158 |
| L7 | Integrated FAT/endurance/SAT | TOOLING COMPLETE / PHYSICAL RELEASE GATE PENDING | #83; PR #160 |
| L9 | Rev-A custom hardware | NEW CONTROLLED H2 REQUIRED / H4 PENDING | #178/#85/#19 then #162 |
| L10 | Browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 |
| L13 | Promotion graph hygiene | CONTINUOUS | #93 |
| L14 | Live orchestration cadence | CONTINUOUS | #94 |
| L16 | Industrial UI v1 | EXACT SOFTWARE IMAGE FROZEN / PHYSICAL EXECUTION PENDING | #164/#174; PR #179 candidate; evidence PR #181/#183 |

## Current Industrial UI Waveshare candidate

PR #179 is the sole current Waveshare release candidate and remains intentionally Draft/frozen for physical #174:

- source `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact `10293685030` / `industrial-ui-waveshare-800x480-candidate`
- artifact digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256 `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256 `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- ESP-IDF container `espressif/idf:v6.0.1`
- rollback enabled; no compiled STA credentials; no compiled Engineering credential prefill; both bench auth bypasses OFF; minimum native touch target 44 px.

PR #179 exact root/build/package and independent diagnostic build are software-GREEN. Its native UI includes the current Product Core integration, Network scan/select/manual/connect/restart, Grid/Generator 1..3 plus optional Transfer/Sync source commissioning, current alarms/events with filters/sorts/Engineering-only acknowledgement, and fail-closed auth/control behavior. No physical PASS exists yet for this identity.

PR #181 established the complete #174 evidence surface. PR #183 corrected Transfer/ATS and synchronized Grid+Generator to explicit topology-dependent applicability without touching PR #179 firmware. For executed evidence, configured channels require a real round-trip PASS; absent/unsupported channels require roundtrip=false plus a factual non-empty reason. Missing applicability, contradiction or fake optional PASS fails closed.

## Physical release dependency graph

1. **Industrial UI #174:** execute the exact PR #179 candidate on a real Waveshare 800x480 panel. Record visual/touch/roles, Network workflow, Grid/Gen1..3 source mapping, explicit Transfer/Sync applicability, alarm filter/sort/ACK, real Modbus communication, browser/resource health and one uninterrupted >=4 h / >=240-sample run.
2. **Generator #80:** execute the source-transition physical bench; after PASS replay the validated #106 runtime slice to current `dev` and re-earn exact-head CI.
3. **Site #81:** prove real breaker/run/ATS/sync and meter commissioning from authoritative wiring/manual evidence.
4. **Inverters #82:** qualify each deployed exact model/firmware/manual with physical read-only and write/readback/rollback evidence plus signed approval.
5. **OTA #86:** execute real-controller interruption/power-loss/rollback on one exact intended final release image.
6. **Final #83:** execute integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance and signed SAT.
7. **#91/#79:** bind final evidence identity and close only with zero critical blockers.

## Retired historical Waveshare graph

Historical source `87841ecee727fe1d814d4186be8c8c26e4afafb4` retains its own short physical PASS and interrupted ~2 h / 121-sample attempt. The old >=4 h, backend-parity and persistence/ARM gates were never completed. Because this image predates the current Industrial UI and cannot qualify PR #179, issues #87/#24/#25/#26/#27 were closed `not_planned` and PRs #20/#57/#67 were closed unmerged on 2026-09-12. This retirement removes a duplicate active release path; it does not rewrite the old evidence into PASS.

## Production inverter evidence boundary

Current #82 audit has narrowed known targets without unlocking writes: GoodWe GW100K-HT still needs the exact official HT production protocol; Huawei SUN2000-115KTL-M2 still needs an exact official applicable ME-family Modbus definition; Solis `S6-EH3P(80-125)K10-NV-YD-H` family is exact but the available compiled register reference is non-authoritative for production writes; Growatt/Knox/FoxESS exact installed model/protocol identity remains unresolved.

## Rev-A product-hardware track

Historical provider artifact `9909976209` is retained as evidence only. Deterministic replay run `33884657384` returned 20 DRC violations. Exact affected identities are U1 Espressif ESP32-S3-WROOM-1-N8, J2 GCT USB4105-GF-A-120 and J3 CETUS J1B1211CCD. Before any final fabrication, #178/#85 require authoritative component and intended-fabricator evidence, approved narrowly scoped rules committed before a new checkpoint, and fresh ERC/DRC/SI/STEP/provider-package acceptance to a new exact H2 identity. #162 H4 physical prototype qualification follows only after that.

## Operating policy

- Fetch live `dev`, PR heads, issues, CI and physical evidence every orchestration cycle.
- Keep 2–3 independent active CI lanes where meaningful; hardware/site waits never stop independent work.
- Every software merge uses fresh live target, exact head, fresh required CI, `behind_by=0`, and expected-head guard.
- Frozen physical candidates are not rebased/churned merely because `dev` advances.
- Superseded release graphs are retired rather than kept open as duplicate active paths; historical evidence remains immutable.
- Governance revisions state their known parent baseline; they never pretend to know their future merge commit.
- No guessed protocol/register/polarity/timing/topology/hardware-rule evidence and no fabricated physical PASS.
- Project reaches 100% only after all required current-release physical dependencies, promotions, FAT/endurance, signed SAT and final traceability close.
