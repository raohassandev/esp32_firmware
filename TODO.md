# AISH-OS Live TODO v2

Master program: #79. Live repository truth overrides stale historical checklists.

## EXECUTING NOW

- [ ] **L1 / #88 + #78 — Modbus TCP connection modes (Coder: ChatGPT, QA: GitHub Actions).** Finish schema/NVS-safe migration, meter/inverter config + diagnostics exposure, tests for `per_transaction`, `persistent`, `reconnect_on_error`, full exact-head CI, PR and governed merge. Existing WIP branch `work/modbus/connection-modes` was current before governance PR #97; reconcile it onto live `dev` before opening/merging the final PR.
- [ ] **L3 / #87 — Waveshare final acceptance (Physical: Claude/site operator, Integration: ChatGPT).** Exact candidate `87841ece...`; short physical PASS. Obtain one uninterrupted >=4 h same-image soak (>=240 one-minute samples), then finish #25 backend parity and #26 persistence/ARM matrix before source promotion.
- [ ] **L8 / #84 — continuous governance service (ChatGPT).** Reconcile live state after merges, physical verdicts and blocker changes.

## READY PARALLEL SOFTWARE / AUDIT

- [ ] **L10 / #90 — final served-browser poller audit.** Audit only actually embedded/served assets. PRs #59/#62/#65/#69/#71/#73/#76 are already merged and must not be reopened without regression evidence.
- [ ] **L12 / #92 — requirements closure matrix.** Verify `VERIFY_LIVE` rows against current `dev`; mark already-implemented requirements with exact evidence and open work only for genuine gaps.
- [ ] **L11 / #91 — evidence traceability.** Keep exact SHA/run/artifact evidence and superseded candidate records current.
- [ ] **L13 / #93 — promotion graph hygiene.** Prevent stale/behind PR merges, use expected-head guards and fresh promotion PRs.

## SOFTWARE READY — PHYSICAL/BENCH GATED

- [ ] **L2 / #80 — Generator source-transition FAT.** PR #77 is software GREEN; do not merge until Grid<->Generator/Island/Sync/Transfer/conflict/stale bench matrix passes with actual run/breaker/ATS/meter evidence.
- [ ] **L4 / #86 + #50 — Secure OTA physical qualification.** PR #52 software GREEN; after Waveshare source graph closes, reconcile to intended release baseline and run interruption/power-loss/rollback/mark-valid qualification.
- [ ] **L15 / #95 — post-Waveshare held software reconciliation.** Re-evaluate/replay PR #52 and PR #54 onto final post-Waveshare baseline; do not merge stale branches blindly.

## EXTERNAL EVIDENCE / COMMISSIONING

- [ ] **L5 / #81 — real site source evidence.** Document exact grid/generator/ATS/sync source, register/contact, mask, polarity, meter sign/scaling and topology. No guessed values.
- [ ] **L6 / #82 — production inverter profiles.** Per manufacturer/model manual + identity + telemetry/write/readback/rollback + bench proof + signed production approval.
- [ ] **L7 / #83 — integrated PV-DG FAT/SAT/endurance.** Grid, DG, mixed-source, Modbus TCP PCB/TIME_WAIT, network recovery, fail-closed, signed evidence tied to exact SHA.

## SEPARATE PRODUCT-HARDWARE TRACK

- [ ] **L9 / #85 — Rev-A PCB/KiCad.** PR #18/#19; separate from current Waveshare firmware release unless Owner explicitly couples the milestones.

## COMPLETED — DO NOT REOPEN WITHOUT REGRESSION EVIDENCE

- [x] AISH-OS v2 governance reconciliation — PR #97, merge `430e9157eb82196501f896d9323da16c86f9255e`; #89/#96 complete.
- [x] Generator strong source evidence — PR #58.
- [x] Generator 1–3 persisted configuration/schema migration — PR #63.
- [x] Generator 1–3 runtime/fleet aggregation — PR #64.
- [x] Browser lifecycle batches and active auth/poller owners — PRs #59, #62, #65, #69, #71, #73, #76.
- [x] Dead Engineering wrapper firmware linkage cleanup — PR #75.
- [x] AISH-OS initial control room — PR #60.
- [x] Waveshare short physical display/touch gate on `87841ece...` — PASS; final continuous soak remains open.

## GLOBAL RELEASE GATE

Do not claim 100% until all release-target software is merged through exact-head/zero-behind gates, all required physical/FAT/SAT gates pass against exact identities, production mappings are qualified, and no critical blocker remains.
