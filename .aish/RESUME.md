# AISH Resume — Waveshare ESP32-S3-Touch-LCD-5

Updated: 2026-08-18

Lifecycle: `IMPLEMENTATION_STARTED — FEATURE PARITY + BOARD-LOCAL SCREEN`

## Canonical execution target

Repository: `raohassandev/esp32_firmware`

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Branch origin: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

AISH baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Master plan: `docs/boards/waveshare-esp32-s3-touch-lcd-5/MASTER_PLAN.md`

Architecture plan: `docs/architecture/CORE_BOARD_EXECUTION_PLAN.md`

Execution TODO: `docs/boards/waveshare-esp32-s3-touch-lcd-5/TODO.md`

Screen workspace: `boards/waveshare_esp32_s3_touch_lcd_5/screen/`

Screen TODO: `boards/waveshare_esp32_s3_touch_lcd_5/screen/SCREEN_TODO.md`

## Product Owner scope lock

- One authoritative shared Product Core.
- `phase1-fix` is the current latest working/site-tested product baseline.
- Every supported hardware board gets a dedicated adapter/integration area and dedicated long-lived `board/<board-id>` branch.
- Generic Core fixes/improvements must propagate to every supported board.
- Board-specific functionality must remain confined to that board.
- Existing real-site-tested product behavior is the reference.
- Existing Web UI/backend APIs remain shared and authoritative.
- Waveshare local screen/HMI work is authorized only as a board-specific presentation surface over existing product functionality.
- No new backend/business/control functionality is authorized in this milestone.
- Scheduler/automation scheduling is not authorized.

## Current architecture decision

Do not copy/fork the entire firmware per board and do not perform a big-bang refactor.

Existing product components remain Core-owned in their current locations. Introduce a minimal Board Support Contract only where needed to remove physical-board assumptions.

Board-specific path:

`boards/waveshare_esp32_s3_touch_lcd_5/**`

Planned shared boundary:

`components/board_support/**`

The Core owns product behavior. Board adapters own physical-board identity/resources/safe initialization only. The Waveshare screen is a board-local frontend and may not become a second backend.

## Current product parity surface

Must remain unchanged unless a real defect is independently proven:

- configuration/migration/persistence;
- Wi-Fi/network/recovery behavior;
- Modbus TCP path;
- meter/inverter behavior;
- source detection;
- control/safety/fail-closed behavior;
- commissioning/write gates;
- auth/session/RBAC;
- alarms/audit/provenance;
- HTTP APIs;
- Web UI/browser behavior;
- automatic-control default/authority semantics.

## Waveshare capability state

Current milestone state:

- LCD/LVGL HMI — `ACTIVE_BOARD_SPECIFIC_SCREEN_WORK`
- touch application UI — `ACTIVE_BOARD_SPECIFIC_SCREEN_WORK`, read-only operator surface first
- onboard RS485/Modbus RTU product transport — `RESERVED_NOT_ACTIVE`
- SD application logging/history — `RESERVED_NOT_ACTIVE`
- RTC application integration — `RESERVED_NOT_ACTIVE`
- CAN/TWAI product integration — `RESERVED_NOT_ACTIVE`
- isolated DI/DO product logic — `RESERVED_NOT_ACTIVE`

The local screen may represent existing product state only. It may not create new control/business semantics.

## Screen implementation state

Implemented on this branch:

- `screen/api/screen_api.h` — local presentation models for existing `GET /api/live` and `GET /api/status` contracts.
- `screen/api/screen_api.c` — cJSON parser that preserves null/unknown values rather than coercing to zero.
- `screen/pages/overview_screen.h/.c` — read-only LVGL operator Overview skeleton.
- Overview shows grid/active-source power, solar power, requested/applied PV, control mode/reason, meter/network/controller/alarm state and firmware version.
- Screen source attribution is deliberately taken from `/api/status.source.attributed_to`, the backend fail-closed field intended for screens; raw live source is not rendered as authoritative.
- `screen/CMakeLists.txt` exists but is intentionally not wired into the current default firmware build yet.
- `screen/SCREEN_TODO.md` records remaining bounded work.

Waveshare upstream hardware/display reference pinned for qualification:

`waveshareteam/ESP32-S3-Touch-LCD-5@a7b179dbfccea8121c88770d8a3c53e5a84b1024`

Official LVGL v9 example dependencies observed at that baseline: LVGL 9, Espressif `esp_lvgl_adapter`, GT911 touch support. Vendor demo is only a bring-up reference, never product acceptance.

