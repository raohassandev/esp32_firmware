# AISH-OS Live TODO v2

Master program: #79. Live repository truth overrides stale historical checklists. Current software integration baseline: `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491` after PR #148.

## RELEASE-BLOCKING PHYSICAL / EXTERNAL WORK

- [ ] **L3 / #87 — Waveshare final acceptance.** Exact candidate `87841ecee727fe1d814d4186be8c8c26e4afafb4`; short physical gate PASS. Obtain one uninterrupted >=4 h same-image soak with >=240 one-minute samples. First attempt reached ~2 h / 121 clean samples before USB dock/power loss; partial runs cannot be combined. Current `dev` includes PR #142 final-evidence/package validators. After physical PASS, complete #25 backend parity/recovery and #26 persistence/ARM before promotion.
- [ ] **L2 / #80 — Generator source-transition bench qualification.** Draft PR #106 is software GREEN but physically gated. After physical PASS, replay the identical validated slice onto current `dev`, rerun exact-head CI, and merge only 0-behind.
- [ ] **L5 / #81 — real site source evidence.** Record actual breaker/run/ATS/sync provenance, address/contact, mask, polarity, meter identity/sign/scaling and topology. No guessed values.
- [ ] **L6 / #82 — production inverter profiles.** Each manufacturer/model requires exact official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback bench evidence and signed production approval. Production release remains deliberately blocked with zero approved real profiles.
- [ ] **L4 / #86 — Secure OTA physical qualification.** Software is now merged on current `dev` via PR #145, but physical qualification remains open. After the Waveshare release/source graph is resolved, identify one exact intended OTA-capable release artifact and physically prove authenticated upload, invalid-image rejection before write, interruption/power-loss, previous-slot recovery, pending verification, mark-valid and deliberate rollback without NVS/full-flash erase. Do not transfer physical PASS from `87841ece...` to the OTA-enabled image.
- [ ] **L7 / #83 — integrated PV-DG FAT/SAT/endurance.** Execute Grid, DG, mixed-source, all three Modbus modes, slow/dead peer, exception, reset/reconnect, Wi-Fi recovery, multi-device load, lwIP/socket resource trends and signed SAT tied to exact identities.

## CONTINUOUS GOVERNANCE / TRACEABILITY

- [ ] **L8 / #84 — live governance reconciliation.** Synchronize execution tree, TODO, blocker ledger, program board, evidence index, requirements snapshot and gates after each state change.
- [ ] **L11 / #91 — evidence traceability.** Preserve exact SHA/run/artifact/config/profile evidence and keep stale identities non-authoritative.
- [ ] **L13 / #93 — promotion graph hygiene.** Never merge a behind PR; use current-base replay, fresh exact-head CI and expected-head guards.

## HELD / PHYSICAL-GATED WORK — DO NOT CHURN FROZEN IDENTITIES

- [ ] Draft PR #106 — source-transition runtime candidate; #80 physical gate.
- [ ] PR #57/#20 — frozen Waveshare source/promotion graph; #87/#27.
- [ ] PR #67 — historical frozen-candidate/source-coupled tooling guard; generic reusable final-acceptance/package tooling is independently merged on current `dev` via PR #142.
- [ ] #86 — OTA physical matrix on a future exact intended OTA-capable release identity. Historical PR #52 is closed/superseded; do not merge it.

## COMPLETED SOFTWARE — DO NOT REOPEN WITHOUT CURRENT REGRESSION EVIDENCE

