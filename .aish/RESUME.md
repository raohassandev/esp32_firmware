# AISH Resume — Waveshare ESP32-S3-Touch-LCD-5

Updated: 2026-08-18

Lifecycle: `SCREEN SOFTWARE IMPLEMENTED — EXACT-TARGET HARDWARE INTEGRATION PENDING`

## Canonical execution target

Repository: `raohassandev/esp32_firmware`

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Branch origin: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

AISH baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Master plan: `docs/boards/waveshare-esp32-s3-touch-lcd-5/MASTER_PLAN.md`

Architecture plan: `docs/architecture/CORE_BOARD_EXECUTION_PLAN.md`

Board execution TODO: `docs/boards/waveshare-esp32-s3-touch-lcd-5/TODO.md`

Screen workspace: `boards/waveshare_esp32_s3_touch_lcd_5/screen/`

Screen execution TODO: `boards/waveshare_esp32_s3_touch_lcd_5/screen/SCREEN_TODO.md`

## Product Owner scope lock

- `phase1-fix` is the current latest working/site-tested product baseline.
- One authoritative shared Product Core; no copied business/safety firmware per board.
- Every hardware target has a board-specific area/branch.
- Generic Core fixes/improvements propagate to every supported board.
- Board-specific hardware/UI behavior remains confined to that board.
- Existing backend/API and real-site-tested behavior stay authoritative.
- Waveshare local LCD work is authorized as a board-specific presentation surface over existing functionality.
- Current local HMI is read-only. No new control/write/business functionality is authorized.
- Existing Web UI remains unchanged/shared.
- Scheduler/automation scheduling is not authorized for this project.

## Current board/screen architecture

Core/business authority remains in existing components. Screen code is isolated at:

`boards/waveshare_esp32_s3_touch_lcd_5/screen/**`

The screen does not include or call control_engine, safety_manager, meter_manager, inverter_manager, config_manager, commissioning_gate, network_manager, or Modbus implementation headers directly.

It consumes existing backend contracts through bounded screen-owned parsers and a provider-injected runtime bridge. There is no second HTTP server, second backend, hidden FreeRTOS screen task, or local control policy.

The root/default project `CMakeLists.txt` still does NOT compile the screen component. This protects the current site-tested default build until exact Waveshare display/LVGL qualification is complete.

## Existing API contracts consumed

No endpoint was created for the local HMI. It consumes existing:

- `GET /api/live`
- `GET /api/status`
- `GET /api/meters`
- `GET /api/inverters`
- `GET /api/telemetry`
- `GET /api/operator/events`
- `GET /api/operator/alarms`

Safety/presentation invariants:

- numeric `null`/missing -> unavailable, never measured zero;
- source attribution -> `/api/status.source.attributed_to` only;
- control mode/inhibit reason -> backend wording, not locally re-derived;
- endpoint failure -> affected surface becomes unavailable; unrelated healthy surface remains intact;
- current screen has no write/control callbacks.

## Screen source implementation — completed

### API/runtime

- `screen/api/screen_api.h/.c`
  - bounded models/parsers for live, status, meters, inverters, telemetry, events, alarms;
  - bounded row counts and text buffers;
  - null/unknown preservation.
- `screen/screen_runtime.h/.c`
  - provider-injected existing-API bridge;
  - separate fast/status/devices/operations refresh lanes;
  - no internal scheduler/task;
  - large bounded snapshots are module-static instead of LVGL-task stack allocations.

### UI

- `screen/components/screen_widgets.*` — shared LVGL presentation layer.
- `screen/pages/overview_screen.*` — Overview.
- `screen/pages/grid_screen.*` — Grid/Meters.
- `screen/pages/solar_screen.*` — Solar/Inverters.
- `screen/pages/alarms_screen.*` — Alarms/Events.
- `screen/pages/readiness_screen.*` — Readiness/Controller State.
- `screen/screen_app.*` — touch navigation: Overview / Grid / Solar / Alarms / Ready.

All are read-only product-parity surfaces.

### Waveshare display profiles

Pinned upstream review baseline:

`waveshareteam/ESP32-S3-Touch-LCD-5@a7b179dbfccea8121c88770d8a3c53e5a84b1024`

`screen/drivers/waveshare_display_profile.*` records both vendor profiles with NO default physical SKU:

- `WAVESHARE_DISPLAY_800X480`: 800x480, 16 MHz pixel clock, vendor porch/pulse timings;
- `WAVESHARE_DISPLAY_1024X600`: 1024x600, 21 MHz pixel clock, vendor porch/pulse timings;
- shared vendor RGB pins;
- I2C SDA GPIO8 / SCL GPIO9 at 400 kHz;
- GPIO4 recorded by observed vendor reset/address-selection role, without mislabeling it as the only GT911 reset path because CH422G also participates.

## QA evidence/state

Added:

- `tests/waveshare_screen_api_test.c`
- `tests/waveshare_screen_source_contract.py`
- `tests/waveshare_display_profile_test.c`
- `.github/workflows/waveshare-screen-checks.yml`

The dedicated workflow checks:

