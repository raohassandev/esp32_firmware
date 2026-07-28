# Current Execution TODO

**Branch:** `feature/multibrand-inverter-profiles`  
**Current software head before this TODO commit:** `e982d3f952e146b730c9d7cc365560ac27c32f0c`  
**CI run:** `30393575906` — complete web/source/host-test suite passed; ESP-IDF 6.0.1 build passed; compiler warnings: 0.  
**Release state:** development/bench candidate only; unqualified physical inverter profiles remain fail-closed.

`MASTER_EXECUTION_TODO.md` remains the full product scope.

## Completed and software-tested

- [x] Correct recursive Meters-page error observer.
- [x] Correct Alarms-page observer recursion.
- [x] Add bounded/cancellable browser meter requests.
- [x] Remove duplicate full EM500 consistency polling.
- [x] Disable build-time Wi-Fi provisioning by default.
- [x] Remove compiled station credentials.
- [x] Preserve commissioned Wi-Fi through schema migration.
- [x] Prevent generic configuration import from enabling control.
- [x] Validate imported endpoint, meter, inverter and control numbers.
- [x] Add shared bounded HTTP body reader with cumulative deadlines.
- [x] Add JSON-depth protection to protected write APIs.
- [x] Return JSON null for unavailable power and age values.
- [x] Reject NaN and infinity in Modbus decoding and manager boundaries.
- [x] Fail control calculations toward zero on invalid data.
- [x] Move operational JSON allocation outside spinlocks.
- [x] Add cumulative Modbus transaction deadline and timeout bounds.
- [x] Bypass DNS for literal IPv4 endpoints.
- [x] Handle interrupted socket operations.
- [x] Clamp meter count and degraded polling rate.
- [x] Restore persistent salted Engineering password authentication.
- [x] Add serial-only one-time Engineering setup code.
- [x] Add random HttpOnly SameSite session cookies with expiry.
- [x] Add failed-login lockout, logout invalidation and password rotation.
- [x] Remove temporary Engineering authentication bypass.
- [x] Protect write, restart, configuration and Engineering endpoints through the registration gateway.
- [x] Add production access-policy and auth-loop regression gates.
- [x] Expire and reverify inverter identity.
- [x] Build immutable commandable-fleet snapshot.
- [x] Validate command scale, range, width and finiteness.
- [x] Support one-word and two-word commands.
- [x] Enforce aggregate command cap before physical writes.
- [x] Execute write/readback confirmation after production commands.
- [x] Execute bounded command retry.
- [x] Execute rollback to safe zero after failure or mismatch.
- [x] Record applied command state only after confirmed readback.
- [x] Add deterministic Grid/Generator source-state classification.
- [x] Add Grid Only, Generator Only, synchronized, Island, Transfer, No Source, Conflict and Unknown states.
- [x] Add generator minimum-load, reserve and reverse-power-margin safe-PV calculation.
- [x] Add multi-generator aggregation for up to three generator evidence records.
- [x] Reject stale, contradictory and non-finite generator evidence.
- [x] Add zero-export, limited-export and minimum-import policy calculations.
- [x] Add PI deadband, anti-windup and independent ramp limits.
- [x] Apply generator safe limit to the control-policy output.
- [x] Fail closed during transfer, conflict, unknown and no-source conditions.
- [x] Connect tested minimum-grid-import policy to the live control task.
- [x] Reset live PI/ramp state on invalid communication and write failure.
- [x] Keep live generator and transfer operation blocked until real run/breaker/ATS evidence exists.
- [x] Add executable host tests for source modes, generator aggregation, control policy and inverter command confirmation.
- [x] Add dedicated background EM500 acquisition task.
- [x] Cache instantaneous, source-input, energy and setup register groups.
- [x] Route EM500 snapshot HTTP reads through the immediate cache adapter.
- [x] Route EM500 history, settings and settings-plan reads through bounded background jobs.
- [x] Preserve last-good register data with freshness, response-time and success/error metadata.
- [x] Add `/api/meters/em500/cache` quality/freshness status endpoint.
- [x] Add source contracts proving EM500 HTTP handlers do not execute direct Modbus I/O.
- [x] Pass complete current software test suite and ESP-IDF build with zero warnings.

