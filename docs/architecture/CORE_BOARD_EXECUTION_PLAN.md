# Core + Board Architecture — Execution Plan

Status: `APPROVED SCOPE — FEATURE PARITY ONLY`

Date: 2026-08-18

Repository: `raohassandev/esp32_firmware`

Current proven product baseline: `phase1-fix@3c486f0eb5595668c78af7491fa7a1550ab2bc71`

Current Waveshare board branch: `board/waveshare-esp32-s3-touch-lcd-5`

Governance: `AISH-OS@acaba0f5a06d2893d350cafd9d949cc068d6d6f1`

## 1. Product Owner scope lock

This milestone is **not a feature-development milestone**.

The existing firmware has already been exercised on a real site and is the behavioral reference. The goal is to restructure the product so the same proven Core can run on multiple ESP32-S3 boards without duplicating product logic.

Required invariants:

1. There is exactly one authoritative shared Product Core.
2. Every supported board has a dedicated board-specific implementation area and a dedicated long-lived board branch.
3. A Core bug fix or approved Core improvement must propagate to every supported board.
4. Board-specific behavior must remain confined to that board and must not silently change another board.
5. The current Web UI remains the shared Web UI.
6. Existing control, safety, configuration, commissioning, authentication, alarms, source detection, meter/inverter behavior and operational semantics remain unchanged unless an independently justified defect is discovered.
7. No new customer-facing feature or new field-control behavior is authorized in this milestone.
8. Scheduler/automation scheduling is not used for project execution.
9. Repository-resident context remains authoritative so a new chat/agent can resume from Git evidence.
10. No task is called complete without AISH-required exact-head evidence.

## 2. Explicitly out of scope for this milestone

The Waveshare board contains useful hardware, but availability of a peripheral is not authorization to add product functionality.

The following are **reserved capabilities, not current product scope**:

- native LCD/LVGL operator HMI;
- touch-driven local commissioning;
- direct onboard RS485/Modbus RTU product transport;
- SD-card application logging/history/export;
- RTC-backed application timekeeping;
- CAN/TWAI product integration;
- isolated DI/DO product logic;
- new control algorithms or safety policies;
- new inverter/meter profiles;
- new Web pages/workflows;
- new OTA/auth/session behavior;
- unrelated modernization/refactoring.

These capabilities may be identified, documented, placed in safe electrical/default states, or exercised with isolated vendor/minimal demos for board qualification. They must not become active product functionality without a later Product Owner scope decision and a new AISH work packet.

## 3. Architecture model

The target model is:

```text
+-------------------------------------------------------------+
|                    PRODUCT CORE                             |
|                                                             |
| app_core                                                    |
| config_manager / persistence / migration                    |
| meter + inverter managers/profiles                          |
| control_engine + safety_manager                             |
| source_detection / commissioning gates                      |
| alarms / audit / provenance                                 |
| network + web APIs + existing Web UI                        |
| common diagnostics/tests                                    |
+---------------------------+---------------------------------+
                            |
                            | stable Board Support Contract
                            v
+-------------------------------------------------------------+
|                 BOARD SUPPORT INTERFACE                     |
| board identity / capability declaration                     |
| safe init/shutdown hooks                                    |
| board configuration / resource ownership                    |
| optional physical service adapters                          |
+---------------------------+---------------------------------+
                            |
          +-----------------+------------------+
          |                                    |
          v                                    v
+--------------------------+       +--------------------------+
| Board Adapter A          |       | Board Adapter B          |
| Waveshare 5              |       | Future ESP32-S3 board    |
| pins/resources           |       | pins/resources           |
| board-safe init          |       | board-safe init          |
| board-only capabilities  |       | board-only capabilities  |
+--------------------------+       +--------------------------+
```

### Core rule

The Product Core owns **what the controller does**.

### Board rule

A Board Adapter owns **how the selected physical board provides the resources needed by the Core**.

A board adapter must never own business rules, safety policy, register semantics, user authorization, commissioning policy, alarm meaning or control calculations.

## 4. Progressive migration rule — no big-bang rewrite

The existing codebase is already field-proven. Do not reorganize stable code merely to make directories look cleaner.

Existing components remain Core-owned in their current locations unless a change is required to remove a proven board dependency.

Initially classify as Core-owned:

- `components/app_core/**`
- `components/config_manager/**`
- `components/control_engine/**`
- `components/safety_manager/**`
- `components/source_detection/**`
- `components/meter_manager/**`
- `components/meter_profiles/**`
- `components/inverter_manager/**`
- `components/profile_manager/**`
- `components/modbus_tcp/**`
- `components/network_manager/**`
- `components/web_server/**`
- existing `web/**`
- common tests and production contracts

