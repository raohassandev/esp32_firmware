# AISH-OS Live TODO v3

Master program: #79. Live repository truth overrides stale checklists. Current `dev`: `1b4d7631862afdb38da99fbbae9aa170729b0bdb` after PR #160.

## RELEASE-CRITICAL EXECUTION QUEUE

- [ ] **P0 / L3 / #87/#27 — Waveshare final soak.** Preserve exact `87841ecee727fe1d814d4186be8c8c26e4afafb4`; use the PR #159 fail-closed capture executor; obtain one new uninterrupted >=4 h / >=240-sample run with LCD/touch/Wi-Fi/backend active. Do not combine the prior ~2 h run.
- [ ] **P0 / #25 — backend parity/recovery.** Execute only after genuine Waveshare soak PASS on the same accepted identity; validate with PR #150 tooling.
- [ ] **P0 / #26 — persistence/ARM.** Execute save/readback/reboot/restore/interrupted-save/ARM fail-closed matrix on the same accepted identity after soak PASS; no NVS/full-flash erase.
- [ ] **P0 / L2 / #80 — Generator source-transition physical bench.** Execute full Grid<->Transfer<->Generator/Island/stale/conflict/source-loss/recovery matrix using authoritative breaker/run/ATS/sync and meter sign/scaling evidence. Validate with PR #151. After PASS replay exact #106 runtime slice to latest `dev`, earn fresh CI, merge 0-behind.
- [ ] **P0 / L5 / #81 — Real site source commissioning.** Fill and execute PR #156 evidence package for every required Grid/Gen/ATS/sync channel, exact address/contact/mask/polarity/manual/wiring provenance, meter mapping/sign/scaling, physical toggle, stale and recovery.
- [ ] **P0 / L6 / #82 — Production inverter qualification.** For every deployed model: exact official manual/model/firmware, identity/read-only telemetry/status proof, controlled write/readback/failure/rollback, safe-zero and signed production approval. Validate with PR #158. No guessed map.
- [ ] **P0 / L4 / #86 — Secure OTA physical qualification.** On one immutable intended OTA-capable release identity execute authenticated upload, invalid rejection, interrupted upload, power-loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, rollback and NVS persistence. Validate with PR #152.
- [ ] **P0 / L7 / #83 — Integrated FAT/endurance/SAT.** After prerequisites pass, execute complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded peers/network resets/recovery/resource trends and signed SAT. Validate final record with PR #160 tooling.
- [ ] **P0 / #91 — Final release evidence index.** Bind final SHA/artifact/config/site source maps/approved profiles/FAT/SAT records; zero critical blockers; close master #79 only then.

## ACTIVE MANAGEMENT / GOVERNANCE

- [ ] **L8 / #84 — governance reconciliation.** Current atomic reconciliation branch `docs/governance-reconcile-pr160`; keep all eight governance artifacts synchronized after merges or physical verdicts.
- [ ] **L13 / #93 — promotion graph hygiene.** Exact-head CI + 0-behind + expected-head merge; stale replay instead of historical CI reuse.
- [ ] **L14 / #94 — orchestration cycle.** Maintain 2–3 independent active lanes where meaningful; physical waits never stop independent work.

## REV-A PRODUCT HARDWARE TRACK

- [ ] **L9 / #85 / PR #19 — integrate H2/H3 design checkpoint.** H2 routing/DRC/SI/STEP and H3 provider package already PASS in run `33797012638`; PR #19 exact-head CI is active at `ad7417153d85ba60a440d161385793c21eac4076`.
- [ ] **L9 H4 — fabricate/assemble prototype and execute electrical/communications/relay/enclosure/thermal validation.** Provider package artifact id `9909976209`, digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`.

## COMPLETED SOFTWARE / EVIDENCE AUTOMATION — DO NOT REOPEN WITHOUT CURRENT REGRESSION

- [x] Core runtime/config/UI/Modbus/safety/OTA software through PR #148.
- [x] Waveshare final/package validator PR #142.
- [x] Waveshare post-soak validator PR #150.
- [x] Generator physical evidence validator PR #151.
- [x] Secure OTA physical evidence validator PR #152.
- [x] Site source commissioning validator PR #156 -> `1c6e1de9ba01c759bc7dc6331f418160614cbbd7`.
- [x] Inverter per-model physical production qualification validator PR #158 -> `56e2abfb9291b8b5f0786dc8051820a53865984b`.
- [x] Waveshare one-command physical soak capture executor PR #159 -> `3fd831b677ff590c54cb5cef412a55c9cdea5ca8`.
- [x] Integrated FAT/endurance/signed-SAT validator and runbook PR #160 -> `1b4d7631862afdb38da99fbbae9aa170729b0bdb`.

## GLOBAL DONE GATE

Do not claim 100% until every P0 physical/site/manufacturer/FAT/SAT item above genuinely passes against exact identities, all required promotions are merged with fresh exact-head CI, final evidence is traceable, and no critical blocker remains. CI or validators may enforce evidence quality but cannot create hardware/site PASS.
