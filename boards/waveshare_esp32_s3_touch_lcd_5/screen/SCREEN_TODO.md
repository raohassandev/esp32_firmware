# Waveshare Screen TODO

Scope: local Waveshare 5-inch HMI only. Existing backend/core behavior is authoritative. No new product functionality in this milestone.

## S0 — API contract foundation
- [x] Freeze screen use of existing `GET /api/live` and `GET /api/status` contracts.
- [x] Add screen-owned C models for those payloads.
- [x] Preserve backend `null`/unknown values instead of coercing them to zero.
- [x] Parse control labels/reasons without re-deriving control policy.
- [x] Use `/api/status.source.attributed_to` as the authoritative source label.
- [ ] Add parser tests with representative good/stale/offline/unknown payload fixtures.

## S1 — Read-only operator overview
- [x] Create LVGL overview page skeleton.
- [x] Grid/active-source power card.
- [x] Solar power card.
- [x] Requested PV card.
- [x] Applied PV card.
- [x] Control mode and inhibit reason.
- [x] Meter/network/controller/alarm state rows.
- [x] Explicit backend-unavailable rendering.
- [ ] Replace hexadecimal alarm fallback with backend-provided human-readable alarm names.
- [ ] Exact 800x480/1024x600 layout verification after physical SKU is frozen.

## S2 — Waveshare display/touch qualification
- [ ] Freeze exact board SKU and PCB revision.
- [ ] Pin Waveshare upstream baseline commit `a7b179dbfccea8121c88770d8a3c53e5a84b1024` or a reviewed successor.
- [ ] Qualify official LVGL v9 demo against project ESP-IDF 6.0.1.
- [ ] Pin exact LVGL / esp_lvgl_adapter / GT911 component versions.
- [ ] Bring up RGB display without application/core changes.
- [ ] Bring up GT911 touch without application/core changes.
- [ ] Measure framebuffer/PSRAM/internal-DMA heap impact.

## S3 — Build integration
- [x] Add isolated screen-local `CMakeLists.txt`.
- [x] Keep screen component disconnected from the current site-tested default build.
- [ ] Add board-specific build selector only after S2 dependency qualification.
- [ ] Compile exact Waveshare target with zero new warnings.
- [ ] Prove current non-Waveshare/default build is unchanged.

## S4 — Data/runtime integration
- [ ] Select the lowest-risk adapter that consumes the existing authoritative backend state without creating a second business-logic implementation.
- [ ] Feed `/api/live`-equivalent fast state to the page at the existing 500 ms information cadence.
- [ ] Feed `/api/status`-equivalent slower state at the existing status cadence.
- [ ] Bound UI work so rendering/touch can never block control/network tasks.
- [ ] On missing/stale backend data, render unavailable/unknown rather than a guessed value.

## S5 — Parity pages
Only existing product capabilities may be represented.
- [ ] Overview parity.
- [ ] Grid/meter operational view parity.
- [ ] Solar/inverter operational view parity.
- [ ] Alarms/event view parity.
- [ ] Readiness/status parity.
- [ ] Engineering/commissioning surface only if explicitly enabled later, using existing authorization/write gates.

## S6 — QA / HIL
- [ ] API contract regression tests.
- [ ] Exact-resolution UI render checks.
- [ ] Touch target/readability checks.
- [ ] Meter offline/stale behavior.
- [ ] Source unknown/conflict behavior.
- [ ] Network loss/recovery behavior.
- [ ] Backend unavailable behavior.
- [ ] Active alarms behavior.
- [ ] Display/touch fault must not alter control authority.
- [ ] Resource/stack/heap/watchdog measurements under active UI.
- [ ] Physical board acceptance before COMPLETE.
