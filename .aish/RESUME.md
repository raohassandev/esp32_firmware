# AISH Resume — Waveshare ESP32-S3-Touch-LCD-5

Updated: 2026-08-18

Lifecycle: `PLANNING / OWNER INPUTS PENDING`

## Canonical execution target

Repository: `raohassandev/esp32_firmware`

Branch: `board/waveshare-esp32-s3-touch-lcd-5`

Branch origin: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

Planning commit: `326e97122a94f802c01f8cc1080d6da80cbf74cb`

AISH baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Master plan: `docs/boards/waveshare-esp32-s3-touch-lcd-5/MASTER_PLAN.md`

## Current verified repository facts

- `main`/`dev` are behind the actual product-development line.
- `phase1-fix` is the selected board-port baseline and includes later field-verified safety/network/control fixes.
- The existing target configuration has ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 240 MHz CPU, 1 kHz FreeRTOS tick and resource tuning that was originally justified for a DevKitC-1 N16R8.
- The current components include Modbus TCP but no first-class project Modbus RTU/RS485 component.
- Existing web, control, safety, source-detection, commissioning and profile logic are to remain authoritative shared product layers.
- A draft operator-UI refinement branch is diverged from the current safety baseline; it is not the base for this board port.

## Waveshare upstream facts recorded for planning

Family: Waveshare ESP32-S3-Touch-LCD-5 / -5B.

Known official family capabilities: ESP32-S3-WROOM-1-N16R8, RGB LCD, GT911 touch, CH422G expander, SP3485 RS485, TF/SD, PCF85063 RTC, TJA1051 CAN, isolated DI/DO, USB-C 5V and DC 7-36V input.

Exact physical SKU and PCB revision are still `OWNER_INPUT_PENDING`; no board-dependent code may be called verified before this is frozen.

## Work packet state

- WP0 Canonical baseline/governance bootstrap — `IN_PROGRESS`: branch + master plan + resume context created; owner inputs and branch inventory classification remain.
- WP1 Exact Waveshare upstream baseline — `READY_AFTER_OWNER_SKU`: official docs/repo located; exact SKU/revision and physical demo evidence pending.
- WP2 Board-support abstraction — `WAITING_DEPENDENCY: WP1`
- WP3 Display/touch foundation — `WAITING_DEPENDENCY: WP1/WP2`
- WP4 RS485/Modbus RTU read-only — `WAITING_DEPENDENCY: WP1/WP2`
- WP5 SD/RTC/isolated-I/O — `WAITING_DEPENDENCY: WP1/WP2`
- WP6 Core/application integration — `WAITING_DEPENDENCY: WP2/WP4`
- WP7 Web parity/local HMI — `WAITING_DEPENDENCY: WP2/WP3/WP6`
- WP8 Physical write/control qualification — `WAITING_DEPENDENCY: WP4/WP6 + real peer hardware/manual evidence`
- WP9 HIL/fault/reliability — `WAITING_DEPENDENCY: integrated RC`
- WP10 Shadow/pilot — `ENVIRONMENT_WAIT until RC + owner/site authority`
- WP11 Release/cleanup/future-board baseline — `WAITING_DEPENDENCY`

## Planned lanes (not background workers)

L0 integration/governance; L1 upstream qualification; L2 BSP/display/touch; L3 RS485/Modbus RTU; L4 SD/RTC/DI-DO/CAN; L5 core portability; L6 web regression; L7 independent QA/safety; L8 hardware/HIL/reliability; L9 release/audit.

Only lanes with READY work and non-overlapping write scopes may be activated. No Scheduler is authorized. A lane is not reported as executing without repository/tool evidence of execution.

## Current bottleneck

Exact physical board identity/revision and intended production role are not frozen. These decisions affect display timings, environmental acceptance, HMI scope and field-control release depth.

## Owner inputs required

1. Exact board: `ESP32-S3-Touch-LCD-5` 800x480 / SKU 28117 OR `ESP32-S3-Touch-LCD-5B` 1024x600 / SKU 28151; provide PCB revision or rear-board photo if possible.
2. RS485 policy: primary field transport + keep TCP, or RS485-only on this board.
3. Local LCD scope: full native HMI, limited status/commissioning HMI, or bring-up only.
4. Deployment role: final production field controller vs prototype/pilot before custom hardened PCB.
5. Available bench equipment/peer devices.
6. Field environmental envelope if production use is intended.

## Next safe execution after owner inputs

1. Refresh exact branch heads and AISH main head.
2. Update this checkpoint with confirmed SKU/revision and deployment role.
3. Pin the exact Waveshare vendor repository/example revision and record upstream qualification decisions.
4. Run/prepare the vendor minimal-baseline matrix for display/touch, I2C/CH422G, RS485, SD, RTC and isolated I/O.
5. Create the dependency DAG/control-plane records and activate only non-overlapping READY implementation/QA lanes.
6. Start the board-support skeleton and exact Waveshare target build without enabling production physical writes.

## Completion vocabulary

Use AISH evidence states exactly. In particular, compile/host/simulation/vendor-demo states may not be called project hardware complete. Hardware-dependent work remains `HARDWARE_VALIDATION_PENDING` until exact-target/HIL evidence closes the required failure surface.