## Remaining software work

### Network availability and commissioning

- [ ] Support legal 32-byte SSIDs without truncation in ESP-IDF station/AP structures.
- [ ] Support legal 64-byte PSKs without truncation.
- [ ] Move remaining Wi-Fi radio actions out of event callbacks into the manager task.
- [ ] Replace long reconnect delays with interruptible manager waits.
- [ ] Protect or single-own retry, fallback and sweep state.
- [ ] Verify terminal disconnect always schedules retry or recovery AP.
- [ ] Stop browser Wi-Fi scan polling after route exit, hidden-tab state or deadline.
- [ ] Complete reconnect/scan race regression tests.

### Meter and browser quality

- [ ] Surface EM500 cache group quality, age, response time and scan state in the Engineering page.
- [ ] Label stale last-good analyser values explicitly in every tab.
- [ ] Add bounded automatic retry while a background cache/job is warming.
- [ ] Preserve and expose Modbus exception function and exception code.
- [ ] Qualify persistent, reconnect-on-error and per-transaction socket modes.
- [ ] Perform TCP PCB/TIME_WAIT endurance test on hardware.
- [ ] Add timeout/finally protection to every remaining browser poller.

### Persisted Solar + Grid product model

- [ ] Persist selectable zero-export, limited-export and minimum-import policies.
- [ ] Persist export limit and minimum-import settings.
- [ ] Add signed grid-meter orientation commissioning workflow.
- [ ] Add explicit grid breaker/availability evidence instead of assuming Grid Only from a fresh grid meter.
- [ ] Add grid-loss shutdown and recovery stabilization timers.
- [ ] Add simulator load-step and communication-loss integration tests.

### Live Solar + Generator integration

- [ ] Add Generator 1–3 meter roles to persisted configuration.
- [ ] Acquire generator run feedback.
- [ ] Acquire generator breaker/contactor feedback.
- [ ] Acquire ATS/transfer and grid-generator synchronization evidence.
- [ ] Persist rated capacity, minimum loading, reserve and reverse margin per generator.
- [ ] Connect real generator evidence to the tested source-state engine.
- [ ] Connect live generator safe-PV cap to inverter commands.
- [ ] Add fast PV curtailment after generator load rejection.
- [ ] Add warm-up, synchronized, loaded, cooling and stopped timers.
- [ ] Add transition stabilization and PI/ramp reset.
- [ ] Add generator simulator integration tests.

### Inverter qualification

- [ ] Complete manufacturer/manual register inventory.
- [ ] Complete model-specific identity, telemetry, command and readback maps.
- [ ] Add model-specific timeout, enable sequence and rollback behavior where required.
- [ ] Physically qualify every supported inverter profile.
- [ ] Record signed production approval per profile.

## Physical release gates

- [ ] Real meter register, sign, scale and scan-rate FAT.
- [ ] One-hour browser/controller healthy, slow and failure soak.
- [ ] Meter-loss, inverter-loss, Wi-Fi-loss and restart FAT.
- [ ] Grid zero-export, limited-export and minimum-import FAT.
- [ ] Generator minimum-load and reverse-power FAT.
- [ ] Source-transfer FAT.
- [ ] Inverter command/readback/rollback FAT for every approved model.
- [ ] Signed site SAT tied to firmware SHA, sdkconfig hash, ESP-IDF version, board MAC and timestamp.
- [ ] Production release approved.

Current result: the software safety foundation, production authentication, live grid policy, transactional inverter confirmation, and asynchronous analyser architecture are implemented and green. Remaining software work is concentrated in Wi-Fi ownership/recovery, persisted source evidence and Generator integration. Physical FAT/SAT remains mandatory and cannot be completed without the connected plant equipment.
