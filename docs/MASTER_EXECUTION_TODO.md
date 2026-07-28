# Automatrix ESP32 PV-DG Controller — Master Execution TODO

**Repository:** `raohassandev/esp32_firmware`  
**Integration branch:** `feature/multibrand-inverter-profiles`  
**Baseline:** `ed765b090b024c07c3beedeef29aadba13428ca7`  
**Audit source:** `docs/DEEP_CODE_AUDIT_2026-07-28.md` from `audit/deep-code-audit-2026-07-28`  
**Related system reference:** `raohassandev/SolTrix` (`dev` / RC0 safety model)

## Product objective

Deliver a reliable ESP32-S3 industrial controller that safely manages:

1. **Solar + Grid operation**
   - zero export;
   - limited export;
   - minimum grid import hold;
   - maximum solar utilization;
   - grid-loss fail-safe;
   - recovery and ramp control.

2. **Solar + Generator operation**
   - generator minimum-loading hold;
   - reverse-power prevention;
   - configurable generator reserve/headroom;
   - generator run/breaker/source validation;
   - solar curtailment during low plant load;
   - multi-generator support;
   - communication-loss fail-safe;
   - recovery and ramp control.

3. **Solar + Grid + Generator operation**
   - explicit source-state detection;
   - grid-only, generator-only, synchronized, transfer, island, no-source and unknown modes;
   - priority and transition logic;
   - safe inverter command/readback;
   - fail-closed control on conflicting or stale evidence.

The ESP firmware must follow the same safety boundary used by SolTrix: no production physical writes or automatic PV-DG claim without verified register mapping, write/readback, scaling, timeout, rollback, communication-loss, scan-rate and signed FAT/SAT evidence.

---

# Current status

- [x] Deep source audit completed on baseline `ed765b0`.
- [x] Audit branch anchored and report published.
- [x] Meter Modbus frame validation exists.
- [x] Basic grid-meter active-power polling exists.
- [x] Browser request timeout/cancellation exists.
- [x] Inverter writes remain profile-gated/fail-closed.
- [ ] Meters-page freeze fixed and regression-tested.
- [ ] Production authentication restored.
- [ ] Full analyser acquisition moved out of HTTP handlers.
- [ ] Solar-Grid control physically qualified.
- [ ] Solar-Generator control implemented and physically qualified.
- [ ] Combined Grid/Generator source-state engine completed.
- [ ] Production release gates completed.

---

# P0 — Immediate release and bench blockers

## P0.1 Browser freeze

- [ ] Fix recursive `MutationObserver` feedback in `web/engineering-errors.js`.
- [ ] Do not translate generated descendants again.
- [ ] Do not destroy diagnostics cards or Retry controls with `replaceChildren()`.
- [ ] Narrow or remove whole-page subtree observation.
- [ ] Add a DOM-stability regression test for `ESP_ERR_TIMEOUT`.
- [ ] Verify stable CPU and bounded DOM count for 30 minutes with repeated meter errors.

**Acceptance:** one raw error produces one friendly message; navigation and Retry remain responsive.

## P0.2 Commissioned Wi-Fi protection

- [ ] Restore build Wi-Fi provisioning default to disabled.
- [ ] Preserve `wifi_provision_id` during schema migrations.
- [ ] Never overwrite commissioned SSID, password, DHCP/static-IP mode during upgrade.
- [ ] Remove personal/default Wi-Fi credentials from production build inputs.
- [ ] Add schema migration tests and release CI gate.

**Acceptance:** firmware upgrade preserves existing network configuration without site intervention.

## P0.3 Non-finite numeric safety

- [ ] Reject NaN and infinity in every Modbus decoder.
- [ ] Reject non-finite meter and inverter values at manager boundaries.
- [ ] Make clamp helpers fail-safe on non-finite input.
- [ ] Block PI/control calculations when source data is invalid.
- [ ] Test `0xFFFFFFFF`, NaN, +Inf and -Inf register patterns.

**Acceptance:** invalid numeric data can never create a non-zero inverter command.

