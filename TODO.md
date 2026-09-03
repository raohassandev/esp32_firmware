# AISH-OS Live TODO v2

Master program: #79. Live repository truth overrides stale historical checklists. Current software integration baseline: `ad651806edb95a749b7d65b61fe1f6b2cf2148db` after PR #152.

## RELEASE-BLOCKING PHYSICAL / EXTERNAL WORK

- [ ] **L3 / #87 — Waveshare final acceptance.** Exact candidate `87841ecee727fe1d814d4186be8c8c26e4afafb4`; short physical gate PASS. Obtain one uninterrupted >=4 h same-image soak with >=240 one-minute samples. First attempt reached ~2 h / 121 clean samples before USB dock/power loss; partial runs cannot be combined. PR #142 supplies final-evidence/package validation. After genuine final-soak PASS, execute #25 backend parity/recovery and #26 persistence/ARM using PR #150 validators before promotion.
- [ ] **L2 / #80 — Generator source-transition bench qualification.** Draft PR #106 is software GREEN but physically gated. Execute Grid->Transfer->Generator, Generator->Transfer->Grid, Island, supported Sync, conflict/stale/source-loss, breaker/ATS/run evidence, meter sign/scaling and recovery dwell. PR #151 provides the fail-closed physical record validator. After PASS, replay the identical validated runtime slice onto current `dev`, rerun exact-head CI, and merge only 0-behind.
- [ ] **L5 / #81 — real site source evidence.** Record actual breaker/run/ATS/sync provenance, address/contact, mask, polarity, meter identity/sign/scaling and topology. No guessed values or kW-sign source authority.
- [ ] **L6 / #82 — production inverter profiles.** Each manufacturer/model requires exact official manual/model/firmware identity, physical identity/telemetry/status proof, write/readback/rollback bench evidence and signed production approval. Production release remains deliberately blocked with zero approved real profiles.
- [ ] **L4 / #86 — Secure OTA physical qualification.** Software is merged via PR #145 and regression-covered via PR #148. PR #152 adds a fail-closed real-controller evidence validator, but physical authenticated upload, invalid-image rejection, interruption/power-loss, previous-slot recovery, pending verification, mark-valid and deliberate rollback remain open on one exact intended OTA-capable release identity. Do not transfer physical PASS from `87841ece...`.
- [ ] **L7 / #83 — integrated PV-DG FAT/SAT/endurance.** Execute Grid, DG, mixed-source, all three Modbus modes, slow/dead peer, exception, reset/reconnect, Wi-Fi recovery, multi-device load, lwIP/socket resource trends and signed SAT tied to exact identities.

## CONTINUOUS GOVERNANCE / TRACEABILITY

- [ ] **L8 / #84 — live governance reconciliation.** Synchronize execution tree, TODO, blocker ledger, program board, evidence index, requirements snapshot, agent registry and gates after each state change.
- [ ] **L11 / #91 — evidence traceability.** Preserve exact SHA/run/artifact/config/profile evidence and keep stale identities non-authoritative.
- [ ] **L13 / #93 — promotion graph hygiene.** Never merge a behind PR; use current-base replay, fresh exact-head CI and expected-head guards.

## HELD / PHYSICAL-GATED WORK — DO NOT CHURN FROZEN IDENTITIES

- [ ] Draft PR #106 — source-transition runtime candidate; #80 physical gate.
- [ ] PR #57/#20 — frozen Waveshare source/promotion graph; #87/#27.
- [ ] PR #67 — historical frozen-candidate/source-coupled tooling guard; generic reusable final-acceptance/package tooling is independently merged via PR #142.
- [ ] #86 — OTA physical matrix on one exact intended OTA-capable release identity; physical record tooling is merged via PR #152.

## COMPLETED SOFTWARE / TOOLING — DO NOT REOPEN WITHOUT CURRENT REGRESSION EVIDENCE

- [x] Core AISH-OS v2 software safety, Modbus modes, UI/browser, config persistence, source evidence framework, inverter write authority and OTA software/regression work through PR #148.
- [x] Governance reconciliation through PR #149 -> `eee505bc3fcb07640836fa79c6becfc629c6050b`.
- [x] Waveshare post-soak backend parity/persistence evidence tooling — PR #150 -> `184d7e658ac44496a4f9efe0fd5db5844ad7fa43`; focused `33792231764`, OTA always-on `33792231776`, full `33792231708`, all GREEN. Tooling creates no physical PASS.
- [x] Generator/source-transition physical evidence tooling — PR #151 -> `892a5811160098a765df7895af943eadf0457d48`; focused `33793093720`, strong-evidence `33793093627`, OTA always-on `33793093852`, full `33793093622`, all GREEN. #80 remains physical.
- [x] Secure OTA real-controller evidence tooling — PR #152 -> `ad651806edb95a749b7d65b61fe1f6b2cf2148db`; focused `33793934963`, secure OTA current-dev `33793934980`, OTA always-on `33793934865`, full `33793934989`, all GREEN. #86 remains physical.
- [x] Waveshare short display/touch/Alarms gate on exact `87841ece...` — PASS; final continuous soak remains open.

## SEPARATE PRODUCT-HARDWARE TRACK

- [ ] **L9 / #85 — Rev-A PCB/KiCad.** PR #18/#19; separate unless the Owner explicitly couples milestones.

## GLOBAL RELEASE GATE

Do not claim 100% until all release-target software is governed and merged, every required physical/FAT/SAT/OTA gate passes against exact identities, real site source mappings and production inverter profiles are documentation-backed and physically qualified, and no critical blocker remains. Evidence validators cannot substitute for observation and acceptance thresholds must not be weakened to create a PASS.
