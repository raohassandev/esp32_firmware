# Waveshare ESP32-S3-Touch-LCD-5 — Execution TODO

Status: `PLANNED — FEATURE PARITY ONLY`

Architecture authority: `docs/architecture/CORE_BOARD_EXECUTION_PLAN.md`

Board branch: `board/waveshare-esp32-s3-touch-lcd-5`

Source baseline: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

## Scope guard

- [x] Existing real-site-tested product is the behavioral reference.
- [x] Existing Web UI remains authoritative shared UI.
- [x] No new customer-facing or field-control feature is authorized.
- [x] Core changes must propagate to every supported board.
- [x] Board-specific behavior must remain isolated to its board.
- [x] Scheduler is not authorized.
- [ ] Exact physical Waveshare SKU/revision recorded.

## P0 — Freeze current working Core

Owner: L0 + L1

- [ ] Record exact approved Core source SHA and toolchain version.
- [ ] Inventory current Core-owned components.
- [ ] Inventory current target/DevKit-specific assumptions.
- [ ] Inventory top-level `sdkconfig.defaults`, Kconfig and partition assumptions.
- [ ] Record current configuration schema versions and migration guarantees.
- [ ] Record API route inventory.
- [ ] Record existing Web asset/route/browser contract baseline.
- [ ] Record control/safety/source-detection test inventory.
- [ ] Record current auth/RBAC and commissioning gates.
- [ ] Record intentionally disabled/unqualified behaviors.
- [ ] Reconcile existing repository evidence that is still authoritative.
- [ ] Create `CORE_OWNERSHIP.md` or equivalent machine-readable ownership record.
- [ ] Mark all Waveshare-only extra peripherals `RESERVED_NOT_ACTIVE`.

Acceptance:
- [ ] No ambiguity remains about behavior to preserve.
- [ ] No new feature requirement entered the milestone.

## P1 — Define minimal Board Support Contract

Owner: L0 + L3

- [ ] Define board identity contract.
- [ ] Define board capability declaration.
- [ ] Define safe board init contract.
- [ ] Define optional safe shutdown/failure hook only if actually required.
- [ ] Define compile-time single-board selection.
- [ ] Add build failure for invalid/multiple target selection.
- [ ] Define board-specific sdkconfig/default ownership.
- [ ] Define pin/resource ownership mechanism.
- [ ] Define runtime Core SHA + Board ID reporting.
- [ ] Add source/architecture test that board-specific conditionals do not spread through Core safety/business files.
- [ ] Do not add unused LCD/RS485/SD/RTC/CAN/DI-DO APIs.

Acceptance:
- [ ] Board contract is intentionally small.
- [ ] Product behavior is unchanged.

## P2 — Create repository board structure

Owner: L2 + L0 integration

Target structure:

- [ ] Create `components/board_support/**`.
- [ ] Create `boards/waveshare_esp32_s3_touch_lcd_5/**`.
- [ ] Add board README with exact SKU/revision and vendor references.
- [ ] Add board pins/resource table.
- [ ] Add board-specific sdkconfig defaults/delta.
- [ ] Add board capability manifest.
- [ ] Add build integration so only selected board adapter links.
- [ ] Keep stable Core components in existing locations unless a real dependency forces a seam.
- [ ] Prohibit wholesale movement/renaming of field-proven modules.

Acceptance:
- [ ] Shared Core remains visually and logically recognizable.
- [ ] Waveshare implementation is contained in board paths plus bounded integration files.

## P3 — Freeze exact Waveshare hardware baseline

Owner: L2 + L6

- [ ] Confirm exact model: 5 or 5B.
- [ ] Record SKU.
- [ ] Record PCB revision/rear-board markings.
- [ ] Pin official Waveshare docs/repository commit used as reference.
- [ ] Record MCU/module/Flash/PSRAM identity.
- [ ] Record power requirements.
- [ ] Record all onboard peripheral pins/resources for conflict analysis.
- [ ] Record boot/strap/debug pin conflicts.
- [ ] Record required CH422G/default expander states even if application features remain disabled.
- [ ] Record peripherals not used by current product as `RESERVED_NOT_ACTIVE`.
- [ ] Run only the minimal vendor baseline needed to prove board health and required boot resources.

Acceptance:
- [ ] Exact hardware identity is frozen.
- [ ] No vendor demo is called project acceptance.

## P4 — Port existing Core to Waveshare with zero feature expansion

