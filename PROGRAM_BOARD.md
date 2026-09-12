# AISH-OS v5 Program Board

Authoritative master: #79. **Reconciliation parent:** `dev` `8016c005be8d548e9388026a89592b70b92ec1a3` after PR #182. Live repository and genuine physical evidence override this board. This board intentionally records the known parent before its own revision rather than claiming an unknowable future merge SHA.

## Executive board

| Lane | Scope | State | Execution owner / next gate |
|---|---|---|---|
| L0/L8 | Program management/governance | CONTINUOUS / PARENT-SEMANTICS RECONCILED | ChatGPT / #84 |
| L1 | Modbus modes/deadlines | SOFTWARE COMPLETE | #83 physical endurance |
| L2 | Generator source transition | SOFTWARE GREEN / BENCH PENDING | #80; Draft #106; PR #151 |
| L3 | Historical Waveshare release | SHORT PASS / FINAL SOAK PENDING | #87/#27; then #25/#26 |
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

PR #179 is intentionally Draft and frozen for physical #174:

- source `72a1a82a8fc5ad4406b5bd51fba1f80f9c182884`
- tree `3069c65b4234fcd2b6418f9bbbe7859f1cd9abce`
- artifact `10293685030` / `industrial-ui-waveshare-800x480-candidate`
- artifact digest `sha256:44dc05fe2c6e61d3a8b5fdfc7c936937da948691a2038358d5c0b3c1008de541`
- application SHA256 `0bbdb75be4ea7c0337f07e83dbdd3e34736ce8f667a42aa11abeb5c638f60734`
- UF2 SHA256 `199feec247563130f800d25e2a7024ebbaf64a65d3d8b6c6c5c9ec4c242b2500`
- ESP-IDF container `espressif/idf:v6.0.1`
- rollback enabled; no compiled STA credentials; no compiled Engineering credential prefill; both bench auth bypasses OFF; minimum native touch target 44 px.

PR #179 exact root/build/package and independent diagnostic build are software-GREEN. Its native UI includes the current Product Core integration, Network scan/select/manual/connect/restart, Grid/Generator 1..3 plus optional Transfer/Sync source commissioning, current alarms/events with filters/sorts/Engineering-only acknowledgement, and fail-closed auth/control behavior. No physical PASS exists yet for this identity.

PR #181 established the complete v2 #174 evidence surface. PR #183 corrects one evidence-contract mismatch without touching PR #179: Transfer/ATS and synchronized Grid+Generator are now explicitly topology-dependent. For executed evidence, each optional surface must declare applicability; configured channels require a real round-trip PASS, while absent/unsupported channels require roundtrip=false plus a factual non-empty reason. Missing applicability, contradiction or fake optional PASS fails closed.

## Physical release dependency graph

1. **Industrial UI #174:** execute the exact PR #179 candidate on a real Waveshare 800x480 panel. Record visual/touch/roles, Network workflow, Grid/Gen1..3 source mapping, explicit Transfer/Sync applicability, alarm filter/sort/ACK, real Modbus communication, browser/resource health and one uninterrupted >=4 h / >=240-sample run.
2. **Generator #80:** execute the source-transition physical bench; after PASS replay the validated #106 runtime slice to current `dev` and re-earn exact-head CI.
3. **Site #81:** prove real breaker/run/ATS/sync and meter commissioning from authoritative wiring/manual evidence.
4. **Inverters #82:** qualify each deployed exact model/firmware/manual with physical read-only and write/readback/rollback evidence plus signed approval.
5. **OTA #86:** execute real-controller interruption/power-loss/rollback on one exact intended final release image.
6. **Final #83:** execute integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance and signed SAT.
7. **#91/#79:** bind final evidence identity and close only with zero critical blockers.

The historical `87841ece...` Waveshare lane (#87/#27/#25/#26) remains separate. Its prior short PASS and interrupted ~2 h soak never transfer to PR #179.

## Rev-A product-hardware track

Historical provider artifact `9909976209` is retained as evidence only. Deterministic replay run `33884657384` could not reproduce historical `DRC=0` and returned 20 DRC violations. Before any final fabrication, #178/#85 require authoritative component/fabricator evidence for necessary exceptions, approved narrowly scoped rules committed before a new checkpoint, and fresh ERC/DRC/SI/STEP/provider-package acceptance to a new exact H2 identity. #162 H4 physical prototype qualification follows only after that.

## Operating policy

- Fetch live `dev`, PR heads, issues, CI and physical evidence every orchestration cycle.
- Keep 2–3 independent active CI lanes where meaningful; hardware/site waits never stop independent work.
- Every software merge uses fresh live target, exact head, fresh required CI, `behind_by=0`, and expected-head guard.
- Frozen physical candidates are not rebased/churned merely because `dev` advances.
- Governance revisions state their known parent baseline; they never pretend to know their future merge commit.
- No guessed protocol/register/polarity/timing/topology/hardware-rule evidence and no fabricated physical PASS.
- Project reaches 100% only after all required physical dependencies, promotions, FAT/endurance, signed SAT and final traceability close.
