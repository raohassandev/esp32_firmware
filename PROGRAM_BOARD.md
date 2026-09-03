# AISH-OS v2 Program Board

Authoritative master: Issue #79. Live repository truth overrides this snapshot when they diverge. Software CI and evidence tooling never substitute for required physical acceptance.

Current `dev` snapshot: `ad651806edb95a749b7d65b61fe1f6b2cf2148db` after governed PR #152 merge.

| Lane | Scope | State | Primary evidence / dependency |
|---|---|---|---|
| L0/L8 | Governance and live reconciliation | CONTINUOUS / HEALTHY | #79/#84/#91/#93; reconciliation follows #152 |
| L1 | Modbus TCP connection modes and bounded endpoint admission | COMPLETE/MERGED | PR #99, PR #114; physical endurance #83 |
| L2 | Generator source-transition admission | SOFTWARE GREEN / PHYSICAL BENCH BLOCKED | #80 / Draft PR #106; physical validator PR #151 |
| L3 | Waveshare release | SHORT PHYSICAL PASS / FINAL SOAK INCOMPLETE | #87/#27; exact `87841ece...`; final tooling PR #142; post-soak tooling PR #150 |
| L4 | Secure OTA | SOFTWARE COMPLETE/MERGED / PHYSICAL QUALIFICATION BLOCKED | PR #145 + #148; physical validator PR #152; #86 still requires real-controller execution |
| L5 | Real site source commissioning | BLOCKED EXTERNAL | #81; actual breaker/run/ATS/sync/polarity/meter evidence |
| L6 | Production inverter profiles | GENERIC CORE HARDENED / MANUFACTURER QUALIFICATION BLOCKED | #82; exact official manuals + bench proof; release gate PR #112 |
| L7 | Integrated FAT/SAT/endurance | PHYSICAL RELEASE GATE PENDING | #83 |
| L9 | Rev-A PCB/KiCad | SEPARATE TRACK | #85 / PR #18/#19 |
| L10 | Served-browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Exact evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 / PR #103 |
| L15 | Historical held Phase-1 reconciliation | SOFTWARE SLICES REPLAYED; PHYSICAL RELEASE GRAPH STILL OPEN | PR #144 superseded #54; PR #145 superseded #52 |

## Governed software merges since the previous governance baseline

- PR #149 — eight-artifact governance reconciliation -> `eee505bc3fcb07640836fa79c6becfc629c6050b`; head `ed48bd91ec3c6b669be0f0d33ca50e129be735fc`; always-on OTA `33779699849`, full Firmware/Web/ESP32-S3 `33779699784`; 0-behind expected-head merge.
- PR #150 — fail-closed Waveshare post-soak evidence validators -> `184d7e658ac44496a4f9efe0fd5db5844ad7fa43`; head `93b798e5180df04d6587ded7e1aee0fa3a672e3b`; post-soak tools `33792231764`, OTA always-on `33792231776`, full `33792231708`, all GREEN. No physical PASS created.
- PR #151 — fail-closed generator/source-transition physical evidence validator -> `892a5811160098a765df7895af943eadf0457d48`; head `d3a70ec8dad791e6a5fd00ff65e9b351455db184`; generator evidence `33793093720`, generator strong-evidence `33793093627`, OTA always-on `33793093852`, full `33793093622`, all GREEN. #80 remains physically open.
- PR #152 — fail-closed secure OTA real-controller evidence validator -> `ad651806edb95a749b7d65b61fe1f6b2cf2148db`; head `294ed08a8f703d33151fb2ec38ca76da20f6aa54`; OTA physical evidence `33793934963`, secure OTA current-dev `33793934980`, OTA always-on `33793934865`, full Firmware/Web/ESP32-S3 `33793934989`, all GREEN; 0-behind expected-head merge. #86 remains physically open.

## Current Waveshare physical truth

Exact candidate remains `87841ecee727fe1d814d4186be8c8c26e4afafb4` / tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e` / artifact `9843536218`.

Short physical display/touch/Alarms acceptance is PASS: no recurring sweep/reload/flicker/corruption, Alarms opens, touch remains responsive, zero recorded WDT/panic/NO_MEM, and DMA/resource floor is healthy. The first continuous same-image soak reached about 2 h / 121 one-minute samples plus 25 clean backend rounds, then the USB dock/power path disappeared. This is neither firmware-failure evidence nor a final PASS. A new uninterrupted >=4 h / >=240-sample run is required; partial runs are not additive.

Current `dev` includes generic final-evidence/package tooling from PR #142 and post-soak backend-parity/persistence validators from PR #150. These validate supplied evidence only; they do not replace the required observed hardware run.

## Remaining release work

1. Exact Waveshare `87841ece...`: one uninterrupted >=4 h / >=240-sample same-image soak; then #25 backend parity/recovery and #26 persistence/ARM on the same accepted identity using PR #150 tooling.
2. #80 generator source-transition physical matrix for Draft PR #106 using PR #151 validator; only after genuine PASS replay the identical validated runtime slice onto current `dev` and run fresh CI.
3. #81 real site source mapping and meter sign/scaling provenance from actual manuals/wiring/site observations.
4. #82 exact official manufacturer profile documentation and physical identity/telemetry/status/write/readback/rollback qualification. Public GoodWe GW100K-HT material confirms Modbus-RTU/SunSpec but not the exact HT control map; no production write profile is approved from incomplete documentation. Huawei SUN2000-115KTL-M2 exact applicable Modbus control documentation remains required.
5. #86 secure OTA physical interruption/power-loss/previous-slot/pending-verification/mark-valid/rollback matrix on one exact intended OTA-enabled release artifact, validated with PR #152 tooling.
6. #83 integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance, and signed SAT.

## Execution policy

- Maximum 2–3 CI-active software PRs.
- Before every merge: re-fetch live target and exact current PR head, require `behind_by=0` and fresh exact-head required CI, then use expected-head guard.
- If target advances, replay only validated non-overlapping work onto current target; never merge a behind PR.
- Runtime configuration/mapping changes that can invalidate live control assumptions must remove command authority before persistence.
- No full-flash erase, NVS erase, guessed register/polarity/timing/protocol semantics, or production write from an unqualified profile.
- Physical evidence is valid only for its exact source/artifact/config/profile identity.
- Evidence validators may reject incomplete records but cannot manufacture or transfer a physical PASS.
- Final-acceptance defaults must not be weakened to manufacture a PASS.
- Do not call the project 100% complete until every physical/external release gate above is genuinely satisfied.