Owner: L2 + L3 + L0 integration

- [ ] Build shared Core for Waveshare target.
- [ ] Resolve only genuine board/toolchain incompatibilities.
- [ ] Preserve 16 MB flash/8 MB PSRAM assumptions only after exact-board verification.
- [ ] Preserve partition layout unless a proven board constraint requires change.
- [ ] Preserve existing NVS/config schema.
- [ ] Preserve Wi-Fi behavior.
- [ ] Preserve current recovery behavior.
- [ ] Preserve current HTTP server behavior.
- [ ] Preserve current Modbus TCP path.
- [ ] Preserve current meter/inverter profile behavior.
- [ ] Preserve source detection.
- [ ] Preserve control/safety calculations.
- [ ] Preserve commissioning and write gates.
- [ ] Preserve auth/session/RBAC behavior.
- [ ] Preserve alarms/audit/provenance.
- [ ] Preserve existing Web interface/assets.
- [ ] Keep LCD/touch application feature disabled.
- [ ] Keep direct onboard RS485 product transport disabled.
- [ ] Keep SD application usage disabled.
- [ ] Keep RTC/CAN/DI-DO application features disabled.

Acceptance:
- [ ] Exact Waveshare build passes.
- [ ] No unauthorized feature appears in code/UI/API/config.

## P5 — Golden regression suite

Owner: L4 + L5 independent QA

- [ ] Run all current host/unit tests.
- [ ] Run all current source contracts.
- [ ] Run config import/export/migration tests.
- [ ] Run persistence/default tests.
- [ ] Run auth positive/negative tests.
- [ ] Run commissioning authority tests.
- [ ] Run alarm lifecycle tests.
- [ ] Run source-detection tests.
- [ ] Run control fail-closed tests.
- [ ] Run meter/inverter profile/write-gate tests.
- [ ] Run Modbus TCP tests.
- [ ] Run Web syntax/asset-order tests.
- [ ] Run full-app browser fixture.
- [ ] Capture representative Web screenshots only as regression evidence; no redesign.
- [ ] Compare API schemas with golden baseline.
- [ ] Compare config semantics with golden baseline.
- [ ] Treat every unexplained behavioral difference as FAIL.

Acceptance:
- [ ] Golden deterministic regression is PASS at exact head.

## P6 — Exact Waveshare runtime proof

Owner: L6 Hardware/HIL

- [ ] Flash without destructive NVS erase.
- [ ] Confirm board boots without panic/reset loop.
- [ ] Confirm Core/Board identity in logs.
- [ ] Confirm PSRAM detection and memory test as applicable.
- [ ] Record bootstrap stack high-water mark.
- [ ] Record task stack high-water marks for material tasks.
- [ ] Record free/minimum internal heap.
- [ ] Record total/free PSRAM.
- [ ] Confirm watchdog margin.
- [ ] Confirm Wi-Fi association/recovery path.
- [ ] Confirm Web UI reachable from real browser.
- [ ] Confirm concurrent browser/API use does not wedge HTTP server.
- [ ] Confirm current Modbus TCP path against controlled/real peer.
- [ ] Confirm existing meter read path.
- [ ] Confirm existing inverter read/command-gate state.
- [ ] Confirm automatic control starts in the expected safe authority state.
- [ ] Confirm unused Waveshare peripherals do not create boot/resource/output side effects.

Acceptance:
- [ ] `TARGET_RUNTIME_PASS` for current-product hardware paths.

## P7 — Current-product HIL/safety equivalence

Owner: L5 + L6

- [ ] Exercise current meter unavailable/stale behavior.
- [ ] Exercise current network loss/recovery behavior.
- [ ] Exercise Modbus TCP peer timeout/unavailable behavior.
- [ ] Exercise wrong/invalid device evidence where current test setup supports it.
- [ ] Exercise inverter write authorization denial.
- [ ] Exercise existing command/readback behavior where real qualified peer is available.
- [ ] Exercise reboot while configuration is commissioned.
- [ ] Verify no normal test erases NVS/credentials.
- [ ] Verify failure paths remain fail-closed.
- [ ] Verify resource pressure does not starve control/network paths.
- [ ] Verify existing site-critical safety invariants against exact board.

Acceptance:
- [ ] `HIL/BENCH_PASS` for all hardware-dependent current-product requirements available in the bench setup.
- [ ] Anything requiring unavailable real equipment remains explicitly pending, never called complete.

