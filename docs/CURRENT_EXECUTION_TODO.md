# Current Execution TODO

**Branch:** `feature/multibrand-inverter-profiles`  
**Software head before this TODO commit:** `22c627148bbd3fa4216405dfa5c3a3eb1bb94bbf`  
**Verification:** complete browser/source/host-test suite is green; final exact-head ESP-IDF 6.0.1 zero-warning build is required before flashing.  
**Release state:** development/bench candidate only; physical meter, source-evidence and inverter FAT/SAT remain mandatory.

`MASTER_EXECUTION_TODO.md` remains the full product scope.

## Completed and software-tested

### Core audit and safety

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

### Authentication and protected writes

- [x] Restore persistent salted Engineering password authentication.
- [x] Add serial-only one-time Engineering setup code.
- [x] Add random HttpOnly SameSite session cookies with expiry.
- [x] Add failed-login lockout, logout invalidation and password rotation.
- [x] Remove temporary Engineering authentication bypass.
- [x] Protect write, restart, configuration and Engineering endpoints through the registration gateway.
- [x] Add production access-policy and auth-loop regression gates.

### Inverter command safety foundation

- [x] Expire and reverify inverter identity.
- [x] Build immutable commandable-fleet snapshot.
- [x] Validate command scale, range, width and finiteness.
- [x] Support one-word and two-word commands.
- [x] Enforce aggregate command cap before physical writes.
- [x] Execute write/readback confirmation after production commands.
- [x] Execute bounded command retry.
- [x] Execute rollback to safe zero after failure or mismatch.
- [x] Record applied command state only after confirmed readback.

### Source-state and control foundation

- [x] Add deterministic Grid/Generator source-state classification.
- [x] Add Grid Only, Generator Only, synchronized, Island, Transfer, No Source, Conflict and Unknown states.
- [x] Add generator minimum-load, reserve and reverse-power-margin safe-PV calculation.
- [x] Add multi-generator aggregation for up to three generator evidence records.
- [x] Reject stale, contradictory and non-finite generator evidence.
- [x] Add zero-export, limited-export and minimum-import policy calculations.
- [x] Add PI deadband, anti-windup and independent ramp limits.
- [x] Apply generator safe limit to the control-policy output.
- [x] Fail closed during transfer, conflict, unknown and no-source conditions.
- [x] Reset live PI/ramp state on invalid communication and write failure.
- [x] Keep live generator and transfer operation blocked until real run/breaker/ATS evidence exists.
- [x] Add executable host tests for source modes, generator aggregation, control policy and inverter command confirmation.

### EM500 acquisition and browser quality

- [x] Add dedicated background EM500 acquisition task.
- [x] Cache instantaneous, source-input, energy and setup register groups.
- [x] Route EM500 snapshot HTTP reads through the immediate cache adapter.
- [x] Route EM500 history, settings and settings-plan reads through bounded background jobs.
- [x] Preserve last-good register data with freshness, response-time and success/error metadata.
- [x] Add `/api/meters/em500/cache` quality/freshness status endpoint.
- [x] Add source contracts proving EM500 HTTP handlers do not execute direct Modbus I/O.
- [x] Surface EM500 cache group quality, age, response time and scan state in Engineering.
- [x] Label stale last-good analyser values explicitly.
- [x] Add bounded automatic retry while a background cache/job is warming.
- [x] Preserve and expose Modbus exception function, code, timestamp and count.

### Network availability and commissioning

- [x] Support legal 32-byte SSIDs without truncation in ESP-IDF station/AP structures.
- [x] Support legal 64-byte PSKs without truncation.
- [x] Move Wi-Fi connect, disconnect, retry, fallback-AP and scan actions into the manager task.
- [x] Replace long reconnect delays with interruptible manager waits.
- [x] Single-own retry, fallback and sweep state.
- [x] Ensure terminal disconnect schedules retry or recovery AP.
- [x] Stop browser Wi-Fi scan polling after route exit, hidden-tab state or deadline.
- [x] Add reconnect/scan ownership and lifecycle regression tests.

### Persisted Solar + Grid product model

- [x] Persist selectable zero-export, limited-export and minimum-import policies in verified NVS storage.
- [x] Persist export limit and minimum-import settings.
- [x] Add grid-meter import-positive/export-positive orientation commissioning.
- [x] Add explicit Modbus grid-availability and grid-breaker-closed evidence.
- [x] Remove the fresh-power-meter-only Grid Only assumption.
- [x] Add immediate fail-closed blocking for stale, absent, open-breaker and contradictory source evidence.
- [x] Add grid-loss confirmation and continuous recovery-stabilization timers.
- [x] Add a non-blocking cached Solar-Grid runtime status API.
- [x] Add a protected Solar-Grid commissioning page with bounded requests and route-aware polling.
- [x] Force automatic control disabled in persistence and in the running controller after source-model changes.
- [x] Schedule and confirm a safe-zero inverter command from the control task after runtime disable.
- [x] Preserve the last confirmed applied command if safe-zero confirmation fails.
- [x] Add load-step, communication-loss, loss-confirmation and recovery-stabilization host integration tests.
- [x] Add source contracts preventing HTTP Modbus I/O and hardcoded Grid Only operation.

## Remaining software work

### Modbus socket qualification and remaining browser lifecycle audit

- [ ] Qualify persistent, reconnect-on-error and per-transaction socket modes.
- [ ] Perform TCP PCB/TIME_WAIT endurance test on hardware.
- [ ] Audit every remaining browser poller for timeout/finally/route-exit protection.

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

- [ ] Commission the real grid-availability and breaker evidence register addresses; no address has been guessed.
- [ ] Real meter register, sign, scale and scan-rate FAT.
- [ ] One-hour browser/controller healthy, slow and failure soak.
- [ ] Meter-loss, inverter-loss, Wi-Fi-loss and restart FAT.
- [ ] Grid zero-export, limited-export and minimum-import FAT.
- [ ] Generator minimum-load and reverse-power FAT.
- [ ] Source-transfer FAT.
- [ ] Inverter command/readback/rollback FAT for every approved model.
- [ ] Signed site SAT tied to firmware SHA, sdkconfig hash, ESP-IDF version, board MAC and timestamp.
- [ ] Production release approved.

Current result: the software Solar-Grid operating model, explicit source evidence gate, network ownership, asynchronous analyser path, production authentication and transactional inverter safety foundation are implemented and software-tested. The controller remains fail-closed until actual grid evidence registers and approved inverter profiles are commissioned and physically qualified.
