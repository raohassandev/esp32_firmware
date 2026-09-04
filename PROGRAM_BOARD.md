# AISH-OS v3 Program Board

Authoritative master: #79. Snapshot baseline: `dev` `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134` after governed PR #176. Live repository and observed physical evidence override this board if they diverge.

## Executive board

| Lane | Scope | State | Execution owner / next gate |
|---|---|---|---|
| L0/L8 | Program management/governance | ACTIVE RECONCILIATION | ChatGPT / current 8-file governance PR |
| L1 | Modbus modes/deadlines | SOFTWARE COMPLETE | #83 physical endurance |
| L2 | Generator source transition | SOFTWARE GREEN / BENCH PENDING | #80; Draft #106; PR #151 validator |
| L3 | Historical Waveshare release | SHORT PASS / FINAL SOAK PENDING | #87/#27; PR #159 capture; then #25/#26 |
| L4 | Secure OTA | SOFTWARE COMPLETE / PHYSICAL PENDING | #86; PR #152 validator |
| L5 | Real site source commissioning | TOOLING COMPLETE / SITE EXECUTION PENDING | #81; PR #156 validator |
| L6 | Production inverter profiles | TOOLING + GENERIC CORE COMPLETE / MODEL QUALIFICATION PENDING | #82; PR #158 validator |
| L7 | Integrated FAT/endurance/SAT | TOOLING COMPLETE / PHYSICAL RELEASE GATE PENDING | #83; PR #160 validator |
| L9 | Rev-A custom hardware | HISTORICAL H2 DRC PASS NON-REPRODUCIBLE / NEW CONTROLLED H2 REQUIRED / H4 PENDING | #163 disposition -> new H2 -> #19; then #162 |
| L10 | Browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 |
| L13 | Promotion graph hygiene | CONTINUOUS | #93 |
| L14 | Live orchestration cadence | CONTINUOUS | #94 |
| L16 | Industrial UI v1 | SOFTWARE COMPLETE / EXACT-IMAGE HMI PHYSICAL PENDING | #164/#174; PR #175 validator |

## Governed Industrial UI software chain on `dev`

- PR #165 — authoritative Industrial shell/design system -> `ad4a091c267e9fc11e0903604fee5c8369da2488`.
- PR #167 — actionable Operator workflow -> `8fd6f1988ea32ba08b86872e62d873388abbed8f`.
- PR #169 — Engineering Commission/Configure/Service workspace -> `77e9c9d7046549970dc9bc58bd683304d4f1ced3`.
- PR #172 — legacy nav ownership consolidated under Industrial UI -> `4029a86ac15261a424a77401443680b551c7609f`.
- PR #173 — browser socket/LRU/N16R8 PSRAM resilience gate -> `ada5cc8010183a69e831260b8d8bf36c1bb0dbed`.
- PR #175 — exact-image Industrial UI physical evidence validator -> `9a22d56b9749a7689581e2b8f5e92df3c1e58038`; tooling only, no physical PASS.
- PR #176 — final task-based `Overview / Grid / Solar / Alarms / Readiness` Operator IA -> `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134`; exact-head Industrial UI, physical-tools, poller, OTA, browser-resilience and full Firmware/Web/ESP32-S3 gates GREEN.

## Existing release/evidence chain

- PR #150 — historical Waveshare post-soak parity/persistence validators.
- PR #151 — generator/source-transition physical evidence validator.
- PR #152 — secure OTA physical evidence validator.
- PR #156 — site-source commissioning evidence tooling -> `1c6e1de9ba01c759bc7dc6331f418160614cbbd7`.
- PR #158 — inverter per-model physical production qualification tooling -> `56e2abfb9291b8b5f0786dc8051820a53865984b`.
- PR #159 — historical Waveshare one-command soak capture executor -> `3fd831b677ff590c54cb5cef412a55c9cdea5ca8`.
- PR #160 — integrated FAT/endurance/signed-SAT evidence gate -> `1b4d7631862afdb38da99fbbae9aa170729b0bdb`.
- PR #161 — governance reconciliation -> `72e817140a27f9833d79662a0d9b994e63477906`.

## Physical release dependency graph

1. **Historical Waveshare `87841ece...`**: one new uninterrupted >=4 h / >=240-sample run using PR #159, then #25/#26 on exactly that identity.
2. **Industrial UI #174**: freeze one new immutable Waveshare-capable current UI image and execute native 800x480 visual/touch/role/browser/resource matrix plus >=4 h / >=240-sample same-image endurance using PR #175. Do not inherit step 1 evidence.
3. **Generator #80**: physical source-transition bench; then fresh-current-dev replay/merge of validated #106 runtime behavior.
4. **Site #81**: authoritative real breaker/run/ATS/sync and meter commissioning using PR #156.
5. **Inverters #82**: exact official manufacturer/model/firmware qualification + physical write/readback/rollback + signed approval using PR #158.
6. **OTA #86**: real-controller rollback/interruption matrix on one exact intended release identity using PR #152.
7. **Final #83**: integrated Grid/DG/mixed-source FAT, Modbus/network endurance and signed SAT using PR #160.
8. **#91/#79**: freeze final evidence index, verify zero critical blockers, close release.

## Rev-A product-hardware track

Historical H2 release run `33797012638` produced checkpoint `324e0db1600c2fd883d83f923a0c442669b237f0` and provider package `9909976209` digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`, with a marker claiming ERC/DRC/unconnected/SI/STEP PASS.

PR #163 proved that the historical `DRC=0` result cannot currently be reproduced from that frozen checkpoint. After removing the unsafe post-checkpoint `.kicad_dru` relaxation, head `f020be6bcabc8dec6c05d80aaf00ec47fe6476b4` replayed the original KiCad 10.0.5 upgrade/refill/save semantics. Run `33884657384` passed provenance, ERC, netlist/design-control/HW-interface/power budget, SI, stats and STEP, but failed DRC with 20 violations / 0 unconnected: J2 USB-C internal hole clearance (4), J3 RJ45 edge clearance (2), U1 ESP32 edge clearance (2), and U1 thermal-via drill size (12). Evidence artifact `9941333133`, digest `sha256:668ab99694a0408b673b6b2875d396286bf965dce1a01f1de7de09c8540760b3`.

Do **not** merge #163 as a PASS and do not fabricate with post-checkpoint rules. Next hardware gate is a new controlled H2 acceptance: authoritative component/fabricator evidence for any exceptions, approved rules committed before checkpoint, fresh ERC/DRC/SI/STEP/provider packaging, then a new exact checkpoint/package identity. Historical provider package is retained as evidence only, not final fabrication authority. H4 remains #162.

## Operating policy

- Keep 2–3 independent active lanes where meaningful.
- Hardware/site waits never stop independent software/governance/hardware design work.
- Every merge uses fresh live target, exact head, fresh exact-head required CI, `behind_by=0`, and expected-head guard.
- Frozen physical candidates are not rebased/churned merely because `dev` advances.
- No guessed protocol/register/polarity/timing/topology and no fabricated physical PASS.
- No physical PASS transfers across the historical Waveshare and new Industrial UI identities.
- Do not create low-value patch PRs once a physical gate already has complete automation; execute the gate instead.
- Project reaches 100% only after all physical dependencies, promotions, FAT/endurance, signed SAT and final traceability close.