## P8 — Establish canonical Core branch

Owner: L0

- [ ] Reconcile `phase1-fix` with legitimate later generic Core fixes only.
- [ ] Identify stale/diverged feature/UI/hardware branches that must not define Core.
- [ ] Create `core/stable` from approved reconciled Core head.
- [ ] Document `core/stable` as the only generic-product merge destination.
- [ ] Update README/developer pull instructions away from stale branch references.
- [ ] Protect rule: generic fixes cannot terminate on board branches.
- [ ] Protect rule: board branches are never merged wholesale into Core.

Acceptance:
- [ ] Canonical Core authority is unambiguous.

## P9 — Implement Board Sync Gate

Owner: L0 + L7

- [ ] Create board support registry listing all supported board branches.
- [ ] Create `core_head` / `synced_core_sha` ledger.
- [ ] Define `CORE_CURRENT`, `CORE_STALE`, `BOARD_FAIL` states.
- [ ] Define required checks after every Core merge.
- [ ] Ensure every Core change creates an obligation for all `SUPPORTED` boards.
- [ ] Add CI/build matrix where feasible without creating unnecessary workflow churn.
- [ ] Record hardware-validation requirement based on changed failure surface rather than always rerunning every physical test.
- [ ] Block board release when its Core SHA is stale unless explicitly frozen by authority.

Acceptance:
- [ ] Repository can answer exactly which Core SHA every board contains.

## P10 — Prove Core propagation end-to-end

Owner: L0 + relevant QA/HIL lanes

- [ ] Use the next legitimate Core fix/improvement, or a harmless non-behavioral diagnostic change if no defect exists.
- [ ] Create bounded `work/core/<issue>` branch.
- [ ] Verify Core change.
- [ ] Merge to `core/stable`.
- [ ] Sync `board/waveshare-esp32-s3-touch-lcd-5`.
- [ ] Sync every other currently supported board branch.
- [ ] Run per-board exact build.
- [ ] Run change-risk-selected regression.
- [ ] Run hardware evidence where the failure surface requires it.
- [ ] Update sync ledger.

Acceptance:
- [ ] Multi-board Core propagation is proven by repository history/evidence, not only described in documentation.

## P11 — Branch cleanup

Owner: L7

For every existing branch:

- [ ] Classify `CANONICAL_CORE`.
- [ ] Classify `SUPPORTED_BOARD`.
- [ ] Classify `ACTIVE_WORK`.
- [ ] Classify `SUPERSEDED_TRACE`.
- [ ] Classify `ARCHIVE_CANDIDATE`.
- [ ] Classify `DELETE_CANDIDATE`.
- [ ] Resolve stale draft PRs.
- [ ] Preserve meaningful failure/field evidence before deletion.
- [ ] Archive/tag trace branches when needed.
- [ ] Delete merged/obsolete temporary branches only after classification.
- [ ] Keep long-lived branch count intentional.

Acceptance:
- [ ] No branch can be mistaken for canonical Core or a supported board accidentally.

## P12 — Final Waveshare parity release gate

Owner: L0 + L4 + L5 + L6 + L7

- [ ] Exact release Core SHA recorded.
- [ ] Exact Waveshare board head recorded.
- [ ] Exact toolchain/dependency lock recorded.
- [ ] All REQUIRED Quality-360 dimensions PASS.
- [ ] Deterministic regression PASS.
- [ ] Exact target build PASS.
- [ ] Required target runtime PASS.
- [ ] Required HIL/bench PASS.
- [ ] Existing Web/API/config behavior PASS.
- [ ] No unauthorized new features present.
- [ ] Board-specific implementation contained.
- [ ] Core sync ledger current.
- [ ] `.aish/RESUME.md` updated.
- [ ] Temporary work branches cleaned.

Final allowed lifecycle only after all applicable evidence:

`FEATURE_PARITY_COMPLETE — WAVESHARE BOARD`

## Deferred future work — not part of current TODO

Do not start without a new Product Owner authorization/work packet:

- [ ] Native LCD/LVGL HMI.
- [ ] Touch UI.
- [ ] Onboard RS485/Modbus RTU production transport.
- [ ] SD logging/history.
- [ ] RTC application integration.
- [ ] CAN/TWAI integration.
- [ ] Isolated DI/DO product logic.
- [ ] Any new control feature.
- [ ] Any new Web feature.