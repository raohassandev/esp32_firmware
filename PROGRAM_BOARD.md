# AISH-OS v2 Program Board

Authoritative master: Issue #79. Live repository truth overrides this snapshot when they diverge. Software CI never substitutes for required physical acceptance.

Current `dev` snapshot: `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491` after governed PR #148 merge.

| Lane | Scope | State | Primary evidence / dependency |
|---|---|---|---|
| L0/L8 | Governance and live reconciliation | CONTINUOUS / HEALTHY | #79/#84/#91/#93; current reconciliation follows #148 |
| L1 | Modbus TCP connection modes and bounded endpoint admission | COMPLETE/MERGED | PR #99, PR #114; physical endurance #83 |
| L2 | Generator source-transition admission | SOFTWARE GREEN / PHYSICAL BENCH BLOCKED | #80 / Draft PR #106 |
| L3 | Waveshare release | SHORT PHYSICAL PASS / FINAL SOAK INCOMPLETE | #87/#27; exact `87841ece...`; current-dev acceptance tooling PR #142; uninterrupted >=4 h / >=240-sample soak still required |
| L4 | Secure OTA | SOFTWARE COMPLETE/MERGED / PHYSICAL QUALIFICATION BLOCKED | PR #145 current-dev merge; PR #148 always-on regression gate; #86 physical matrix depends on resolved Waveshare release identity |
| L5 | Real site source commissioning | BLOCKED EXTERNAL | #81; actual breaker/run/ATS/sync/polarity/meter evidence |
| L6 | Production inverter profiles | GENERIC CORE HARDENED / MANUFACTURER QUALIFICATION BLOCKED | #82; exact official manuals + bench proof; release gate PR #112 |
| L7 | Integrated FAT/SAT/endurance | PHYSICAL RELEASE GATE PENDING | #83 |
| L9 | Rev-A PCB/KiCad | SEPARATE TRACK | #85 / PR #18/#19 |
| L10 | Served-browser final audit | COMPLETE/CLOSED | #90 |
| L11 | Exact evidence traceability | CONTINUOUS | #91 |
| L12 | Requirements closure audit | COMPLETE/CLOSED | #92 / PR #103 |
| L15 | Historical held Phase-1 reconciliation | SOFTWARE SLICES REPLAYED; PHYSICAL RELEASE GRAPH STILL OPEN | PR #144 superseded #54; PR #145 superseded #52; L3/L4 physical gates remain |

## Governed software merges since the previous governance baseline

- PR #143 — governance reconciliation through PR #142 -> `353c0a50cb0243edd5a73d58313f13623048b273`; full `33769494156` GREEN; 0-behind expected-head merge.
- PR #144 — operator continuity/truthful Plant verdict + Theme menu bridge replayed onto current `dev` -> `8b5fce29aaf0de7ec9a5531ad3ea66c78e4539ed`; head `890f3cd66aa693a214d4edc012770b951307f259`; focused `33771513776`, full `33771513697` GREEN; historical PR #54 closed/superseded. No backend/control authority was added.
- PR #145 — rollback-safe secure OTA replayed onto current `dev` -> `73bcb3e3a57dd482ac87b174906254ad60c8575b`; head `7ab58704c72cf61eca858e8004c12094a0d6bbe3`; Secure OTA `33773071036`, HTTP body ownership `33773071278`, full Firmware/Web/ESP32-S3 `33773071302`, all triggered safety workflows GREEN; 0-behind expected-head merge. Historical PR #52 closed/superseded. This software merge creates no OTA physical PASS.
- PR #148 — always-on secure OTA regression gate for every PR and `dev` push -> `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491`; head `9ed4ec82066ca1687a5a8ce4b4f2cf130281f44a`; always-on `33778878536`, full Firmware/Web/ESP32-S3 `33778878091` GREEN; 0-behind expected-head merge.

Earlier relevant baseline:
- PR #142 — generic Waveshare final-acceptance tooling on current `dev` -> `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2`; focused `33768630723`, full `33768630667` GREEN. This changes tooling/tests only and creates no physical PASS.

## Current Waveshare physical truth

Exact candidate remains `87841ecee727fe1d814d4186be8c8c26e4afafb4` / tree `6ddd7900f9b4ece0fba9349b905e1c078fc3401e` / artifact `9843536218`.

Short physical display/touch/Alarms acceptance is PASS: no recurring sweep/reload/flicker/corruption, Alarms opens, touch remains responsive, zero recorded WDT/panic/NO_MEM, and DMA/resource floor is healthy. The first continuous same-image soak reached about 2 h / 121 one-minute samples plus 25 clean backend rounds, then the USB dock/power path disappeared. This is neither firmware-failure evidence nor a final PASS. A new uninterrupted >=4 h / >=240-sample run is required; partial runs are not additive.

Current `dev` carries deterministic serial acceptance parsing, combined final gate and immutable package identity/checksum/flash-layout verification from PR #142. Those tools validate supplied evidence; they do not replace the required observed hardware run. PR #144/#145/#148 change current `dev` after the frozen physical candidate and therefore inherit none of its physical PASS.

## Remaining release work

1. Exact Waveshare `87841ece...`: uninterrupted >=4 h / >=240-sample same-image soak; then #25 backend parity/recovery and #26 persistence/ARM on the same accepted identity.
2. #80 generator source-transition physical matrix for Draft PR #106; only after PASS replay the identical validated slice onto current `dev` and run fresh CI.
3. #81 real site source mapping and meter sign/scaling provenance.
4. #82 exact official manufacturer profile documentation and physical identity/telemetry/status/write/readback/rollback qualification.
5. #86 secure OTA interruption/power-loss/previous-slot/pending-verification/mark-valid/rollback matrix on one exact intended OTA-enabled release artifact; PR #145 software CI is not this evidence.
6. #83 integrated Grid/DG/mixed-source FAT, all Modbus modes/network endurance, and signed SAT.

## Execution policy

- Maximum 2–3 CI-active software PRs.
- Before every merge: re-fetch live target and exact current PR head, require `behind_by=0` and fresh exact-head required CI, then use expected-head guard.
- If target advances, replay only validated non-overlapping work onto current target; never merge a behind PR.
- Runtime configuration/mapping changes that can invalidate live control assumptions must remove command authority before persistence.
- No full-flash erase, NVS erase, guessed register/polarity/timing/protocol semantics, or production write from an unqualified profile.
- Physical evidence is valid only for its exact source/artifact/config/profile identity.
- Final-acceptance defaults must not be weakened to manufacture a PASS.
- Do not call the project 100% complete until every physical/external release gate above is genuinely satisfied.