Introduce only the minimum new boundary required for board portability:

```text
components/
  board_support/
    include/
      board_support.h
      board_capabilities.h
      board_identity.h
    board_support.c

boards/
  waveshare_esp32_s3_touch_lcd_5/
    CMakeLists.txt
    Kconfig
    board_config.h
    board_pins.h
    board_impl.c
    sdkconfig.defaults
    README.md

  <future-board-id>/
    ...same contract...
```

Names may be adjusted during implementation if the existing ESP-IDF build layout requires it, but ownership boundaries must remain equivalent.

## 5. Stable Board Support Contract

The first Board Support Contract must be intentionally small. It should expose only what the current Core actually requires.

Minimum contract:

- board ID;
- board revision/family metadata when known;
- feature/capability flags;
- board-safe early initialization;
- board-safe shutdown/failure hook where required;
- resource/pin ownership description;
- board-specific sdkconfig/build defaults;
- diagnostic identity for logs/API/version reporting.

Do **not** add abstract APIs for RS485, SD, RTC, CAN, LCD or touch until product code actually needs those capabilities. Premature interfaces create untested architecture and encourage feature creep.

Unused Waveshare peripherals should be kept unclaimed or explicitly placed in a harmless board-defined state only where needed for safe boot/power/resource behavior.

## 6. Build-time board selection

Exactly one board target must be selected per firmware build.

Recommended compile-time model:

```text
BOARD_TARGET=<board-id>
```

or an equivalent Kconfig choice.

Requirements:

- invalid/multiple board selection fails the build;
- common Core sources are identical for every board build at the same Core revision;
- only the selected board adapter is linked;
- board-specific pins/defaults do not leak into common Core headers;
- the firmware reports both Core revision and Board ID at runtime;
- partition/config compatibility is explicitly checked before changing any persisted layout.

## 7. Git branch model

Long-lived branch roles:

### `core/stable`

Future canonical branch for the shared Product Core and Board Support Contract.

It is created only after the current field-proven `phase1-fix` baseline is frozen and reconciled. Until that transition is executed, `phase1-fix@3c486f0...` remains the source baseline.

### `board/<board-id>`

Long-lived branch containing:

- the canonical Core inherited from `core/stable`;
- exactly that board's adapter/integration delta;
- board-specific hardware evidence;
- no unrelated feature development.

Current board:

`board/waveshare-esp32-s3-touch-lcd-5`

Future examples:

`board/<vendor>-<model>-<revision>`

### Short-lived work branches

Use bounded branches such as:

- `work/core/<issue>`
- `work/board/waveshare-5/<issue>`
- `test/<scope>` when needed

Delete/archive after integration according to branch-retention policy.

## 8. Core change propagation rule

This is the most important repository invariant.

A generic bug fix or approved Core improvement is never allowed to remain trapped in one board branch.

### Required Core-fix flow

```text
Defect found
   |
   v
Classify: CORE or BOARD-SPECIFIC?
   |
   +--> CORE
   |      |
   |      v
   |   work/core/<issue>
   |      |
   |      v
   |   Core tests + exact target build gates
   |      |
   |      v
   |   merge -> core/stable
   |      |
   |      v
   |   BOARD SYNC GATE
   |      |
   |      +--> merge core/stable -> board/A
   |      +--> merge core/stable -> board/B
   |      +--> merge core/stable -> board/C
   |      |
   |      v
   |   per-board build/regression evidence
   |
   +--> BOARD-SPECIFIC
          |
          v
       work/board/<id>/<issue>
          |
          v
       merge only -> board/<id>
```

### Mandatory rules

- Do not fix a generic Core defect directly and permanently on a board branch.
- If a Core defect is discovered while working on a board, reproduce/classify it, fix it on a Core work branch, merge to Core, then resync boards.
- Do not merge one complete board branch into another board branch.
- Do not merge a complete board branch back into Core.
- If a board change reveals a reusable generic improvement, extract only the generic change into a separate Core work packet/PR; keep board-specific code in the board branch.
- Board branches consume Core changes; Core never consumes hardware-specific behavior by accident.

## 9. Board Sync Gate

Every Core merge creates a synchronization obligation for every `SUPPORTED` board.

Maintain a repository record equivalent to:

```yaml
core_head: <sha>
boards:
  waveshare-esp32-s3-touch-lcd-5:
    branch: board/waveshare-esp32-s3-touch-lcd-5
    synced_core_sha: <sha>
    build: PASS|PENDING|FAIL
    regression: PASS|PENDING|FAIL
    hardware_required: true|false
```

