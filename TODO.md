# AISH-OS Live TODO v2

Master program: #79. Live repository truth overrides stale historical checklists. Current software integration baseline: `df282a3e8afee27dfc220694e4461e4ad49d2277`.

## RELEASE-BLOCKING PHYSICAL / EXTERNAL WORK

- [ ] **L3 / #87 — Waveshare final acceptance.** Exact candidate `87841ecee727fe1d814d4186be8c8c26e4afafb4`; short physical gate PASS. Obtain one uninterrupted >=4 h same-image soak (>=240 one-minute samples). First attempt reached ~2 h / 121 clean samples before USB dock/power loss. Partial runs cannot be combined. After PASS, complete #25 backend parity/recovery and #26 persistence/ARM before promotion.
- [ ] **L2 / #80 — Generator source-transition bench qualification.** Draft PR #106 is software GREEN but physically gated. After physical PASS, replay the identical validated slice onto current `dev`, rerun exact-head CI, and merge only 0-behind.
- [ ] **L5 / #81 — real site source evidence.** Record actual breaker/run/ATS/sync provenance, address/contact, mask, polarity, meter identity/sign/scaling and topology. No guessed values.
- [ ] **L6 / #82 — production inverter profiles.** Each manufacturer/model requires official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback bench evidence and signed production approval.
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
- [x] Pending manufacturer profiles no longer guess transport semantics — PR #113.
- [x] Modbus endpoint admission closes synchronous-DNS cumulative-deadline gap — PR #114.
- [x] Profile assignment disables running/persisted control before register-map persistence — PR #117.
- [x] Complete production inverter write authority + fresh mapped ON_GRID gate — PR #119.
- [x] Governance reconciliation through PR #119 — PR #120.
- [x] Legacy schema migration OOM cannot replace commissioned NVS; startup fails closed — PR #122, focused `33738503242`, full `33738503251`, merge `dfe93de50e2a5715f4d212ff3233d566d36e2cfd`.
- [x] Safety alarm flags published/read atomically; no transient false all-clear — PR #124, focused `33739241779`, full `33739241807`, merge `3096f2bfa10e86b3163b99ae7622bffded6791ac`.
- [x] Live generic config import, meter mapping, inverter mapping and profile assignment force-disable command authority before persistence — PR #127, focused `33741274303`, full `33741274300`, merge `df282a3e8afee27dfc220694e4461e4ad49d2277`.
- [x] Stale replay PRs #115/#116/#118/#121/#123/#126 closed and non-authoritative.
- [x] Waveshare short display/touch/Alarms gate on exact `87841ece...` — PASS; final continuous soak remains open.

## SEPARATE PRODUCT-HARDWARE TRACK

- [ ] **L9 / #85 — Rev-A PCB/KiCad.** PR #18/#19; separate unless the Owner explicitly couples milestones.

## GLOBAL RELEASE GATE

Do not claim 100% until all release-target software is governed and merged, every required physical/FAT/SAT/OTA gate passes against exact identities, real site source mappings and production inverter profiles are documentation-backed and physically qualified, and no critical blocker remains.