## P0.4 HTTP/API safety

- [ ] Bound JSON nesting depth before recursive parsing.
- [ ] Apply consistent body-size and timeout limits.
- [ ] Move all cJSON allocation outside spinlocks.
- [ ] Snapshot history/events under lock, serialize after unlock.
- [ ] Prevent generic `/api/config` from bypassing control interlocks.
- [ ] Validate every numeric configuration field for range and finiteness.

**Acceptance:** hostile JSON and maximum history requests cannot reboot, deadlock or starve the ESP32.

## P0.5 Authentication

- [ ] Restore production Engineering authentication.
- [ ] Ensure denied requests return complete `401/403` responses.
- [ ] Add compile-time failure for production builds with auth bypass enabled.
- [ ] Protect config, restart, profile assignment and control endpoints.
- [ ] Keep only approved operator telemetry public/read-only.

**Acceptance:** unauthenticated clients cannot change configuration or issue control actions.

---

# P1 — Meter and Modbus runtime

## P1.1 Deterministic transaction engine

- [ ] Add one cumulative transaction deadline for DNS/connect/send/receive/parse.
- [ ] Do not restart the total timeout for each received byte.
- [ ] Parse/cache literal IPv4 endpoints; bound hostname resolution.
- [ ] Support qualified connection modes per device: persistent, reconnect-on-error, per-transaction.
- [ ] Measure and prevent TCP PCB/TIME_WAIT exhaustion.
- [ ] Preserve and expose Modbus exception codes.
- [ ] Reject timeout `0` and unreasonable values.
- [ ] Ensure degraded backoff is never faster than configured normal polling.
- [ ] Remove silent EM500 scale heuristics; use explicit profile metadata.

## P1.2 Acquisition scheduler and cache

- [ ] Stop long Modbus scans inside HTTP handlers.
- [ ] Create one dedicated per-bus/per-meter acquisition scheduler.
- [ ] Maintain fast cache: active power, voltage, frequency, PF.
- [ ] Maintain medium cache: current, reactive/apparent power.
- [ ] Maintain slow cache: THD, asymmetry, energy counters.
- [ ] Implement bounded on-demand jobs for history/setup/raw diagnostics.
- [ ] Return cached API responses immediately.
- [ ] Expose scan-in-progress, age, last success/failure, response time and quality.
- [ ] Preserve last good full-analyser values and mark them stale.
- [ ] Coordinate adaptive polling and stale thresholds.

**Acceptance:** a dead/slow meter never blocks the web server, control task or unrelated device polling.

---

# P1 — Solar + Grid control

## P1.3 Grid operating modes

- [ ] Implement **Zero Export** mode.
- [ ] Implement **Limited Export** mode with configurable export cap.
- [ ] Implement **Minimum Grid Import Hold** mode.
- [ ] Implement **Maximum Solar / no-export** mode.
- [ ] Support signed meter orientation and commissioning verification.
- [ ] Apply configurable deadband, PI limits and anti-windup.
- [ ] Apply independent ramp-up and ramp-down rates.
- [ ] Block control on stale/degraded/invalid grid data.
- [ ] Define grid-loss response and recovery delay.
- [ ] Confirm command readback before reporting applied power.
- [ ] Reset/reconcile integral and ramp state after communication recovery.

**Acceptance:** grid import/export remains inside configured bounds during load steps, inverter loss, meter loss and network recovery.

---

# P1 — Solar + Generator control

## P1.4 Generator source model

- [ ] Add explicit generator meter roles for Generator 1–3.
- [ ] Add generator run feedback.
- [ ] Add generator breaker/contactor feedback.
- [ ] Add rated kW, minimum loading %, reserve kW and maximum loading %.
- [ ] Validate meter sign/orientation for each generator.
- [ ] Support multiple running generators and combined available capacity.
- [ ] Detect generator stopped, running unloaded, loaded, overloaded and unknown states.

## P1.5 Generator loading and reverse-power protection