A board is `CORE_CURRENT` only when its `synced_core_sha` equals the intended Core release SHA and required board gates pass.

Release of a board from a stale Core SHA is blocked unless Product Owner authority explicitly freezes that board on an older release for a documented reason.

## 10. Board-specific containment rule

Each board branch must maintain an ownership manifest.

For Waveshare, board-specific writes should normally be limited to:

- `boards/waveshare_esp32_s3_touch_lcd_5/**`
- board-specific evidence/docs;
- narrow shared build-selection integration owned by the integration lane.

If a board implementation needs a change in a Core-owned path, L0/Integration must classify it first:

1. genuine generic Core change -> Core work packet;
2. missing generic Board Support Contract -> Core interface change, then board implementation;
3. board hack leaking into Core -> reject/rework;
4. unavoidable target conditional -> document and minimize, with a planned interface cleanup only if evidence justifies it.

Board-specific `#ifdef BOARD_X` conditionals scattered through safety/control/web/business files are prohibited as a normal architecture.

## 11. Current Waveshare milestone — parity, not expansion

Success means the existing product behavior runs on the Waveshare target without changing user-visible or field-control semantics.

Current required product surface:

- same boot/safe-start behavior;
- same configuration and migration behavior;
- same Wi-Fi/network behavior required by current product;
- same Modbus TCP behavior;
- same meter/inverter behavior;
- same source detection;
- same control and fail-safe rules;
- same commissioning gates;
- same authentication/authorization;
- same alarms/audit/provenance;
- same HTTP APIs;
- same Web UI and browser workflows;
- same automatic-control default/authority behavior;
- same persistence expectations across normal firmware update/reboot.

Waveshare onboard LCD, touch, RS485, SD, RTC, CAN and DI/DO are not part of product parity unless required merely to establish safe board boot/resource ownership.

## 12. Golden behavior baseline

Before portability edits, freeze the current working behavior as a golden reference.

Record from the proven baseline where available:

- exact SHA and ESP-IDF version;
- build warnings and image/partition sizes;
- current production default/safety settings;
- API route inventory and representative response schemas;
- Web asset hash/order and browser regression suite;
- configuration schema versions/migration behavior;
- control/safety host tests;
- known physical-site verified facts already present in repository evidence;
- known intentionally disabled/unqualified behavior;
- startup resource/stack/heap expectations.

Porting changes are accepted only if differences are board-required and documented.

## 13. Execution phases

### Phase 0 — Scope freeze and canonical baseline

Outcome: no ambiguity about what is being preserved.

Tasks:

- freeze `phase1-fix@3c486f0...` as migration source;
- inventory active Core components and build assumptions;
- produce Core-vs-board ownership map;
- capture golden tests/contracts/evidence;
- identify DevKit-specific sdkconfig/pin/resource assumptions;
- freeze exact Waveshare SKU/revision;
- mark all new hardware capabilities `RESERVED_NOT_ACTIVE`.

Exit gate: baseline and ownership are reviewable from repository evidence.

### Phase 1 — Core/Board contract bootstrap

Outcome: minimal board seam exists without changing product behavior.

Tasks:

- create `board_support` contract;
- add board ID/build selection;
- add Waveshare adapter skeleton;
- add board capability declaration;
- move only genuine board constants/defaults behind the seam;
- keep stable Core code in place;
- add compile-time containment tests.

Exit gate: original target and Waveshare target both compile from the same Core sources; no product feature changed.

### Phase 2 — Canonical Core branch transition

Outcome: one authoritative shared Core line exists.

Tasks:

- reconcile `phase1-fix` against any legitimate later Core-only fixes;
- create `core/stable` from the approved reconciled head;
- document branch authority;
- update stale README/developer instructions that point to obsolete branches;
- establish Core change classification and Board Sync Gate records.

Exit gate: future generic changes have exactly one merge destination.

### Phase 3 — Waveshare parity bring-up

Outcome: same product boots and operates on the Waveshare board with no new product capability.

Tasks:

- qualify exact Waveshare module/revision and required vendor board glue;
- produce exact board sdkconfig/resource configuration;
- boot Core on physical Waveshare target;
- prove NVS/config load;
- prove Wi-Fi/network/Web UI;
- prove current Modbus TCP path with controlled/real peer;
- prove existing meter/inverter read behavior;
- keep automatic control authority in the existing safe state during bring-up;
- measure heap/PSRAM/stacks/watchdog/control cadence under existing Web workload;
- confirm unused peripherals cannot create unsafe outputs or resource conflicts.

