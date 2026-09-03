# AISH-OS Live TODO v2

Master program: #79. Live repository truth overrides stale historical checklists.

## RELEASE-BLOCKING PHYSICAL / EXTERNAL WORK

- [ ] **L3 / #87 — Waveshare final acceptance.** Exact candidate `87841ecee727fe1d814d4186be8c8c26e4afafb4`; short physical gate PASS. Obtain one uninterrupted >=4 h same-image soak (>=240 one-minute samples). First attempt reached ~2 h / 121 clean samples before the USB dock/power path disappeared. Partial runs cannot be combined. After PASS, complete #25 backend parity/recovery and #26 persistence/ARM on the exact accepted identity before promotion.
- [ ] **L2 / #80 — Generator source-transition bench qualification.** Draft PR #106 is software GREEN. Do not merge until Grid<->Generator/Transfer/Island/Sync/conflict/stale/source-loss behavior is physically proven with actual run/breaker/ATS/meter evidence. If `dev` has advanced, replay the identical validated slice only after physical PASS and rerun exact-head CI.
- [ ] **L5 / #81 — real site source evidence.** Record authoritative breaker/run/ATS/synchronism provenance, addresses/contacts, masks, active polarity, meter identity/sign/scaling and topology. No guessed values or kW-sign state inference.
- [ ] **L6 / #82 — production inverter profiles.** Generic safety is hardened/merged, but every manufacturer/model still requires official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback bench evidence and signed production approval.
- [ ] **L4 / #86 — Secure OTA physical qualification.** After the intended Waveshare release baseline is accepted, reconcile PR #52 and physically prove interrupted upload, power-loss handling, previous-slot boot, pending verification, mark-valid and deliberate rollback without NVS/full-flash erase.
- [ ] **L7 / #83 — integrated PV-DG FAT/SAT/endurance.** Execute Grid, DG and mixed-source matrices plus all three Modbus TCP modes, slow/dead slave, exception, reset/reconnect, Wi-Fi recovery, multi-device load, lwIP/socket resource trends and signed SAT tied to exact firmware/config/profile identities.

## CONTINUOUS GOVERNANCE / TRACEABILITY

- [ ] **L8 / #84 — live governance reconciliation.** Keep execution tree, TODO, blocker ledger, program board and evidence index synchronized after every merge or physical verdict.
- [ ] **L11 / #91 — evidence traceability.** Preserve exact SHA/run/artifact/config/profile evidence and keep superseded identities non-authoritative.
- [ ] **L13 / #93 — promotion graph hygiene.** Never merge a behind PR; use current-base replay, exact-tree comparison, fresh CI and expected-head guards.

## HELD SOFTWARE — DO NOT CHURN WHILE PHYSICAL BASELINE IS FROZEN

- [ ] **Draft PR #106** — source-transition runtime candidate, held for #80 bench evidence.
- [ ] **Draft PR #52** — rollback-safe secure OTA, held for #86 and accepted Waveshare baseline.
- [ ] **PR #54** — operator continuity/verdict presentation, held for post-Waveshare reconciliation.
- [ ] **PR #57 / #20 / #67** — frozen Waveshare source/promotion/tooling graph, governed by #87/#27.

## COMPLETED SOFTWARE — DO NOT REOPEN WITHOUT CURRENT REGRESSION EVIDENCE

- [x] AISH-OS v2 governance — PR #97.
- [x] Modbus connection modes / schema migration / diagnostics — PR #99; #88/#78 closed.
- [x] Production build Wi-Fi site-default removal and opt-in provisioning — PR #100.
- [x] Requirements closure audit — #92 / PR #103.
- [x] Final served-browser poller audit — #90 closed.
- [x] Engineering DOM/error stability — PR #105.
- [x] Web spinlock/nonblocking regression — PR #107.
- [x] Inverter reconnect/stale identity revalidation — PR #102.
- [x] Generic inverter command width/scale/range/finite + FC06/FC16 safety — PR #108.
- [x] Modbus endpoint admission closes the synchronous-DNS cumulative-deadline gap — PR #114, merge `fa8ca9b17e08f2478e104942b9d6dbfad4f0ca7f`.
- [x] Persist automatic control disabled before inverter profile assignment persistence — PR #117, merge `1360c4a8356ff8acdc19878f65da311c0b0eccc6`.
- [x] Complete production inverter write-authority gate + fresh ON_GRID positive-command gate — PR #119, merge `d9cd81bcf500a034d6cc88ea92e3bb74e42ed258`.
- [x] Stale replacement PRs #115/#116/#118 closed; they are not evidence sources.
- [x] Waveshare short physical display/touch/Alarms gate on `87841ece...` — PASS; final continuous soak remains open.

## SEPARATE PRODUCT-HARDWARE TRACK

- [ ] **L9 / #85 — Rev-A PCB/KiCad.** PR #18/#19; separate from the current Waveshare firmware release unless the Owner explicitly couples milestones.

## GLOBAL RELEASE GATE

Do not claim 100% until all release-target software is merged through exact-head/zero-behind gates, all required physical/FAT/SAT/OTA gates pass against exact identities, real site source mappings and production inverter profiles are documentation-backed and physically qualified, and no critical blocker remains.
