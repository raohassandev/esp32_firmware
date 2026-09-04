# AISH-OS Live TODO v4

Master program: #79. Live repository truth overrides stale checklists. Current `dev`: `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134` after governed PR #176.

## RELEASE-CRITICAL EXECUTION QUEUE

- [ ] **P0 / L3 / #87/#27 — historical Waveshare final soak.** Preserve exact `87841ecee727fe1d814d4186be8c8c26e4afafb4`; use the PR #159 fail-closed capture executor; obtain one new uninterrupted >=4 h / >=240-sample run with LCD/touch/Wi-Fi/backend active. Do not combine the prior ~2 h run and do not transfer this candidate's PASS to newer UI source.
- [ ] **P0 / #25 — backend parity/recovery.** Execute only after genuine historical Waveshare soak PASS on the same accepted identity; validate with PR #150 tooling.
- [ ] **P0 / #26 — persistence/ARM.** Execute save/readback/reboot/restore/interrupted-save/ARM fail-closed matrix on that same historical identity after soak PASS; no NVS/full-flash erase.
- [ ] **P0 / L16 / #164/#174 — Industrial UI v1 exact-image HMI acceptance.** Software is complete on `dev` through PR #176. Select one immutable new Waveshare-capable Industrial UI release image and execute #174 native 800x480 visual/touch/role/browser/resource matrix plus one uninterrupted >=4 h / >=240-sample same-image run. Validate records with PR #175. Historical #87/#27 physical evidence does not transfer.
- [ ] **P0 / L2 / #80 — Generator source-transition physical bench.** Execute full Grid<->Transfer<->Generator/Island/stale/conflict/source-loss/recovery matrix using authoritative breaker/run/ATS/sync and meter sign/scaling evidence. Validate with PR #151. After PASS replay exact #106 runtime slice to latest `dev`, earn fresh CI, merge 0-behind.
- [ ] **P0 / L5 / #81 — Real site source commissioning.** Fill and execute PR #156 evidence package for every required Grid/Gen/ATS/sync channel, exact address/contact/mask/polarity/manual/wiring provenance, meter mapping/sign/scaling, physical toggle, stale and recovery.
- [ ] **P0 / L6 / #82 — Production inverter qualification.** For every deployed model: exact official manual/model/firmware, identity/read-only telemetry/status proof, controlled write/readback/failure/rollback, safe-zero and signed production approval. Validate with PR #158. No guessed map.
- [ ] **P0 / L4 / #86 — Secure OTA physical qualification.** On one immutable intended OTA-capable release identity execute authenticated upload, invalid rejection, interrupted upload, power-loss, partial-image non-selection, previous-slot boot, pending verification, mark-valid, rollback and NVS persistence. Validate with PR #152.
- [ ] **P0 / L7 / #83 — Integrated FAT/endurance/SAT.** After prerequisites pass, execute complete Grid/DG/mixed-source FAT, all three Modbus modes, degraded peers/network resets/recovery/resource trends and signed SAT. Validate final record with PR #160 tooling.
- [ ] **P0 / #91 — Final release evidence index.** Bind final SHA/artifact/config/site source maps/approved profiles/UI physical record/FAT/SAT records; zero critical blockers; close master #79 only then.

## ACTIVE MANAGEMENT / GOVERNANCE

- [ ] **L8 / #84 — governance reconciliation.** Keep all eight authoritative governance artifacts synchronized after merges or physical verdicts. Current reconciliation branch: `docs/governance-reconcile-industrial-ui-v1`.
- [ ] **L13 / #93 — promotion graph hygiene.** Exact-head CI + 0-behind + expected-head merge; stale replay instead of historical CI reuse.
- [ ] **L14 / #94 — orchestration cycle.** Maintain 2–3 independent active lanes where meaningful; physical waits never stop independent work.

## REV-A PRODUCT HARDWARE TRACK

- [ ] **L9 / #85 / PR #163 -> #19 — fix deterministic H2 PR validation.** PR #163 correctly froze the accepted H2 checkpoint but its current CI rejects post-checkpoint `hardware/kicad/Automatrix_PVDG_RevA.kicad_dru`; disposition this rules-file provenance without weakening routed H2 identity, then re-earn exact-head KiCad CI and advance parent #19.
- [ ] **L9 H4 / #162 — fabricate/assemble prototype and execute electrical/communications/relay/enclosure/thermal validation.** Provider package artifact id `9909976209`, digest `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`.

## COMPLETED SOFTWARE / EVIDENCE AUTOMATION — DO NOT REOPEN WITHOUT CURRENT REGRESSION

- [x] Core runtime/config/Modbus/safety/OTA software through PR #148.
- [x] AISH-OS governance reconciliation PR #161 -> `72e817140a27f9833d79662a0d9b994e63477906`.
- [x] Industrial UI shell/design system PR #165 -> `ad4a091c267e9fc11e0903604fee5c8369da2488`.
- [x] Industrial operator actionable workflow PR #167 -> `8fd6f1988ea32ba08b86872e62d873388abbed8f`.
- [x] Industrial Engineering Commission/Configure/Service workspace PR #169 -> `77e9c9d7046549970dc9bc58bd683304d4f1ced3`.
- [x] Single Industrial UI navigation ownership PR #172 -> `4029a86ac15261a424a77401443680b551c7609f`.
- [x] Browser socket/LRU/N16R8 PSRAM resilience gate PR #173 -> `ada5cc8010183a69e831260b8d8bf36c1bb0dbed`.
- [x] Industrial UI exact-image physical evidence tooling PR #175 -> `9a22d56b9749a7689581e2b8f5e92df3c1e58038`.
- [x] Final task-based Operator IA PR #176 -> `14d13a0d6e5c4b4b95cea35b8cc32f1880ae8134`.
- [x] Waveshare final/package validator PR #142; post-soak validator PR #150; generator physical validator PR #151; secure OTA physical validator PR #152.
- [x] Site source commissioning validator PR #156 -> `1c6e1de9ba01c759bc7dc6331f418160614cbbd7`.
- [x] Inverter per-model physical qualification validator PR #158 -> `56e2abfb9291b8b5f0786dc8051820a53865984b`.
- [x] Waveshare one-command physical soak capture executor PR #159 -> `3fd831b677ff590c54cb5cef412a55c9cdea5ca8`.
- [x] Integrated FAT/endurance/signed-SAT validator and runbook PR #160 -> `1b4d7631862afdb38da99fbbae9aa170729b0bdb`.

## GLOBAL DONE GATE

Do not claim 100% until every P0 physical/site/manufacturer/UI/FAT/SAT item above genuinely passes against exact identities, all required promotions are merged with fresh exact-head CI, final evidence is traceable, and no critical blocker remains. CI or validators may enforce evidence quality but cannot create hardware/site PASS.