- [ ] Implement configurable generator minimum-load hold.
- [ ] Curtail solar when generator loading approaches minimum safe load.
- [ ] Maintain configurable reserve/headroom for load steps.
- [ ] Prevent reverse power into a generator.
- [ ] Add fast curtail path for sudden load rejection.
- [ ] Add controlled recovery after load returns.
- [ ] Block PV-DG control if generator meter, run feedback or breaker evidence is stale/conflicting.
- [ ] Support one or multiple generator contribution allocation.
- [ ] Define behavior during generator start, warm-up, synchronization, cooling and stop.

## P1.6 Grid/Generator transitions

- [ ] Detect Grid Only, Generator Only, Grid + Generator synchronized, Transfer, Island, No Source, Conflict and Unknown.
- [ ] Use fresh voltage/frequency/power plus breaker/run/ATS evidence.
- [ ] Keep profile-specific register `0x2160` as auxiliary evidence only.
- [ ] Freeze or safely curtail solar during transfer ambiguity.
- [ ] Reinitialize PI/ramp state when source mode changes.
- [ ] Add configurable transition stabilization timers.

**Acceptance:** solar cannot reverse-power a generator, and source transfers do not cause uncontrolled inverter output.

---

# P1 — Inverter command safety

- [ ] Reverify inverter identity after disconnect/reconnect, endpoint change or stale period.
- [ ] Bind identity evidence to endpoint, Unit ID, profile and timestamp.
- [ ] Build one immutable eligible-fleet snapshot per command cycle.
- [ ] Enforce hard total-command cap after rounding.
- [ ] Calculate staleness at point of use.
- [ ] Disable running control before persisting profile/register-map changes.
- [ ] Validate command scaling, width, range and finiteness.
- [ ] Support declared 16-bit and 32-bit commands correctly.
- [ ] Record command state only after successful write.
- [ ] Do not advance ramp state after failed writes.
- [ ] Require readback/tolerance confirmation.
- [ ] Implement timeout, retry, rollback and safe fallback.
- [ ] Define release-limit behavior when automatic control is disabled.

**Acceptance:** no unidentified inverter receives a write; fleet command never exceeds the calculated safe limit.

---

# P1 — Network availability

- [ ] Ensure disconnected state always retries or starts recovery AP.
- [ ] Create a single Wi-Fi scan owner.
- [ ] Route UI scans through the network manager.
- [ ] Support legal 32-byte SSIDs and 64-byte PSKs without truncation.
- [ ] Make operator reconnect admission/execution atomic.
- [ ] Protect retry/fallback counters shared across tasks.
- [ ] Replace long uninterruptible delays with event-driven waits.
- [ ] Bound admission-gate lifetime and clean up failed initialization resources.

---

# P1 — Web UI stabilization

- [ ] Fix Alarms-page observer loop.
- [ ] Never display failed/uninitialized meter data as `0.00 kW`.
- [ ] Never record fabricated zeros in trends.
- [ ] Consolidate EM500 polling into one scheduler.
- [ ] Stop Wi-Fi scan polling outside the Wi-Fi page.
- [ ] Register all EM500 tabs before building the tab bar.
- [ ] Ensure Meter Profiles and CT/PT/Tariff Plan are reachable.
- [ ] Add timeout/finally handling to every poller.
- [ ] Replace synthetic global `hashchange` refreshes with targeted refresh events.
- [ ] Handle truncated/non-JSON responses as transport failures.
- [ ] Remove or archive embedded-but-unserved assets.

**Acceptance:** all pages remain responsive for one hour under healthy, slow and failed communications.

---

# P2 — Operator and Engineering product model

- [ ] Separate operator and engineering data models.
- [ ] Create fast cached `Grid Power` operator endpoint.
- [ ] Create `Generator Power` and source-mode operator views.
- [ ] Create detailed Engineering Energy Analyser with group freshness and quality.
- [ ] Keep function codes, raw addresses and word order out of operator views.
- [ ] Show plant mode, evidence, confidence and age.
- [ ] Show meter role, endpoint, Unit ID, response time, success rate and backoff.
- [ ] Add explicit states: healthy, degraded, stale, offline, unsupported and not scanned.
- [ ] Add bounded per-group Retry jobs.

