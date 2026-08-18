# Waveshare ESP32-S3-Touch-LCD-5 — Master Plan

Status: `FEATURE-PARITY PORT — NO NEW PRODUCT FEATURES`

Date: 2026-08-18

Repository: `raohassandev/esp32_firmware`

Board branch: `board/waveshare-esp32-s3-touch-lcd-5`

Source baseline: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

AISH baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Canonical architecture/execution plan:
`docs/architecture/CORE_BOARD_EXECUTION_PLAN.md`

Board execution TODO:
`docs/boards/waveshare-esp32-s3-touch-lcd-5/TODO.md`

Resume authority:
`.aish/RESUME.md`

## 1. Scope lock

The existing real-site-tested firmware is the behavioral reference.

This milestone does **not** add new product functionality. Its purpose is to establish a reusable multi-board architecture and run the same existing Core on the Waveshare ESP32-S3-Touch-LCD-5 target.

Required invariants:

- one authoritative shared Product Core;
- one dedicated adapter/integration area for each board;
- one dedicated long-lived `board/<board-id>` branch for each supported board;
- Core fixes/improvements propagate to every supported board;
- board-specific functionality stays confined to that board;
- existing Web UI remains shared and unchanged except defect fixes required for parity;
- existing control/safety/configuration/commissioning/auth/alarm/source-detection/meter/inverter behavior remains authoritative;
- no Scheduler/automation scheduling;
- no completion claim without AISH exact-head evidence.

## 2. Current architectural decision

Do not fork/copy the whole firmware per board.

Use:

```text
Product Core
    |
    v
Minimal Board Support Contract
    |
    +--> Waveshare board adapter
    +--> Future board adapter A
    +--> Future board adapter B
```

The Core owns product behavior. The Board Adapter owns only physical-board identity, resources, safe init and board-specific implementation details.

Existing field-proven components remain in place. No big-bang directory rewrite is authorized.

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

## 4. Waveshare hardware scope

The exact physical SKU/revision must still be frozen.

The board family exposes LCD/touch, RS485, SD/TF, RTC, CAN/TWAI and isolated I/O. These are **hardware capabilities only** in the current milestone.

Current milestone state:

- LCD/LVGL HMI: `RESERVED_NOT_ACTIVE`
- Touch application UI: `RESERVED_NOT_ACTIVE`
- onboard RS485/Modbus RTU product transport: `RESERVED_NOT_ACTIVE`
- SD application logging/history: `RESERVED_NOT_ACTIVE`
- RTC application integration: `RESERVED_NOT_ACTIVE`
- CAN/TWAI product integration: `RESERVED_NOT_ACTIVE`
- isolated DI/DO product logic: `RESERVED_NOT_ACTIVE`

They may be documented, conflict-checked, or placed into safe board states where boot/electrical safety requires it. They are not authorized product features.

## 5. Product parity target

The Waveshare port must preserve the current product surface:

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

Any unexplained behavioral difference is a defect until reconciled.

## 6. Execution phases

1. **P0 — Freeze working Core**
   - exact SHA/toolchain/config/API/Web/safety baseline;
   - Core-vs-board ownership map;
   - current evidence reconciliation.

2. **P1 — Minimal Board Support Contract**
   - board ID/capabilities/safe init/build selection/resource ownership;
   - no speculative peripheral APIs.

3. **P2 — Repository board structure**
   - `components/board_support/**`;
   - `boards/waveshare_esp32_s3_touch_lcd_5/**`;
   - compile-time containment.

4. **P3 — Exact Waveshare hardware baseline**
   - model/SKU/revision/vendor source/pins/resources;
   - safe-state and conflict proof.

5. **P4 — Feature-parity port**
   - build and run existing Core on Waveshare;
   - current behavior only.

6. **P5 — Golden regression**
   - deterministic tests, API/config/Web parity and safety contracts.

7. **P6 — Exact-target runtime proof**
   - boot, PSRAM, stacks, heap, Wi-Fi, HTTP, current Modbus TCP and product runtime.

8. **P7 — HIL/safety equivalence**
   - current fault/recovery/write-gate/control failure surfaces.

9. **P8 — Establish `core/stable`**
   - reconcile approved Core head;
   - one generic merge destination.

10. **P9 — Board Sync Gate**
    - supported-board registry;
    - `core_head` / `synced_core_sha` ledger;
    - stale-board release block.

11. **P10 — Prove propagation**
    - one real Core change propagated end-to-end to every supported board.

12. **P11 — Branch cleanup**
    - canonical/supported/active/trace/archive/delete classification.

13. **P12 — Final parity release**
    - exact-head Quality-360 + target/HIL evidence + no unauthorized feature.

Detailed checkboxes and acceptance gates are in `TODO.md`.

## 7. Parallel lanes

- L0 Integration/Governance
- L1 Baseline/Architecture
- L2 Waveshare Board Adapter
- L3 Core Portability
- L4 Regression/Web Parity QA
- L5 Safety/Functional QA
- L6 Hardware/HIL
- L7 Release/Branch Hygiene

Implementation WIP is limited to useful non-overlapping work. QA starts in parallel when contracts are stable. Planned lanes are not reported as executing without repository/tool evidence.

## 8. Completion rule

Final state may be called:

`FEATURE_PARITY_COMPLETE — WAVESHARE BOARD`

only when:

- the same approved Core is shared;
- board-specific code is contained;
- no new product feature was introduced;
- exact Waveshare build/runtime evidence passes;
- required HIL/bench evidence passes;
- Web/API/config/control/safety behavior remains regression-green;
- Quality-360 required dimensions PASS;
- Core/Board propagation workflow is proven;
- board sync ledger and repo resume context are current.

## 9. Future features

LCD HMI, direct RS485/Modbus RTU, SD logging, RTC, CAN and isolated DI/DO remain future candidates only. Each requires a new Product Owner authorization and AISH work packet before implementation.