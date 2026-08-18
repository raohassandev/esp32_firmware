# Waveshare ESP32-S3-Touch-LCD-5 — Industrial Port Master Plan

Status: `PLANNING / OWNER INPUTS PENDING`

Target repository: `raohassandev/esp32_firmware`

Dedicated board branch: `board/waveshare-esp32-s3-touch-lcd-5`

Creation baseline: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

Governance baseline: `raohassandev/AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

Project quality target: `L5 HIGH_ASSURANCE` for safety/control paths; no production-control completion claim without exact-target and HIL/bench evidence.

Product Owner constraints captured on 2026-08-18:
- preserve the existing PV-DG core behavior as the product core;
- preserve the existing web interface contract/experience unless a separately approved reconciliation changes it;
- port the product to the Waveshare ESP32-S3-Touch-LCD-5 family using a dedicated board branch;
- use onboard interfaces, including RS485 and TF/SD, where they are qualified and appropriate;
- future ESP32-S3 board targets each receive a dedicated board branch while sharing the same core;
- keep branches clean;
- keep restart/resume context in the repository so another chat/session can continue from repository evidence;
- use AISH-OS governance, Quality-360, exact-head evidence and parallel non-overlapping lanes;
- do not use Scheduler/automation scheduling for this project.

## 1. Repository-state finding and branch decision

`main`/`dev` are not the newest product integration state. The current product safety baseline is `phase1-fix`, which contains later field-verified network, alarm, source-detection, resource, control and commissioning work. Therefore this board branch is created from `phase1-fix`, not from `main`, the stale multi-brand branch, or a stale UI branch.

The existing repository has multiple old/superseded/draft branches and PRs. Branch cleanup is a separate controlled task: classify `CANONICAL`, `ACTIVE`, `SUPERSEDED_TRACE`, `ARCHIVE_CANDIDATE`, and `DELETE_CANDIDATE`; never delete trace branches before explicit retention approval and an archive/tag check.

## 2. Hardware baseline to qualify before application integration

Official Waveshare material identifies the board family as ESP32-S3-WROOM-1-N16R8 (16 MB Flash, 8 MB PSRAM), 5-inch RGB LCD, capacitive touch, onboard RS485, CAN, I2C, TF card, RTC, and isolated I/O. The exact SKU/revision must be frozen before driver/configuration work.

Known board-family mappings to verify against the exact schematic/revision:
- LCD: RGB interface; 800x480 on `ESP32-S3-Touch-LCD-5`, 1024x600 on `ESP32-S3-Touch-LCD-5B`.
- Touch: GT911 over shared I2C on GPIO8/GPIO9; touch IRQ GPIO4; reset through CH422G.
- I/O expander: CH422G on shared I2C; also controls LCD/touch/reset/backlight/SD chip-select functions.
- TF card: GPIO11 MOSI, GPIO12 SCK, GPIO13 MISO, chip select via CH422G EXIO4.
- RS485: SP3485; RX GPIO43, TX GPIO44; vendor documentation states automatic TX/RX direction switching.
- CAN/TWAI: TX GPIO15, RX GPIO16 through TJA1051 family transceiver.
- RTC: PCF85063 family on GPIO8/GPIO9 shared I2C.
- Isolated I/O through CH422G: DI0 EXIO0, DI1 EXIO5, DO0 OD0, DO1 OD1.
- Power: USB-C 5 V or DC 7-36 V; official board operating-temperature rating published as 0-65 C.
- RS485/CAN 120-ohm termination is switch-selectable and disabled by default.

No hardware mapping is production-authoritative until the exact physical SKU, PCB revision and official schematic are recorded and the minimal vendor baseline is exercised.

## 3. Upstream-first qualification gate

Before custom board/peripheral code, qualify in AISH order:
1. existing project implementation and interfaces;
2. ESP-IDF v6.x native driver/example where applicable;
3. ESP Component Registry / Espressif-maintained components;
4. chip-vendor reference where required;
5. exact Waveshare board schematic, pin map and official ESP-IDF example;
6. qualified internal reusable component, if one exists;
7. reputable third party only if higher-authority options fail requirements;
8. custom implementation only with `CUSTOM_REQUIRED` rationale.

The Waveshare vendor examples are a fault-isolation baseline, not project acceptance. Their framework/dependency assumptions must be qualified against this project's ESP-IDF/toolchain rather than copied blindly. All adopted dependencies must be pinned/locked with provenance and license recorded.

Initial qualification records are required for:
- RGB LCD / panel driver;
- GT911 touch;
- CH422G I/O expander and board glue;
- LVGL integration;
- UART/RS485 transport and Modbus RTU stack;
- SDSPI/SDMMC + FATFS path chosen for this board;
- PCF85063 RTC;
- TWAI/CAN;
- isolated DI/DO;
- USB/console/debug path.

## 4. Target architecture: one core, board-specific capability layer

The project must not become a set of copied firmware forks. The branch may contain board-specific integration, but product logic stays portable.

Introduce a board-support boundary with these responsibilities:
- immutable board identity and revision;
- capability declaration;
- pin/resource ownership;
- peripheral initialization and safe shutdown;
- display/touch/backlight/expander glue;
- RS485, SD, RTC, CAN and isolated-I/O physical adapters;
- resource and health telemetry for board peripherals.

Business/safety layers remain above the board boundary:
- configuration and migration;
- meter/inverter profiles;
- control engine and safety policy;
- source detection;
- commissioning gates;
- authentication/authorization;
- alarms/audit/provenance;
- web APIs and embedded web UI.

A board adapter may expose capabilities; it must not duplicate or bypass safety/business rules.

## 5. Communication architecture

### Modbus
Retain the current Modbus TCP path. Add onboard RS485 as an additional transport, not a silent replacement.

Target contract:
`meter/inverter manager -> transport-neutral Modbus request contract -> Modbus TCP OR Modbus RTU -> physical peer`

RS485/Modbus RTU gates:
- vendor UART baseline on GPIO43/44 passes on exact board;
- baud/parity/stop bits configurable and validated;
- CRC, timeout, malformed-frame and wrong-unit handling tested;
- bus turnaround and auto-direction timing measured;
- termination/biasing/wiring documented;
- scheduler prevents overlapping RTU transactions;
- read-only device qualification precedes any production write;
- every write path remains gated by profile/manual evidence plus command readback/confirmation;
- stale/unavailable communication fails closed according to existing control policy.

### CAN/TWAI
CAN is a board capability but not automatically part of the release-critical path. Keep disabled/unowned until a product requirement activates it. When activated, use exact-target TWAI + transceiver/termination evidence.

## 6. Display/touch/local HMI architecture

The browser-based web interface remains the existing web product. The 5-inch LCD cannot be treated as a browser surface.

If local on-device HMI is required, build it as a native LVGL client over the same authoritative application state/model and safety semantics. It must never maintain an independent control truth or bypass server/engineering authorization.

Display/touch gates:
- exact SKU resolution/timings frozen;
- official vendor minimal RGB+touch demo passes first;
- qualified LVGL version and display/touch component versions pinned;
- executable render testing at exact resolution;
- real-panel touch, DMA, PSRAM, refresh, rotation and backlight evidence;
- display failure/touch storm must not starve control/network tasks;
- local UI state must visibly distinguish measured, commanded, stale, unavailable and unknown states;
- no local action may enable physical writes without the same commissioning/authority gates as the web path.

## 7. Memory, timing and resource isolation

The existing product deliberately uses 8 MB PSRAM to protect HTTP/JSON reliability. The RGB display will consume meaningful PSRAM/DMA resources and therefore requires a new resource budget before integration.

Required measurements:
- framebuffer allocation and location;
- internal DMA-capable heap floor;
- total/free/minimum heap and fragmentation;
- task stack high-water marks;
- control-loop period/jitter;
- meter/RS485 poll latency and bus utilization;
- HTTP latency/concurrency under active display refresh;
- LVGL frame/refresh timing;
- watchdog margin;
- SD logging latency under worst-case writes.

No display/SD/UI operation may block the safety/control loop. Use queues/worker tasks and bounded work where storage or rendering can stall.

## 8. SD/TF, RTC and isolated I/O

### SD/TF
Use a qualified ESP-IDF-native storage path adapted to CH422G-controlled CS. Treat SD as optional/non-authoritative for control. Missing/full/corrupt/removed card must not stop control or web service. Logging writes must be buffered/queued and power-loss behavior tested.

### RTC
Use PCF85063 as a time source with an explicit time-quality model. Distinguish `RTC_VALID`, `NETWORK_SYNCED`, `UNSYNCED/INVALID`. Never invent a valid timestamp after RTC battery loss or invalid state.

### Isolated DI/DO
Treat DI/DO as physical I/O with safe startup/reset states. Output command is not proof of field actuation unless independent feedback exists. Production use requires voltage/load/boot/reset/fault evidence and a defined safe state.

## 9. Safety architecture and non-negotiable invariants

Until qualification proves otherwise:
- automatic control starts disabled;
- uncommissioned/unknown source or measurement state fails closed;
- stale meter evidence cannot drive optimistic control;
- unqualified inverter profile cannot issue a production write;
- communication loss drives the existing safe behavior;
- boot/init failure halts safely rather than creating uncontrolled output;
- UI, SD, RTC, touch, Wi-Fi and web faults cannot elevate control authority;
- configuration migration may not erase commissioned Wi-Fi/device/control state;
- no `erase-flash` or destructive persistence step in normal update/commissioning;
- no hardware-dependent task is marked complete from host tests, compile, screenshot or vendor demo alone.

## 10. Parallel AISH execution lanes

Lanes are bounded roles, not background autonomous processes. Activate only dependency-valid work. Default WIP: maximum three concurrent implementation lanes plus independent QA/review/integration, adjusted by the current bottleneck.

Initial lane map:
- L0 — Orchestration / integration / branch-context authority. Owns shared manifests, build configuration, integration ordering and `.aish` state.
- L1 — Upstream/vendor qualification. Owns exact schematic/demo/dependency qualification and pin/resource evidence.
- L2 — Board BSP + display/touch. Owns board-support files and local display/touch adapter only.
- L3 — RS485 + Modbus RTU transport. Owns RTU transport and deterministic protocol tests only.
- L4 — SD + RTC + isolated I/O + optional CAN capability adapters. Activated as dependencies become ready; may split if write scopes are disjoint.
- L5 — Core portability/integration. Owns transport-neutral seams and removal of DevKit-only assumptions; shared-core files only under integration-owner control.
- L6 — Web/UI regression. Owns browser fixtures, API/web parity checks and visual regression; does not alter safety logic.
- L7 — Functional/safety QA. Independent positive/negative/fault-injection tests and Quality-360 evidence.
- L8 — Hardware/HIL/reliability. Exact-board flash, electrical/protocol tests, soak/reboot/power-fault/resource measurements.
- L9 — Release/audit. Exact-head evidence reconciliation, branch hygiene and final completion decision.

`Idle authorized capacity + compatible READY work = ORCHESTRATION_DEFECT`. If hardware/CI is the bottleneck, do not create more coder WIP; move capacity to deterministic tests, review, fixture preparation, documentation of evidence, or blocker removal.

## 11. Work-packet dependency DAG / milestone order

### WP0 — Canonical baseline and governance bootstrap
Acceptance:
- dedicated branch exists from exact latest approved core baseline;
- branch/base SHA recorded;
- repo-resident master plan and resume checkpoint exist;
- stale branch/PR inventory classified without destructive cleanup;
- exact board SKU/revision and lab-resource gaps visible.

### WP1 — Exact Waveshare upstream baseline
Acceptance:
- exact SKU/revision/schematic recorded;
- official vendor repository/example version pinned;
- selected ESP-IDF compatibility decision recorded;
- minimal vendor tests for I2C, RS485, SD, RTC, isolated I/O, display/touch and (if required) CAN have explicit states;
- no vendor-demo pass mislabeled as project pass.

### WP2 — Board-support abstraction + exact target build
Acceptance:
- board descriptor/pins/capabilities implemented;
- current core compiles against the Waveshare target configuration;
- DevKit-specific assumptions isolated;
- target build warnings/resource deltas reviewed;
- exact-build evidence stored.

### WP3 — Display/touch foundation
Acceptance:
- RGB panel + GT911 + CH422G bring-up on exact hardware;
- executable local-HMI render path if local HMI is in scope;
- touch/calibration/backlight/refresh/PSRAM evidence;
- UI failure cannot disturb control cadence.

### WP4 — RS485 + Modbus RTU read-only integration
Acceptance:
- raw UART/RS485 baseline pass;
- deterministic Modbus RTU parser/transaction tests;
- real meter/device read on bench;
- timeout/CRC/wrong-unit/no-peer paths pass;
- current Modbus TCP path remains regression-green.

### WP5 — SD/RTC/isolated-I/O capability integration
Acceptance:
- SD mount/read/write/remove/full/corrupt/power-cycle behavior evidenced;
- RTC valid/invalid/battery-loss behavior evidenced;
- DI/DO safe boot/reset and input/output bench behavior evidenced;
- optional CAN packet remains N/A unless activated by requirement.

### WP6 — Core/application integration
Acceptance:
- managers select TCP/RTU without duplicating profile/control logic;
- safety/source detection/commissioning contracts unchanged or deliberately versioned;
- configuration schema/migration protects existing commissioned data;
- all host/source contracts pass;
- exact Waveshare build passes.

### WP7 — Web parity and local-HMI product integration
Acceptance:
- current approved web routes/API behavior remain regression-green;
- browser full-app fixture/screenshots pass desktop/mobile/light/dark as applicable;
- local HMI, if required, uses authoritative state and displays failure/unknown states truthfully;
- accessibility/touch-size/readability gates pass for each relevant surface.

### WP8 — Physical write/control qualification
Acceptance:
- exact inverter manuals/profile maps frozen;
- manual/simulator/bench evidence reconciled;
- write authorization negative tests pass;
- first-write + readback/confirmation path passes on real peer hardware;
- communication-loss/stale-source/reset/brownout paths demonstrate fail-safe behavior;
- automatic control still not enabled merely because this packet passes.

### WP9 — Full HIL/fault-injection/reliability campaign
Acceptance includes, where applicable:
- meter/inverter disconnect/reconnect;
- RS485 noise/CRC/timeout/bus-contention behavior;
- wrong slave/register/scale/word-order protections;
- Wi-Fi absent/recovery AP behavior;
- HTTP socket saturation while display and control are active;
- touch storm/UI redraw load;
- SD missing/full/corrupt/remove-during-write;
- RTC invalid/time-loss;
- repeated reboot and watchdog paths;
- power interruption during persistence/update;
- heap/stack/resource stability and control-loop jitter under worst representative load;
- environmental/temperature/power-input tests proportional to production use;
- elapsed soak evidence actually observed, never fabricated.

### WP10 — Shadow/pilot field release
Recommended release progression:
`MONITOR_ONLY -> READ_ONLY_FIELD_TELEMETRY -> MANUAL_GUARDED_WRITE -> HIL_CONTROL_PASS -> SHADOW/PILOT -> PRODUCTION_AUTHORITY`

Production authority requires Product Owner/customer acceptance where applicable and exact-head evidence. A useful default to freeze later is at least a 24 h bench soak plus a 72 h shadow/site pilot, but duration is not evidence until actually observed and may be raised by the safety review.

### WP11 — Release, cleanup and future-board baseline
Acceptance:
- all REQUIRED Quality-360 dimensions PASS at exact release head;
- release artifact/version/rollback/configuration documented;
- board branch has no stale work-packet branches;
- reusable core fixes are integrated into the canonical shared core rather than trapped in one board branch;
- branch template/naming rule for future boards documented.

## 12. Quality-360 applicability for this milestone

Default `REQUIRED`:
- FUNCTIONAL_CORRECTNESS
- USER_WORKFLOW_UX (web and local HMI if in scope)
- VISUAL_DESIGN (web regression; local HMI if in scope)
- RESPONSIVE_ADAPTIVE (web)
- ACCESSIBILITY (relevant web/local interaction)
- SECURITY
- AUTHENTICATION_AUTHORIZATION_RBAC
- DATA_INTEGRITY_TRANSACTIONAL
- API_CONTRACT_INTEGRATION
- PERFORMANCE_CAPACITY
- RELIABILITY_FAILURE_HANDLING
- RECOVERY_ROLLBACK_BACKUP
- OBSERVABILITY_DIAGNOSTICS
- COMPATIBILITY_UPGRADE_MIGRATION
- MAINTAINABILITY_ARCHITECTURE
- DEPENDENCY_SUPPLY_CHAIN
- DEPLOYMENT_CONFIGURATION_OPERATIONS
- DOCUMENTATION_HANDOVER
- EMBEDDED_TARGET_HARDWARE
- REGULATORY_SAFETY_COMPLIANCE

`PRIVACY_SENSITIVE_DATA` must be explicitly assessed but may be N/A if no new sensitive-data surface is introduced. CAN is `NOT_APPLICABLE` until a product requirement activates it.

## 13. Current material gaps / release blockers

G1. Canonical repo state is not obvious from `main`; README points at a stale branch.
G2. `phase1-fix` contains the newest core, but board abstraction is not yet the repository's organizing model.
G3. Current `sdkconfig.defaults` explicitly encodes DevKitC-1 assumptions and states there is no Ethernet; this must become board-target configuration rather than a universal fact.
G4. Current component set has Modbus TCP as the transport component; onboard RS485/Modbus RTU is not yet a first-class project transport.
G5. No project display/touch/local-HMI integration exists for this board.
G6. SD, RTC, CH422G isolated I/O and CAN are not integrated into the product architecture.
G7. RGB framebuffer/LVGL resource demand can compete with PSRAM/heap/network/control paths previously stabilized under resource pressure.
G8. Official Waveshare examples and the product project's ESP-IDF baseline must be compatibility-qualified and pinned.
G9. The exact physical SKU (`-5` vs `-5B`) and PCB revision are not yet recorded.
G10. The operating environment/safety suitability of a vendor development board must not be assumed from functional demos; official published board temperature is 0-65 C and no project-specific EMC/surge/environmental acceptance evidence exists yet.
G11. The draft operator-UI branch is diverged from the latest product core; it cannot be used as a safe base without reconciliation.
G12. Secure OTA work exists on stale/draft branches and must be separately reconciled before it is considered part of this board release.
G13. Existing production write/control qualification remains profile/device-specific; a new board port does not automatically promote unverified inverter control.
G14. Branch/PR inventory contains stale and superseded work; branch hygiene requires a controlled cleanup pass.
G15. Repository-resident execution/resume state needs a single canonical new-board checkpoint so future chats do not reconstruct state from old chat messages.

## 14. Branch model for this and future boards

Long-lived:
- canonical shared-core integration branch: to be formally resolved from current `phase1-fix` lineage;
- `board/waveshare-esp32-s3-touch-lcd-5` for this target;
- future `board/<vendor>-<model>-<revision-or-sku>` branches created from a tagged/canonical shared-core baseline, never from a sibling board branch.

Short-lived work-packet branches only when parallel authorship/PR review requires them:
- `wp/ws5-bsp-*`
- `wp/ws5-rs485-*`
- `wp/ws5-display-*`
- `wp/ws5-storage-*`
- `wp/ws5-qa-*`

Delete short-lived branches after verified merge unless retained for a documented trace reason. Core fixes discovered on a board branch must be promoted to the canonical core and then synchronized back to active board branches; do not let core behavior permanently diverge per board.

## 15. Repository-resident context / chat-resume protocol

Canonical restart file: `.aish/RESUME.md` on this branch.

At each material integration checkpoint it must record:
- branch and exact HEAD;
- AISH governance SHA/version;
- current milestone/work packet states;
- last exact-target build/test/HIL evidence;
- current bottleneck;
- READY queue;
- blockers and owner-only actions;
- next safe execution step;
- links/paths to evidence;
- unresolved decisions.

New chat/session restart sequence:
1. read AISH-OS `AGENTS.md` and applicable standards;
2. inspect this branch and exact HEAD;
3. read this master plan;
4. read `.aish/RESUME.md`;
5. verify repository evidence before status claims;
6. recompute READY/bottleneck/WIP;
7. continue execution from the highest-value safe READY packet.

Do not use Scheduler for this project. Parallel lanes are recomputed and managed on each live execution/control cycle; no claim of background autonomous workers is allowed.

## 16. Owner decisions/resources required before board-dependent implementation

1. Confirm exact physical SKU: `ESP32-S3-Touch-LCD-5` (800x480, SKU 28117) or `ESP32-S3-Touch-LCD-5B` (1024x600, SKU 28151), plus PCB revision/photo of rear silkscreen if available.
2. Confirm whether onboard RS485 is the preferred primary field path while Modbus TCP remains supported, or whether this target must use RS485 only.
3. Confirm local 5-inch LCD scope: full native operator/engineering HMI, limited local status/commissioning HMI, or display bring-up only while the web UI remains the main operator interface.
4. Confirm whether this Waveshare development board is intended for final field production/control authority or for prototype/pilot qualification before a hardened custom board.
5. Identify available physical bench resources: exact Waveshare board, PSU, RS485 meter/inverter or simulator, USB-RS485 adapter, TF card, and any CAN/logic-analyzer/oscilloscope/HIL equipment.
6. Confirm the environmental envelope expected in deployment (ambient temperature/enclosure/24V supply quality/EMC or surge expectations) if this board will be used in the field.

All other routine decomposition, branch/PR mechanics, upstream selection, WIP scaling, test routing and reversible engineering decisions remain engineering responsibility under AISH-OS.