- existing backend owns every screen API path;
- no direct business/control dependency leaks into screen code;
- no local HTTP write/server behavior;
- no hidden screen task/scheduler;
- only navigation uses LVGL callbacks;
- fail-closed source rendering;
- null/unavailable handling;
- complete isolated component manifest;
- both Waveshare display profiles with no default SKU;
- host parser tests with `-Wall -Wextra -Werror`;
- host display-profile test with `-Wall -Wextra -Werror`;
- root/default build isolation.

Independent local execution evidence captured in this session:

- display-profile host compile/run with GCC `-std=c11 -Wall -Wextra -Werror` — `PASS` on 2026-08-18.

GitHub Actions final-green evidence remains pending until the workflow run is observed at the final head.

## Waveshare capability state

- LCD/LVGL HMI — `SOURCE_IMPLEMENTED / HARDWARE_PORT_PENDING`
- touch navigation — `SOURCE_IMPLEMENTED / PHYSICAL_TOUCH_PENDING`
- onboard RS485/Modbus RTU product transport — `RESERVED_NOT_ACTIVE`
- SD application logging/history — `RESERVED_NOT_ACTIVE`
- RTC application integration — `RESERVED_NOT_ACTIVE`
- CAN/TWAI product integration — `RESERVED_NOT_ACTIVE`
- isolated DI/DO product logic — `RESERVED_NOT_ACTIVE`

Do not activate the reserved capabilities in this milestone.

## Remaining critical path — cannot be called complete without evidence

1. Freeze exact physical target: `ESP32-S3-Touch-LCD-5` 800x480 or `ESP32-S3-Touch-LCD-5B` 1024x600, plus PCB revision.
2. Qualify official LVGL v9 / `esp_lvgl_adapter` / GT911 dependency set against project ESP-IDF 6.0.1 and pin exact accepted versions.
3. Implement/qualify ESP-IDF RGB + GT911 + CH422G hardware port from the reviewed board profile.
4. Add a board-specific build selector; do not alter the site-tested default build.
5. Exact Waveshare build with zero new warnings and full existing firmware regression suite.
6. Bind screen runtime provider to existing backend authority without duplicating business logic.
7. Exact-board flash/render/touch evidence.
8. PSRAM/framebuffer/internal-DMA heap, stack, watchdog, UI latency and control-loop jitter measurements.
9. Fault/HIL: meter stale/offline, source unknown/conflict, network/backend loss/recovery, alarm behavior, display/touch fault isolation.
10. Only after those gates: `LOCAL_SCREEN_COMPLETE — EXACT WAVESHARE TARGET`.

## Larger Core/board work state

- P0 Freeze current working Core — `READY`
- P1 Minimal Board Support Contract — `WAITING_DEPENDENCY: P0 ownership/baseline`
- P2 Repository board structure — `PARTIAL`: Waveshare board/screen area exists; generic board contract remains pending
- P3 Exact Waveshare hardware baseline — `BLOCKED_ONLY_ON_PHYSICAL_SKU/REVISION`
- P4 Feature-parity Waveshare board port — `WAITING_DEPENDENCY: P1/P2/P3`
- P5 Golden regression — `PARALLEL_READY once integrated board head exists`
- P6 Exact Waveshare runtime proof — `WAITING_DEPENDENCY: P4`
- P7 Current-product HIL/safety equivalence — `WAITING_DEPENDENCY: P4/P6`
- P8 Establish canonical shared Core line — `READY_AFTER_P0 reconciliation`
- P9 Board Sync Gate — `WAITING_DEPENDENCY: P8`
- P10 Prove Core propagation — `WAITING_DEPENDENCY: P9 + legitimate Core change`
- P11 Branch cleanup — `CLASSIFICATION_READY; destructive cleanup later`
- P12 Final parity release — `WAITING_DEPENDENCY`

Screen sub-work:

- S0 API contract foundation — `SOURCE_COMPLETE / HOST TESTS ADDED`
- S1 Overview — `SOURCE_COMPLETE / EXACT-RESOLUTION EVIDENCE PENDING`
- S2 Display/touch qualification — `UPSTREAM PROFILE COMPLETE / EXACT DEPENDENCY + HARDWARE EVIDENCE PENDING`
- S3 Build integration — `ISOLATED COMPONENT + CI COMPLETE / BOARD BUILD SELECTOR PENDING`
- S4 Runtime data integration — `BRIDGE COMPLETE / BOARD PROVIDER + CADENCE/HIL PENDING`
- S5 Operator parity pages — `SOURCE_COMPLETE`
- S6 QA/HIL — `SOFTWARE TESTS ADDED / EXACT-TARGET QA PENDING`

## Only owner input materially blocking exact hardware work

Provide/confirm exact physical board (`ESP32-S3-Touch-LCD-5` vs `ESP32-S3-Touch-LCD-5B`) and PCB revision/rear-board photo when available.

Do not stop non-hardware Core/board governance work while waiting for this input.

## Completion vocabulary

Source implementation is NOT hardware completion.

Allowed future terminal states only after applicable AISH gates:

- `LOCAL_SCREEN_COMPLETE — EXACT WAVESHARE TARGET`
- `FEATURE_PARITY_COMPLETE — WAVESHARE BOARD`
