# AISH Resume — Waveshare ESP32-S3-Touch-LCD-5

Updated: 2026-08-18

Lifecycle: `PLANNED — FEATURE PARITY ONLY`

## Canonical execution target

Repository: `raohassandev/esp32_firmware`

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Branch origin: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

AISH baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Master plan: `docs/boards/waveshare-esp32-s3-touch-lcd-5/MASTER_PLAN.md`

Architecture plan: `docs/architecture/CORE_BOARD_EXECUTION_PLAN.md`

Execution TODO: `docs/boards/waveshare-esp32-s3-touch-lcd-5/TODO.md`

## Product Owner scope lock

- One authoritative shared Product Core.
- Every supported hardware board gets a dedicated adapter/integration area and dedicated long-lived `board/<board-id>` branch.
- Generic Core fixes/improvements must propagate to every supported board.
- Board-specific functionality must remain confined to that board.
- Existing real-site-tested product behavior is the reference.
- Existing Web UI remains shared.
- No new product feature/functionality is authorized in this milestone.
- Scheduler/automation scheduling is not authorized.

## Current architecture decision

Do not copy/fork the entire firmware per board and do not perform a big-bang refactor.

Existing product components remain Core-owned in their current locations. Introduce a minimal Board Support Contract only where needed to remove physical-board assumptions.

Planned board-specific path:

`boards/waveshare_esp32_s3_touch_lcd_5/**`

Planned shared boundary:

`components/board_support/**`

The Core owns product behavior. Board adapters own physical-board identity/resources/safe initialization only.

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

## Waveshare extra capabilities

Current milestone state:

- LCD/LVGL HMI — `RESERVED_NOT_ACTIVE`
- touch application UI — `RESERVED_NOT_ACTIVE`
- onboard RS485/Modbus RTU product transport — `RESERVED_NOT_ACTIVE`
- SD application logging/history — `RESERVED_NOT_ACTIVE`
- RTC application integration — `RESERVED_NOT_ACTIVE`
- CAN/TWAI product integration — `RESERVED_NOT_ACTIVE`
- isolated DI/DO product logic — `RESERVED_NOT_ACTIVE`

These may be hardware-inventoried/conflict-checked or placed into safe board states when required, but they are not current product features.

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
- P2 Repository board structure — `WAITING_DEPENDENCY: P1`
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

## Planned lanes

- L0 Integration/Governance
- L1 Baseline/Architecture
- L2 Waveshare Board Adapter
- L3 Core Portability
- L4 Regression/Web Parity QA
- L5 Safety/Functional QA
- L6 Hardware/HIL
- L7 Release/Branch Hygiene

Only dependency-valid non-overlapping lanes may execute. Planned lanes are not described as active/executing without evidence.

## Immediate execution order

1. P0: freeze exact current behavior, tests, schemas, build assumptions and Core ownership.
2. P3 in parallel: freeze exact Waveshare model/SKU/PCB revision and board resource map.
3. P11 classification-only lane in parallel: classify existing branches/PRs without deletion.
4. After P0: define the minimal board contract and create board structure.
5. Build the same Core on Waveshare with no new product capabilities enabled.
6. Run golden regression and exact hardware/runtime/HIL parity gates.
7. Reconcile and establish `core/stable`.
8. Implement and prove Board Sync Gate.
9. Clean branches only after canonical Core/supported-board lines are stable.

## Current owner input required

Only one input is materially blocking exact board work:

- confirm exact physical target: `ESP32-S3-Touch-LCD-5` or `ESP32-S3-Touch-LCD-5B`, plus PCB revision/rear-board photo if available.

Other feature questions are intentionally deferred because new features are not in scope.

## Completion vocabulary

Do not call the board port complete from code/compile/vendor-demo evidence alone.

Final allowed state after applicable AISH gates:

`FEATURE_PARITY_COMPLETE — WAVESHARE BOARD`