---

# P2 — Validation and release evidence

## Software tests

- [ ] Unit tests for NaN/Inf safety.
- [ ] Unit tests for grid and generator control calculations.
- [ ] Simulator tests for load steps and communication failures.
- [ ] Tests for source-mode transitions and conflicting evidence.
- [ ] Tests for inverter identity expiry and reassignment.
- [ ] Tests for command timeout/readback/rollback.
- [ ] Browser DOM/CPU/request-count soak tests.
- [ ] Network disconnect/recovery tests.

## Bench FAT

- [ ] Verify actual meter register map, sign, scale and scan rate.
- [ ] Verify Huawei `40125`, FC06, `percent_x10` and readback where applicable.
- [ ] Verify each supported inverter model from manufacturer documentation.
- [ ] Verify zero export and limited export.
- [ ] Verify generator minimum loading and reverse-power prevention.
- [ ] Verify rapid load rejection and solar curtailment.
- [ ] Verify meter loss, inverter loss, Wi-Fi loss and controller restart.
- [ ] Verify rollback after write/readback mismatch.

## Site SAT

- [ ] Signed grid-mode test record.
- [ ] Signed generator-mode test record.
- [ ] Signed source-transfer test record.
- [ ] Signed communication-loss/fail-safe test record.
- [ ] Signed maximum scan-rate and stability record.
- [ ] Record firmware SHA, sdkconfig hash, ESP-IDF version, board MAC and timestamp.

---

# Release gates

A production release is blocked unless all are true:

- [ ] No open Critical findings.
- [ ] Authentication bypass disabled.
- [ ] Build Wi-Fi provisioning disabled.
- [ ] No default/personal credentials.
- [ ] No non-finite fail-to-maximum path.
- [ ] No long Modbus operation inside an HTTP handler.
- [ ] Grid and generator control modes physically qualified.
- [ ] Inverter writes documented, read back and rollback-qualified.
- [ ] Browser and controller soak tests pass.
- [ ] FAT/SAT evidence is signed and tied to the exact firmware SHA.

---

# Execution batches

## Batch 1 — Immediate safety and page recovery

- [ ] W1 browser recursion.
- [ ] C1/C2 Wi-Fi provisioning preservation.
- [ ] S1 non-finite fail-safe.
- [ ] H2/H3 JSON and spinlock safety.
- [ ] H1/H5 authentication and denial response.

## Batch 2 — Communication architecture

- [ ] M1/M2 cumulative deadline and DNS behavior.
- [ ] Connection-mode qualification and TIME_WAIT control.
- [ ] Acquisition scheduler and cached APIs.
- [ ] Communication group quality and stale-data model.

## Batch 3 — Solar-Grid and Solar-Generator control

- [ ] Grid zero/limited export and import hold.
- [ ] Generator minimum-load and reverse-power protection.
- [ ] Combined source-state/transition engine.
- [ ] Safety interlocks and controller-state reset rules.

## Batch 4 — Inverter write correctness

- [ ] Identity expiry/reverification.
- [ ] Immutable fleet eligibility snapshot.
- [ ] Scaling/width/range validation.
- [ ] Write/readback/rollback/ramp correctness.

## Batch 5 — Network and UI completion

- [ ] Network retry and scan ownership.
- [ ] Remaining UI observer/poller defects.
- [ ] Operator/Engineering page separation.

## Batch 6 — Qualification and release

- [ ] Simulator and bench FAT.
- [ ] Site SAT.
- [ ] Production build gates.
- [ ] Immutable release tag and evidence bundle.

---

# Update discipline

Every implementation update must include:

1. exact branch and SHA;
2. completed TODO items;
3. remaining TODO items;
4. tests run and results;
5. known blockers;
6. whether the SHA is safe to build, flash, bench-test or release.

Do not mark an item complete from compilation/source-contract tests alone when physical behavior is required.