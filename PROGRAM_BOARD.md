# AISH-OS v2 Program Board

Authoritative master: #79. Snapshot baseline: `dev` `1b4d7631862afdb38da99fbbae9aa170729b0bdb` after PR #160. Live repository and observed physical evidence override this board if they diverge.

## Executive board

| Lane | Scope | State | Execution owner / next gate |
|---|---|---|---|
| L0/L8 | Program management/governance | ACTIVE RECONCILIATION | ChatGPT / governance PR after #160 |
| L1 | Modbus modes/deadlines | SOFTWARE COMPLETE | #83 physical endurance |
| L2 | Generator source transition | SOFTWARE GREEN / BENCH PENDING | #80; Draft #106; PR #151 validator |
| L3 | Waveshare release | SHORT PASS / FINAL SOAK PENDING | #87/#27; PR #159 capture; then #25/#26 |
| L4 | Secure OTA | SOFTWARE COMPLETE / PHYSICAL PENDING | #86; PR #152 validator |
| L5 | Real site source commissioning | TOOLING COMPLETE / SITE EXECUTION PENDING | #81; PR #156 validator |
| L6 | Production inverter profiles | TOOLING + GENERIC CORE COMPLETE / MODEL QUALIFICATION PENDING | #82; PR #158 validator |
| L7 | Integrated FAT/endurance/SAT | TOOLING COMPLETE / PHYSICAL RELEASE GATE PENDING | #83; PR #160 validator |
| L9 | Rev-A custom hardware | H2/H3 AUTOMATED PASS / PR #19 CI ACTIVE / H4 PENDING | #85; prototype fabrication + physical validation |
| L10 | Browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 |
| L13 | Promotion graph hygiene | CONTINUOUS | #93 |
| L14 | Live orchestration cadence | CONTINUOUS | #94 |

## Governed software/evidence chain now on `dev`

- PR #150 — Waveshare post-soak parity/persistence validators -> `184d7e658ac44496a4f9efe0fd5db5844ad7fa43`.
- PR #151 — generator/source-transition physical evidence validator -> `892a5811160098a765df7895af943eadf0457d48`.
- PR #152 — secure OTA physical evidence validator -> `ad651806edb95a749b7d65b61fe1f6b2cf2148db`.
- PR #153 — governance reconciliation -> `df815696fb201f36d46846e4efac0740274884a8`.
- PR #156 — site-source commissioning evidence tooling -> `1c6e1de9ba01c759bc7dc6331f418160614cbbd7`; focused `33796735953`, OTA `33796735766`, full `33796735830` GREEN.
- PR #158 — inverter per-model physical production qualification tooling -> `56e2abfb9291b8b5f0786dc8051820a53865984b`; focused `33797589153`, OTA `33797588865`, full `33797588858` GREEN.
- PR #159 — Waveshare one-command soak capture executor -> `3fd831b677ff590c54cb5cef412a55c9cdea5ca8`; focused `33798050886`, OTA `33798050815`, full `33798050594` GREEN.
- PR #160 — integrated FAT/endurance/signed-SAT evidence gate -> `1b4d7631862afdb38da99fbbae9aa170729b0bdb`; focused `33829562747`, OTA `33829562751`, full `33829562735` GREEN.

## Physical release dependency graph

1. **Waveshare exact candidate `87841ece...`**: one new uninterrupted >=4 h / >=240-sample run using PR #159 capture executor.
2. **Same Waveshare identity**: #25 backend parity/recovery and #26 persistence/ARM.
3. **Generator #80**: physical source-transition bench; then fresh-current-dev replay/merge of validated #106 runtime behavior.
4. **Site #81**: authoritative real breaker/run/ATS/sync and meter commissioning using PR #156.
5. **Inverters #82**: exact official manufacturer/model/firmware qualification + physical write/readback/rollback + signed approval using PR #158.
6. **OTA #86**: real-controller rollback/interruption matrix on one exact intended release identity using PR #152.
7. **Final #83**: integrated Grid/DG/mixed-source FAT, Modbus/network endurance and signed SAT using PR #160.
8. **#91/#79**: freeze final evidence index, verify zero critical blockers, close release.

## Rev-A product-hardware track

KiCad H2 release run `33797012638` completed GREEN: ERC=0, DRC=0, UNCONNECTED=0, L2 ground and critical-route SI geometry PASS, STEP/mechanical/manufacturing export PASS, routed native checkpoint `324e0db1600c2fd883d83f923a0c442669b237f0`, provider package artifact id `9909976209` digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`, and engineering artifact id `9909977211` digest `sha256:246830e56b8a17be3a0057186e7e30102c5e5dd371fb3bf4e64c2279502e8ea7`. PR #19 exact head `ad7417153d85ba60a440d161385793c21eac4076` is earning fresh CI before integration. H4 fabricated-prototype validation remains physical.

## Operating policy

- Keep 2–3 independent active lanes where meaningful.
- Hardware/site waits never stop independent software/governance/hardware design work.
- Every merge uses fresh live target, exact head, fresh exact-head required CI, `behind_by=0`, and expected-head guard.
- Frozen physical candidates are not rebased/churned merely because `dev` advances.
- No guessed protocol/register/polarity/timing/topology and no fabricated physical PASS.
- Do not create low-value patch PRs once a physical gate already has complete automation; execute the gate instead.
- Project reaches 100% only after all physical dependencies, promotions, FAT/endurance, signed SAT and final traceability close.
