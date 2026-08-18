# Waveshare ESP32-S3-Touch-LCD-5 — Master Plan

Status: `FEATURE-PARITY PORT + AUTHORIZED BOARD-LOCAL HMI — NO NEW CORE PRODUCT FEATURES`

Date: 2026-08-18

Repository: `raohassandev/esp32_firmware`

Board branch: `board/waveshare-esp32-s3-touch-lcd-5`

Source baseline: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

AISH baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Canonical architecture/execution plan: `docs/architecture/CORE_BOARD_EXECUTION_PLAN.md`

Board execution TODO: `docs/boards/waveshare-esp32-s3-touch-lcd-5/TODO.md`

Screen execution TODO: `boards/waveshare_esp32_s3_touch_lcd_5/screen/SCREEN_TODO.md`

Resume authority: `.aish/RESUME.md`

## 1. Scope lock

The existing real-site-tested firmware is the behavioral reference.

This milestone does **not** add new Core/backend/control functionality. Its purposes are:

1. establish the reusable Core + board-specific architecture;
2. run the same existing product behavior on the Waveshare target;
3. implement the Product Owner-authorized Waveshare local LCD/touch HMI as a **board-specific presentation of existing functionality using the existing backend/API authority**.

Required invariants:

- one authoritative shared Product Core;
- one dedicated adapter/integration area for each board;
- one dedicated long-lived `board/<board-id>` branch for each supported board;
- Core fixes/improvements propagate to every supported board;
- board-specific functionality stays confined to that board;
- existing Web UI remains shared and unchanged except defect fixes required for parity;
- existing control/safety/configuration/commissioning/auth/alarm/source-detection/meter/inverter behavior remains authoritative;
- local HMI may not duplicate backend/business/control truth;
- current HMI milestone is read-only: no new control/write callbacks;
- no Scheduler/automation scheduling;
- no completion claim without AISH exact-head evidence.

## 2. Architecture

Do not fork/copy the whole firmware per board and do not perform a big-bang Core rewrite.

```text
Product Core / Existing Backend Authority
                  |
                  v
       Minimal Board Support Contract
                  |
       +----------+----------+
       |                     |
       v                     v
Waveshare Adapter       Future Board Adapter
       |
       +--> board-local LCD/touch presentation
            consumes existing API/state contracts
```

The Core owns product behavior. Board adapters own physical identity/resources/safe init/board-local implementation. The Waveshare HMI owns presentation only.

Existing field-proven Core components stay in their current locations unless a bounded portability seam is actually required.

## 3. Core propagation rule

Generic fixes follow:

```text
work/core/<issue>
      -> verify
      -> core/stable
      -> Board Sync Gate
      -> board/waveshare...
      -> every other SUPPORTED board
      -> per-board exact build / risk-selected regression
```

A generic Core defect may not remain permanently fixed only in one board branch.

Board-only changes remain on `board/<id>` and are never merged wholesale into Core or another board branch.

## 4. Waveshare capability scope

The exact physical SKU/revision must still be frozen.

Current milestone state:

- LCD/LVGL HMI: `AUTHORIZED_BOARD_SPECIFIC_PRESENTATION` — read-only existing-product parity surface;
- touch navigation: `AUTHORIZED_BOARD_SPECIFIC_PRESENTATION` — no new control/write semantics;
- onboard RS485/Modbus RTU product transport: `RESERVED_NOT_ACTIVE`;
- SD application logging/history: `RESERVED_NOT_ACTIVE`;
- RTC application integration: `RESERVED_NOT_ACTIVE`;
- CAN/TWAI product integration: `RESERVED_NOT_ACTIVE`;
- isolated DI/DO product logic: `RESERVED_NOT_ACTIVE`.

The screen workspace is:

`boards/waveshare_esp32_s3_touch_lcd_5/screen/**`

Its backend contracts are existing routes only: `/api/live`, `/api/status`, `/api/meters`, `/api/inverters`, `/api/telemetry`, `/api/operator/events`, `/api/operator/alarms`.

No screen-specific backend route or control policy is authorized.

## 5. Product parity target

The Waveshare port must preserve:

- safe boot behavior;
- current configuration and migration semantics;
- current Wi-Fi/network behavior;
- current Modbus TCP transport;
- current meter/inverter profiles and behavior;
- current source detection;
- current control and fail-safe behavior;
- current commissioning/write gates;
- current authentication/authorization;
- current alarms/audit/provenance;
- current HTTP APIs;
- current Web UI/browser workflows;
- current persistence expectations;
- current automatic-control default/authority semantics.

The board-local screen may visualize those existing states but cannot redefine them.

Any unexplained Core behavioral difference is a defect until reconciled.

## 6. Screen-specific safety rules