Exit gate: `FEATURE_PARITY_CANDIDATE` with target-runtime evidence; no field-control claim yet beyond evidence actually exercised.

### Phase 4 — Regression and safety equivalence

Outcome: board port has not changed proven semantics.

Run exact-head:

- all deterministic host/source contracts;
- config migration/persistence tests;
- auth/RBAC negative tests;
- API contract tests;
- Web full-app/browser regression;
- control/safety positive and fail-closed tests;
- network reconnect/resource tests relevant to current product;
- target build warning/size/resource gates;
- physical Waveshare runtime tests for hardware-dependent paths;
- representative HIL/bench tests for current product meter/inverter/control interfaces.

Any unexpected behavioral difference is a defect until explained and approved.

Exit gate: Quality-360 required dimensions PASS for the parity port.

### Phase 5 — Core propagation proof

Outcome: architecture proves the reason it exists.

Use a harmless, testable Core-only change or the next legitimate Core defect fix to exercise the real propagation workflow:

1. Core work branch;
2. Core verification;
3. merge to `core/stable`;
4. sync every supported board branch;
5. per-board exact build;
6. required regression/HIL by failure surface;
7. update sync ledger.

Do not declare the multi-board architecture operational until this propagation loop is proven once end-to-end.

### Phase 6 — Branch hygiene and release

Outcome: maintainable repository state.

Tasks:

- classify all existing branches/PRs;
- close superseded PRs where trace is sufficient;
- archive/tag where retention is required;
- delete only confirmed obsolete branches;
- retain `core/stable` + supported `board/*` long-lived lines;
- delete merged temporary work branches;
- publish exact Core SHA + Board SHA/version pairing;
- update `.aish/RESUME.md` and board sync ledger.

Exit gate: no ambiguous canonical branch and no stale board/core relationship.

## 14. Parallel AISH lanes for this scope

The current scope does not need feature teams for LCD/RS485/SD/etc.

Use these lanes:

- **L0 Integration/Governance** — Core/board ownership, shared build files, branch authority, sync ledger, exact-head integration.
- **L1 Baseline/Architecture** — repository audit, golden behavior, board contract, dependency DAG.
- **L2 Waveshare Board Adapter** — only `boards/waveshare.../**` and board-required glue.
- **L3 Core Portability** — removes genuine hardware assumptions behind the stable board contract; no feature work.
- **L4 Regression/Web Parity QA** — existing API/Web behavior and browser evidence.
- **L5 Safety/Functional QA** — existing control, fail-closed, auth, configuration and negative-path verification.
- **L6 Hardware/HIL** — exact Waveshare boot/network/Modbus TCP/resource/current-product physical evidence.
- **L7 Release/Branch Hygiene** — propagation audit, stale branch classification, exact release evidence.

Default implementation WIP limit: 2–3 non-overlapping implementation lanes. QA may run in parallel when contracts are stable.

## 15. Quality-360 for feature-parity port

Required unless explicitly shown not applicable:

- FUNCTIONAL_CORRECTNESS
- USER_WORKFLOW_UX — regression only; no redesign
- VISUAL_DESIGN — regression only
- RESPONSIVE_ADAPTIVE — existing Web regression
- ACCESSIBILITY — existing Web regression
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

New-feature-specific UX is not applicable because new features are not authorized.

## 16. Completion definition

The Waveshare parity port is not complete until all of these are true:

- same approved Core is used by the original/reference target and Waveshare target;
- board-specific code is contained behind the Board Support boundary;
- no unauthorized product feature was introduced;
- exact Waveshare target build passes;
- exact hardware runtime evidence passes for current product paths;
- required HIL/bench evidence passes for current control/network/device failure surfaces;
- current Web interface/API/config semantics remain regression-green;
- resource/timing behavior remains within safe measured bounds;
- all REQUIRED Quality-360 dimensions PASS;
- Core/Board Sync Gate is implemented and proven;
- repo context and TODO are current;
- release head records exact Core SHA and board adapter SHA/state.

## 17. Future feature rule

When later authorized, a feature is classified before implementation:

### Core feature

Needed on all boards -> implement in Core, then sync all board branches.

### Capability-dependent Core feature

Common behavior, hardware capability optional -> Core defines policy/contract; each capable board implements the physical adapter; unsupported boards explicitly report capability unavailable.

### Board-only feature

Truly unique to one board/customer hardware -> stays in that board branch/adapter and must not modify common product semantics.

No future agent may infer that a physical peripheral's presence makes it part of product scope.