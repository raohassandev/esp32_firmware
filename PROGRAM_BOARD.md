# AISH-OS v2 Program Board

Authoritative master: Issue #79. Live repository truth overrides this snapshot when they diverge. Software CI never substitutes for required physical acceptance.

Current `dev` snapshot: `093954b5626c034e126fde3b773cedb1add92707` after governed PR #140 merge.

| Lane | Scope | State | Primary evidence / dependency |
|---|---|---|---|
| L0/L8 | Governance and live reconciliation | CONTINUOUS / HEALTHY | #79/#84/#91/#93; fresh reconciliation follows #140 |
| L1 | Modbus TCP connection modes and bounded endpoint admission | COMPLETE/MERGED | PR #99, PR #114; physical endurance #83 |
| L2 | Generator source-transition admission | SOFTWARE GREEN / PHYSICAL BENCH BLOCKED | #80 / Draft PR #106 |
| L3 | Waveshare release | SHORT PHYSICAL PASS / FINAL SOAK INCOMPLETE | #87/#27; exact `87841ece...`; uninterrupted >=4 h / >=240-sample soak still required |
| L4 | Secure OTA | SOFTWARE GREEN / BASELINE + PHYSICAL BLOCKED | #86/#50 / Draft PR #52; depends on accepted Waveshare graph |
| L5 | Real site source commissioning | BLOCKED EXTERNAL | #81; actual breaker/run/ATS/sync/polarity/meter evidence |
| L6 | Production inverter profiles | GENERIC CORE HARDENED / MANUFACTURER QUALIFICATION BLOCKED | #82; exact official manuals + bench proof; release gate PR #112 |
| L7 | Integrated FAT/SAT/endurance | PHYSICAL RELEASE GATE PENDING | #83 |
| L9 | Rev-A PCB/KiCad | SEPARATE TRACK | #85 / PR #18/#19 |
| L10 | Served-browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Exact evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 / PR #103 |
| L15 | Held Phase-1 reconciliation | BLOCKED BY L3 | #95 / PR #52/#54 |

## Governed software merges since the last governance snapshot

- PR #132 — governance reconciliation through PR #131 -> `2e0c946d30027419dfbd0723598ac34315cf6a86`.
- PR #133 — Wi-Fi scan result lifetime cleanup on failure paths -> `e918aef8465435d4af87eaa6c1f001767a9d2170`; full `33755126372` GREEN.
- PR #135 — schema-6 JSON depth guard replay -> `ad6316b5e3dec6bca630d351a80c8e786fd61b69`; focused `33756048306`, full `33756048441` GREEN. PR #134 was closed stale/superseded.
- PR #136 — inverter profile assignment config snapshot moved off task stack -> `321198bb5d970aa5f4842331a229ce63475e0776`; focused `33756766924`, full `33756767088` GREEN.
- PR #137 — schema-6 init/import/export full config snapshots moved off task stacks -> `43d4bd509ea07aadbd9ea18e9813e3ec11c60297`; focused `33761025944`, full `33761025927` GREEN.
- PR #138 — runtime-component regression contract forbidding whole automatic `app_config_t` stack frames -> `dd10809f4246713ab99b3ccc9c3b515ece94fd0d`; focused `33762133658`, full `33762133649` GREEN.
- PR #140 — completed the app-config source contract across `components/` and `main/`, added `main/**/*.c` workflow ownership, and closed array/multi-declarator/qualifier escape forms -> `093954b5626c034e126fde3b773cedb1add92707`; focused `33765114501`, full `33765114492` GREEN.
- PR #139 was intentionally closed unmerged after #140 advanced `dev`; its governance slice is being replayed here on the current base.

## Current Waveshare physical truth

Exact candidate remains `87841ecee727fe1d814d4186be8c8c26e4afafb4` / tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e` / artifact `9843536218`.

Short physical display/touch/Alarms acceptance is PASS: no recurring sweep/reload/flicker/corruption, Alarms opens, touch remains responsive, zero recorded WDT/panic/NO_MEM, and DMA/resource floor is healthy. The first continuous same-image soak reached about 2 h / 121 one-minute samples plus 25 clean backend rounds, then the USB dock/power path disappeared. This is neither firmware-failure evidence nor a final PASS. A new uninterrupted >=4 h / >=240-sample run is required; partial runs are not additive.

## Remaining release work

1. Exact Waveshare `87841ece...`: uninterrupted >=4 h / >=240-sample same-image soak; then #25 backend parity/recovery and #26 persistence/ARM on the same accepted identity.
2. #80 generator source-transition physical matrix for Draft PR #106; only after PASS replay the identical validated slice onto current `dev` and run fresh CI.
3. #81 real site source mapping and meter sign/scaling provenance.
4. #82 exact official manufacturer profile documentation and physical identity/telemetry/status/write/readback/rollback qualification.
5. #86 OTA interruption/power-loss/rollback qualification on the intended accepted baseline.
6. #83 integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance, and signed SAT.

## Execution policy

- Maximum 2–3 CI-active software PRs.
- Before every merge: re-fetch live target and exact current PR head, require `behind_by=0` and fresh exact-head required CI, then use expected-head guard.
- If target advances, replay only validated non-overlapping work onto current target; never merge a behind PR.
- Runtime configuration/mapping changes that can invalidate live control assumptions must remove command authority before persistence.
- No full-flash erase, NVS erase, guessed register/polarity/timing/protocol semantics, or production write from an unqualified profile.
- Physical evidence is valid only for its exact source/artifact/config/profile identity.
- Do not call the project 100% complete until every physical/external release gate above is genuinely satisfied.