- `null`, missing, stale or unavailable measurement is never rendered as measured `0`.
- source attribution shown to the operator comes from backend fail-closed `source.attributed_to`.
- control mode/inhibit reasons use backend wording.
- no local UI callback may write configuration or command plant equipment in the current milestone.
- one failed API contract degrades only its owned surface where possible.
- screen runtime creates no hidden scheduler/task; the qualified board integration owns cadence/locking.
- large bounded snapshots may not consume unsafe LVGL task stack.
- display/touch/API failure may not change control authority.

## 7. Execution phases

1. **P0 — Freeze working Core** — exact SHA/toolchain/config/API/Web/safety baseline and Core-vs-board ownership.
2. **P1 — Minimal Board Support Contract** — board ID/capabilities/safe init/build selection/resource ownership; no speculative peripheral APIs.
3. **P2 — Repository board structure** — shared board boundary + `boards/waveshare_esp32_s3_touch_lcd_5/**` containment.
4. **P3 — Exact Waveshare hardware baseline** — model/SKU/revision/vendor source/pins/resources/safe-state proof.
5. **P4 — Feature-parity board port** — same Core on Waveshare; current behavior only.
6. **P5 — Golden regression** — deterministic tests, API/config/Web parity and safety contracts.
7. **P6 — Exact-target runtime proof** — boot, PSRAM, stacks, heap, Wi-Fi, HTTP, current Modbus TCP and product runtime.
8. **P7 — HIL/safety equivalence** — current fault/recovery/write-gate/control failure surfaces.
9. **P8 — Establish canonical shared Core line** — one generic merge destination after baseline reconciliation.
10. **P9 — Board Sync Gate** — supported-board registry plus `core_head`/`synced_core_sha` release gate.
11. **P10 — Prove propagation** — one real Core change propagated to every supported board.
12. **P11 — Branch cleanup** — canonical/supported/active/trace/archive/delete classification.
13. **P12 — Final parity release** — exact-head Quality-360 + target/HIL evidence + no unauthorized feature.

Screen sub-plan S0-S6 is authoritative in `screen/SCREEN_TODO.md` and runs in parallel where dependencies allow.

## 8. Current screen implementation checkpoint

Software source now exists for:

- bounded existing-API models/parsers;
- provider-injected read-only runtime refresh bridge;
- shared LVGL widgets;
- Overview;
- Grid/Meters;
- Solar/Inverters;
- Alarms/Events;
- Readiness;
- touch navigation shell;
- exact pure-C display timing/pin profiles for both 800x480 and 1024x600 variants;
- host parser/profile tests and screen isolation source contract;
- dedicated board-screen CI workflow.

The root/default firmware build still intentionally excludes this screen component until exact-target display/LVGL qualification is complete.

## 9. Waveshare upstream qualification

Pinned review baseline:

`waveshareteam/ESP32-S3-Touch-LCD-5@a7b179dbfccea8121c88770d8a3c53e5a84b1024`

The official LVGL v9 example is the hardware bring-up reference. It is not product acceptance and its dependency ranges must be qualified against the project's ESP-IDF 6.0.1 before board integration is enabled.

Both vendor resolution/timing profiles are recorded without selecting a default physical SKU. Physical SKU/revision remains an exact-target gate.

## 10. Parallel lanes

- L0 Integration/Governance
- L1 Baseline/Architecture
- L2 Waveshare Board Adapter + display/touch integration
- L3 Core Portability
- L4 Regression/Web + screen contract QA
- L5 Safety/Functional QA
- L6 Hardware/HIL/resource proof
- L7 Release/Branch Hygiene

Implementation WIP is limited to useful non-overlapping work. Planned lanes are not reported as executing without repository/tool evidence.

## 11. Completion rules

`LOCAL_SCREEN_COMPLETE — EXACT WAVESHARE TARGET` requires:

- exact physical SKU/revision;
- qualified/pinned display/LVGL/touch dependencies on ESP-IDF 6.0.1;
- exact board compile/flash/render/touch evidence;
- API/runtime failure behavior evidence;
- PSRAM/DMA/heap/stack/watchdog/control-jitter evidence;
- display/touch fault isolation from control authority;
- applicable Quality-360 gates PASS at exact head.

`FEATURE_PARITY_COMPLETE — WAVESHARE BOARD` additionally requires:

- the same approved shared Core;
- board-specific containment;
- exact Waveshare board runtime/HIL evidence;
- Web/API/config/control/safety regression green;
- Core/Board propagation workflow proven;
- board sync ledger and repo resume context current.

## 12. Future features outside current authorization

Direct onboard RS485/Modbus RTU product transport, SD logging/history, RTC application behavior, CAN/TWAI product behavior and isolated DI/DO product logic remain future candidates. Each requires explicit Product Owner authorization and a bounded AISH work packet before implementation.
