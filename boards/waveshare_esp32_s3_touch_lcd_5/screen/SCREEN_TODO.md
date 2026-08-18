# Waveshare Screen TODO

Scope: local Waveshare 5-inch HMI only. Existing backend/core behavior is authoritative. No new product functionality in this milestone.

Status vocabulary:
- `[x]` = implementation/evidence artifact exists in the branch.
- `[ ]` = still requires implementation or applicable evidence.
- Hardware-dependent items stay open until exact-board evidence exists.

## S0 — API contract foundation
- [x] Freeze screen use of existing `GET /api/live` and `GET /api/status` contracts.
- [x] Extend the same contract model to existing `GET /api/meters`, `/api/inverters`, `/api/telemetry`, `/api/operator/events`, and `/api/operator/alarms`.
- [x] Add bounded screen-owned C models/parsers for those payloads.
- [x] Preserve backend `null`/unknown values instead of coercing them to zero.
- [x] Parse control labels/reasons without re-deriving control policy.
- [x] Use `/api/status.source.attributed_to` as the authoritative source label.
- [x] Add host parser fixtures for good/stale/offline/unknown/null/device/event/alarm payloads.
- [x] Add source contract proving every consumed path is already owned by the existing backend.

## S1 — Read-only operator overview
- [x] Create LVGL overview page.
- [x] Grid/active-source power card.
- [x] Solar power card.
- [x] Requested PV card.
- [x] Applied PV card.
- [x] Control mode and inhibit reason.
- [x] Meter/network/controller/alarm state rows.
- [x] Explicit backend-unavailable rendering.
- [x] Render backend-provided human-readable alarm names rather than hexadecimal flags.
- [x] Use fail-closed backend source attribution only; never render raw `live.source` as authority.
- [ ] Exact 800x480/1024x600 visual verification after physical SKU is frozen.

## S2 — Waveshare display/touch qualification
- [ ] Freeze exact physical board SKU and PCB revision.
- [x] Pin upstream review baseline to `waveshareteam/ESP32-S3-Touch-LCD-5@a7b179dbfccea8121c88770d8a3c53e5a84b1024`.
- [x] Transcribe both official display timing/pin profiles into a board-local pure-C profile with no default SKU assumption.
- [x] Add executable host test for 800x480 and 1024x600 profile values/pins.
- [ ] Qualify official LVGL v9 demo/dependencies against project ESP-IDF 6.0.1.
- [ ] Pin the qualified exact LVGL / esp_lvgl_adapter / GT911 component versions in the board build.
- [ ] Implement/qualify the ESP-IDF RGB/GT911/CH422G hardware port from the reviewed profile.
- [ ] Bring up RGB display on exact board without application/core changes.
- [ ] Bring up GT911 touch on exact board without application/core changes.
- [ ] Measure framebuffer/PSRAM/internal-DMA heap impact.

## S3 — Build integration
- [x] Add isolated screen-local `CMakeLists.txt` containing API, widgets, pages, runtime and display profiles.
- [x] Keep screen component disconnected from the current site-tested default build.
- [x] Add a source contract that fails if the screen leaks into root/default `CMakeLists.txt`.
- [x] Add dedicated board-branch CI workflow for isolation/parser/profile checks.
- [ ] Add board-specific build selector only after S2 dependency qualification.
- [ ] Compile exact Waveshare firmware target with zero new warnings.
- [ ] Run the complete existing firmware regression suite at the integrated Waveshare head.

## S4 — Data/runtime integration
- [x] Implement a provider-injected runtime bridge that consumes the existing API path contracts without a second backend/control implementation.
- [x] Separate refresh lanes: fast `/api/live`, status/readiness, devices, operations.
- [x] Keep scheduling outside the screen component; no hidden FreeRTOS screen task/timer is created.
- [x] Add per-surface unavailable states so one endpoint failure does not erase unrelated healthy data.
- [x] Keep large bounded meter/inverter/alarm/event snapshots off the LVGL task stack.
- [x] On missing/stale backend data, render unavailable/unknown rather than a guessed value.
- [ ] Bind the provider to the qualified board integration while preserving existing backend authority.
- [ ] Invoke `/api/live` refresh at the existing 500 ms information cadence on hardware.
- [ ] Invoke slower status/device/operations refresh without overlapping/control starvation on hardware.
- [ ] Prove UI/render/data work cannot block control/network tasks.

## S5 — Read-only operator parity pages
Only existing product capabilities are represented.
- [x] Overview parity source implementation.
- [x] Grid/meter operational view source implementation.
- [x] Solar/inverter operational view source implementation.
- [x] Alarms/event view source implementation.
- [x] Readiness/status view source implementation.
- [x] Touch navigation shell: Overview / Grid / Solar / Alarms / Ready.
- [x] No control/write callbacks in current HMI milestone.
- Engineering/commissioning mutation surfaces: `N/A — not authorized in current milestone`.

## S6 — QA / HIL
- [x] API parser host-test source added with warnings-as-errors CI command.
- [x] Screen isolation/existing-backend source contract added.
- [x] Display profile host test added; independent local GCC `-Wall -Wextra -Werror` execution passed on 2026-08-18.
- [ ] GitHub CI run must be green at the final software head.
- [ ] Exact-resolution UI render checks.
- [ ] Touch target/readability checks.
- [ ] Meter offline/stale behavior on integrated runtime.
- [ ] Source unknown/conflict behavior on integrated runtime.
- [ ] Network loss/recovery behavior on integrated runtime.
- [ ] Backend unavailable/recovery behavior on integrated runtime.
- [ ] Active alarms/events behavior on integrated runtime.
- [ ] Display/touch fault must not alter control authority.
- [ ] Resource/stack/heap/watchdog/control-jitter measurements under active UI.
- [ ] Physical board acceptance before `COMPLETE`.

## Current remaining critical path

`exact physical SKU/revision -> qualify LVGL/display/touch dependencies on ESP-IDF 6.0.1 -> board hardware port -> board build -> provider binding -> exact-board render/touch/resource/HIL evidence`.

Nothing in the remaining critical path authorizes a new product feature or any new control/write behavior.