- [x] AISH-OS v2 governance — PR #97.
- [x] Modbus connection modes / schema migration / diagnostics — PR #99.
- [x] Production build Wi-Fi site-default removal / opt-in provisioning — PR #100.
- [x] Inverter identity revalidation — PR #102.
- [x] Requirements closure audit — #92 / PR #103.
- [x] Served-browser poller audit — #90.
- [x] Engineering DOM/error stability — PR #105.
- [x] Web spinlock/nonblocking regression — PR #107.
- [x] Generic inverter command width/scale/range/finite + FC06/FC16 safety — PR #108.
- [x] Production release requires a real production-approved inverter profile — PR #112.
- [x] Pending manufacturer profiles do not guess transport — PR #113.
- [x] Modbus endpoint admission closes cumulative synchronous-DNS deadline gap — PR #114.
- [x] Profile assignment disables control before register-map persistence — PR #117.
- [x] Complete production inverter write authority + fresh mapped ON_GRID gate — PR #119.
- [x] Legacy migration OOM preserves commissioned NVS — PR #122.
- [x] Atomic safety-alarm snapshot — PR #124.
- [x] Generic config/meter/inverter/profile mutations revoke command authority before persistence — PR #127.
- [x] Wi-Fi configuration persistence revokes command authority first — PR #129.
- [x] Source-detection configuration persistence revokes command authority first — PR #130.
- [x] Consolidated commissioning mutation interlock inventory — PR #131.
- [x] Governance reconciliation through PR #131 — PR #132.
- [x] Wi-Fi driver scan-result cleanup on failure/zero-result paths — PR #133, merge `e918aef8465435d4af87eaa6c1f001767a9d2170`, full `33755126372`.
- [x] Schema-6 JSON import/export guarded by the 16-level pre-cJSON depth gate — PR #135, merge `ad6316b5e3dec6bca630d351a80c8e786fd61b69`, focused `33756048306`, full `33756048441`. PR #134 closed stale/superseded.
- [x] Inverter profile assignment no longer puts a whole `app_config_t` snapshot on the HTTP task stack — PR #136, merge `321198bb5d970aa5f4842331a229ce63475e0776`, focused `33756766924`, full `33756767088`.
- [x] Schema-6 init/import/export no longer put whole config snapshots on main/HTTP task stacks — PR #137, merge `43d4bd509ea07aadbd9ea18e9813e3ec11c60297`, focused `33761025944`, full `33761025927`.
- [x] Runtime-component source contract forbids whole automatic `app_config_t` stack frames — PR #138, merge `dd10809f4246713ab99b3ccc9c3b515ece94fd0d`, focused `33762133658`, full `33762133649`.
- [x] Project-wide app-config stack guard covers `components/` + `main/` and declaration escape forms — PR #140, focused `33765114501`, full `33765114492`, merge `093954b5626c034e126fde3b773cedb1add92707`.
- [x] Governance reconciliation through PR #140 — PR #141, full `33766240186`, merge `2272caefa87581f27e815ce4420a5880d2d16e38`.
- [x] Generic Waveshare final acceptance + immutable package validation tooling on current `dev` — PR #142, focused `33768630723`, full `33768630667`, merge `d07dca2d2b20a2cf4e712df45fae9dfe7e3024c2`. Tooling PASS is not physical PASS.
- [x] Governance reconciliation through PR #142 — PR #143, full `33769494156`, merge `353c0a50cb0243edd5a73d58313f13623048b273`.
- [x] Operator continuity + truthful Plant verdict + Theme menu repair on current `dev` — PR #144, head `890f3cd66aa693a214d4edc012770b951307f259`, focused `33771513776`, full `33771513697`, merge `8b5fce29aaf0de7ec9a5531ad3ea66c78e4539ed`. Historical PR #54 closed/superseded.
- [x] Rollback-safe secure web OTA on current `dev` — PR #145, head `7ab58704c72cf61eca858e8004c12094a0d6bbe3`, Secure OTA `33773071036`, HTTP ownership `33773071278`, full `33773071302`, merge `73bcb3e3a57dd482ac87b174906254ad60c8575b`. Historical PR #52 closed/superseded. Physical OTA remains #86.
- [x] Secure OTA contracts are always-on for every PR and `dev` push — PR #148, head `9ed4ec82066ca1687a5a8ce4b4f2cf130281f44a`, always-on `33778878536`, full `33778878091`, merge `1a3b0f4ee73ae08588caee7b46f9ab87d1e5b491`.
- [x] Waveshare short display/touch/Alarms gate on exact `87841ece...` — PASS; final continuous soak remains open.

## SEPARATE PRODUCT-HARDWARE TRACK

- [ ] **L9 / #85 — Rev-A PCB/KiCad.** PR #18/#19; separate unless the Owner explicitly couples milestones.

## GLOBAL RELEASE GATE

Do not claim 100% until all release-target software is governed and merged, every required physical/FAT/SAT/OTA gate passes against exact identities, real site source mappings and production inverter profiles are documentation-backed and physically qualified, and no critical blocker remains. Do not weaken acceptance thresholds to create a PASS.