Current screen evidence state:

`SOURCE_IMPLEMENTED / BUILD_INTEGRATION_PENDING / HARDWARE_VALIDATION_PENDING`

## Core branch model to establish

Future canonical generic branch: `core/stable`.

Until reconciled and created, the migration source remains:

`phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

Generic fix flow:

`work/core/<issue> -> verify -> core/stable -> Board Sync Gate -> every SUPPORTED board -> per-board evidence`

Board-only flow:

`work/board/<id>/<issue> -> verify -> board/<id>`

Never merge one full board branch into another. Never merge a full board branch wholesale into Core.

## Work state

- P0 Freeze current working Core — `READY`
- P1 Minimal Board Support Contract — `WAITING_DEPENDENCY: P0 ownership/baseline`
- P2 Repository board structure — `PARTIAL`: Waveshare screen workspace exists; generic board contract remains pending
- P3 Exact Waveshare hardware baseline — `READY_AFTER_OWNER_SKU`
- P4 Feature-parity Waveshare port — `WAITING_DEPENDENCY: P1/P2/P3`
- P5 Golden regression — `PARALLEL_READY once parity head exists; baseline capture can start in P0`
- P6 Exact Waveshare runtime proof — `WAITING_DEPENDENCY: P4`
- P7 Current-product HIL/safety equivalence — `WAITING_DEPENDENCY: P4/P6`
- P8 Establish `core/stable` — `READY_AFTER_P0 reconciliation`
- P9 Board Sync Gate — `WAITING_DEPENDENCY: P8`
- P10 Prove Core propagation — `WAITING_DEPENDENCY: P9 + legitimate Core change`
- P11 Branch cleanup — `PARALLEL_READY classification only; destructive cleanup later`
- P12 Final parity release — `WAITING_DEPENDENCY`

Screen sub-work:
- S0 Existing API contract foundation — `IMPLEMENTED / TESTS_PENDING`
- S1 Read-only Overview — `SOURCE_IMPLEMENTED / BUILD_PENDING`
- S2 Waveshare display/touch qualification — `READY_AFTER_OWNER_SKU`
- S3 Board-specific build integration — `WAITING_DEPENDENCY: S2`
- S4 Runtime data integration — `WAITING_DEPENDENCY: S2/S3`
- S5 Existing-product parity pages — `WAITING_DEPENDENCY: S4`
- S6 Screen QA/HIL — `WAITING_DEPENDENCY: integrated screen build`

## Planned lanes

- L0 Integration/Governance
- L1 Baseline/Architecture
- L2 Waveshare Board Adapter + screen hardware integration
- L3 Core Portability
- L4 Regression/Web Parity QA
- L5 Safety/Functional QA
- L6 Hardware/HIL
- L7 Release/Branch Hygiene

Only dependency-valid non-overlapping lanes may execute. Planned lanes are not described as active/executing without evidence.

## Immediate execution order

1. Keep current site-tested firmware build unchanged while screen source is isolated.
2. Freeze exact Waveshare model/SKU/PCB revision.
3. Qualify Waveshare LVGL v9/display/touch baseline against project ESP-IDF 6.0.1 and pin exact component versions.
4. Add parser fixtures/tests for good/stale/offline/unknown existing API payloads.
5. Open board-specific build gate only after dependency qualification; compile screen component with zero new warnings.
6. Bring up display/touch on exact hardware and measure PSRAM/internal-DMA heap impact.
7. Connect Overview to existing authoritative backend state without introducing a second business-logic implementation.
8. Continue existing-product screen parity pages, then hardware/HIL/fault tests.
9. In parallel, continue the larger P0/P1/P3/Core-board architecture work without mixing board-specific screen code into Core.

## Current owner input required

Only one input materially blocks exact hardware work:

- confirm exact physical target: `ESP32-S3-Touch-LCD-5` or `ESP32-S3-Touch-LCD-5B`, plus PCB revision/rear-board photo if available.

Screen/API source work can continue without this input, but exact layout, dependency build integration, display/touch bring-up and hardware evidence cannot be completed until the physical SKU is frozen.

## Completion vocabulary

Do not call the board port or screen complete from source code, compile or vendor-demo evidence alone.

Final allowed states after applicable AISH gates:

`FEATURE_PARITY_COMPLETE — WAVESHARE BOARD`

and, separately,

`LOCAL_SCREEN_COMPLETE — EXACT WAVESHARE TARGET`
