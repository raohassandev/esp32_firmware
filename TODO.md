# AISH-OS Live TODO v2

Master program: #79. Live repository truth overrides stale historical checklists. Current software integration baseline: `dd10809f4246713ab99b3ccc9c3b515ece94fd0d` after PR #138.

## RELEASE-BLOCKING PHYSICAL / EXTERNAL WORK

- [ ] **L3 / #87 — Waveshare final acceptance.** Exact candidate `87841ecee727fe1d814d4186be8c8c26e4afafb4`; short physical gate PASS. Obtain one uninterrupted >=4 h same-image soak with >=240 one-minute samples. First attempt reached ~2 h / 121 clean samples before USB dock/power loss; partial runs cannot be combined. After PASS, complete #25 backend parity/recovery and #26 persistence/ARM before promotion.
- [ ] **L2 / #80 — Generator source-transition bench qualification.** Draft PR #106 is software GREEN but physically gated. After physical PASS, replay the identical validated slice onto current `dev`, rerun exact-head CI, and merge only 0-behind.
- [ ] **L5 / #81 — real site source evidence.** Record actual breaker/run/ATS/sync provenance, address/contact, mask, polarity, meter identity/sign/scaling and topology. No guessed values.
- [ ] **L6 / #82 — production inverter profiles.** Each manufacturer/model requires exact official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback bench evidence and signed production approval. Production release remains deliberately blocked with zero approved real profiles.
- [ ] **L4 / #86 — Secure OTA physical qualification.** After accepted Waveshare baseline, reconcile PR #52 and physically prove interruption/power-loss/previous-slot/pending-verification/mark-valid/rollback without NVS/full-flash erase.
- [ ] **L7 / #83 — integrated PV-DG FAT/SAT/endurance.** Execute Grid, DG, mixed-source, all three Modbus modes, slow/dead peer, exception, reset/reconnect, Wi-Fi recovery, multi-device load, lwIP/socket resource trends and signed SAT tied to exact identities.

## CONTINUOUS GOVERNANCE / TRACEABILITY

- [ ] **L8 / #84 — live governance reconciliation.** Synchronize execution tree, TODO, blocker ledger, program board, evidence index and requirements snapshot after each state change.
- [ ] **L11 / #91 — evidence traceability.** Preserve exact SHA/run/artifact/config/profile evidence and keep stale identities non-authoritative.
- [ ] **L13 / #93 — promotion graph hygiene.** Never merge a behind PR; use current-base replay, fresh exact-head CI and expected-head guards.

## HELD SOFTWARE — DO NOT CHURN WHILE PHYSICAL BASELINE IS FROZEN

- [ ] Draft PR #106 — source-transition runtime candidate; #80 physical gate.
- [ ] Draft PR #52 — rollback-safe secure OTA; #86 + accepted Waveshare baseline.
- [ ] PR #54 — operator continuity/verdict presentation; post-Waveshare reconciliation.
- [ ] PR #57/#20/#67 — frozen Waveshare source/promotion/tooling graph; #87/#27.

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
- [x] Project-wide source contract forbids whole automatic `app_config_t` runtime stack frames — PR #138, merge `dd10809f4246713ab99b3ccc9c3b515ece94fd0d`, focused `33762133658`, full `33762133649`.
- [x] Waveshare short display/touch/Alarms gate on exact `87841ece...` — PASS; final continuous soak remains open.

## SEPARATE PRODUCT-HARDWARE TRACK

- [ ] **L9 / #85 — Rev-A PCB/KiCad.** PR #18/#19; separate unless the Owner explicitly couples milestones.

## GLOBAL RELEASE GATE

Do not claim 100% until all release-target software is governed and merged, every required physical/FAT/SAT/OTA gate passes against exact identities, real site source mappings and production inverter profiles are documentation-backed and physically qualified, and no critical blocker remains.
