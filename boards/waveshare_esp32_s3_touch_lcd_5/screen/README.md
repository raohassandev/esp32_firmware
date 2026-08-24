# Waveshare Screen Workspace

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Purpose: keep all Waveshare 5-inch local screen/HMI work isolated from the site-tested product core while consuming the same existing backend/API authority.

## Rules

- Use the existing backend/application API contracts and authoritative state already provided by the firmware core.
- Do not duplicate backend, control, safety, commissioning, meter/inverter, alarm, auth, or persistence logic here.
- Current HMI is read-only; no control/write callbacks are authorized in this milestone.
- No new product feature/functionality is authorized.
- Board/screen-specific code stays inside this board workspace and must not leak into generic core components.
- Any generic backend bug discovered during screen integration belongs in the canonical core and must propagate to supported boards.
- Unknown/stale/unavailable backend values remain visibly unknown; never coerce them to measured zero.
- Operator source attribution comes only from backend fail-closed `/api/status.source.attributed_to`.
- The current site-tested/default firmware build must remain unchanged until the exact Waveshare hardware/dependency gate is qualified.

## Current structure

- `api/` — bounded models/parsers for existing backend JSON contracts; no business logic.
- `components/` — reusable LVGL presentation widgets.
- `pages/` — Overview, Grid, Solar, Alarms/Events, Readiness.
- `drivers/` — exact 800x480/1024x600 vendor display profiles; hardware port remains qualification-gated.
- `screen_app.*` — read-only touch navigation shell.
- `screen_runtime.*` — provider-injected refresh bridge for the existing API path contracts; creates no task/scheduler.
- `SCREEN_TODO.md` — execution/evidence checklist.
- `CMakeLists.txt` — isolated component definition, intentionally not wired into root/default build yet.

## Existing backend contracts consumed

No new HTTP/API endpoint was added for this HMI:

- `GET /api/live`
- `GET /api/status`
- `GET /api/meters`
- `GET /api/inverters`
- `GET /api/telemetry`
- `GET /api/operator/events`
- `GET /api/operator/alarms`

`screen_runtime` groups these into fast, status/readiness, device, and operations refresh lanes. Cadence is owned by the future qualified board integration rather than a hidden screen task.

## Current implemented operator surfaces

- Overview: Grid/active-source kW, Solar kW, requested/applied PV, control mode/reason, meter/network/controller/alarm state, firmware version.
- Grid/Meters: configured/enabled/online summary, role/state, measured power and data age.
- Solar/Inverters: fleet summary/capacity, per-inverter state, measured power/age where supported, existing commanded-percent evidence.
- Alarms/Events: existing primary/consequential/unacknowledged counts, lifecycle/suppression state, recommended action, recent events.
- Readiness: monitoring readiness, command-path readiness, automatic-control state, network, meter, inverter, controller, source and authority state.
- Navigation: `Overview / Grid / Solar / Alarms / Ready` only.

All unavailable numeric measurements are rendered as unavailable (`--`) instead of a fabricated zero.

## Waveshare upstream baseline

Reviewed/pinned reference commit:

`waveshareteam/ESP32-S3-Touch-LCD-5@a7b179dbfccea8121c88770d8a3c53e5a84b1024`

The official LVGL v9 example at that revision provides the bring-up reference for RGB panel, GT911 touch, CH422G sequencing and Espressif LVGL adapter. It is not copied as product logic and a vendor-demo pass is not project acceptance.

The board-local pure-C profiles preserve both vendor variants without guessing the user's physical SKU:

- 800x480 profile: 16 MHz pixel clock and its vendor timing set.
- 1024x600 profile: 21 MHz pixel clock and its vendor timing set.
- common RGB pin map and I2C GPIO8/GPIO9 are recorded in `drivers/waveshare_display_profile.*`.

## QA state

Added:

- `tests/waveshare_screen_source_contract.py` — proves backend ownership/isolation/read-only boundary/no hidden scheduler/fail-closed source behavior.
- `tests/waveshare_screen_api_test.c` — parser fixtures for null/stale/status/meters/inverters/readiness/events/alarms.
- `tests/waveshare_display_profile_test.c` — executable profile/pin/timing validation for both variants.
- `.github/workflows/waveshare-screen-checks.yml` — board-branch CI gate.

Independent local GCC execution of the pure display-profile test passed with `-Wall -Wextra -Werror` on 2026-08-18. GitHub CI and exact ESP-IDF/LVGL build evidence still have to close at the integrated head.

## Remaining hardware/integration gate

The software source is not called hardware-complete. Remaining critical path:

1. identify exact physical `ESP32-S3-Touch-LCD-5` vs `5B` SKU and PCB revision;
2. qualify/pin LVGL 9 + `esp_lvgl_adapter` + GT911 dependency versions against this project's ESP-IDF 6.0.1;
3. implement/qualify the RGB/GT911/CH422G hardware port using the reviewed profile;
4. enable this component only in the dedicated Waveshare board build;
5. bind the runtime provider to the existing backend authority without a second business-logic implementation;
6. exact-board compile/flash/render/touch tests;
7. PSRAM/DMA/heap/stack/watchdog/control-jitter measurements and fault/HIL tests.

Current lifecycle: `SCREEN SOFTWARE IMPLEMENTED — HARDWARE INTEGRATION / EXACT-TARGET EVIDENCE PENDING`.
