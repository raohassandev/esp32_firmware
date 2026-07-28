# Current Execution TODO

**Branch:** `feature/multibrand-inverter-profiles`  
**Current software head before this TODO commit:** `020d5edd9f7cec63e2996e33c7ff355b6d1dd4ea`  
**Release state:** development only; physical writes remain fail-closed.

`MASTER_EXECUTION_TODO.md` remains the full product scope.

## Completed and software-tested

- [x] Correct recursive Meters-page error observer.
- [x] Correct Alarms-page observer recursion.
- [x] Add bounded/cancellable browser meter requests.
- [x] Remove duplicate full EM500 consistency polling.
- [x] Disable build-time Wi-Fi provisioning by default.
- [x] Remove compiled station credentials.
- [x] Preserve commissioned Wi-Fi through schema migration.
- [x] Add bounded configuration JSON nesting validation.
- [x] Prevent generic configuration import from enabling control.
- [x] Validate imported endpoint, meter, inverter and control numbers.
- [x] Reject NaN and infinity in Modbus decoding and manager boundaries.
- [x] Fail control calculations toward zero on invalid data.
- [x] Move operational JSON allocation outside spinlocks.
- [x] Add cumulative Modbus transaction deadline and timeout bounds.
- [x] Bypass DNS for literal IPv4 endpoints.
- [x] Handle interrupted socket operations.
- [x] Clamp meter count and degraded polling rate.
- [x] Expire and reverify inverter identity.
- [x] Build immutable commandable-fleet snapshot.
- [x] Validate command scale, range, width and finiteness.
- [x] Support one-word and two-word commands.
- [x] Record command state only after successful write.
- [x] Enforce aggregate command cap.
- [x] Add proper HTTP 401 denial response.
- [x] Add deterministic source-state classification.
- [x] Add Grid Only, Generator Only, synchronized, Island, Transfer, No Source, Conflict and Unknown states.
- [x] Add generator minimum-load, reserve and reverse-power-margin safe-PV calculation.
- [x] Add multi-generator aggregation for up to three configured generators.
- [x] Reject stale, contradictory and non-finite generator evidence.
- [x] Add zero-export, limited-export and minimum-import policy calculations.
- [x] Add PI deadband, anti-windup and independent ramp limits.
- [x] Apply generator safe limit to the control-policy output.
- [x] Fail closed during transfer, conflict, unknown and no-source conditions.
- [x] Add inverter command readback decision policy.
- [x] Require qualified readback before command confirmation.
- [x] Add bounded retry, mismatch detection, rollback and safe-fallback decisions.
- [x] Add executable host tests for source modes, generator aggregation, control policy and inverter command confirmation.
- [x] Compile new control and inverter policy modules into ESP-IDF components.

## Validation in progress

- [ ] Complete GitHub web/source/host-test suite for the current head.
- [ ] ESP-IDF 6.0.1 build for the current head.
- [ ] Zero compiler-warning confirmation.

## P0 software blockers remaining

- [ ] Apply JSON-depth protection to every independent JSON parser.
- [ ] Add bounded request-body receive deadlines to every write endpoint.
- [ ] Return JSON null for unavailable status power values.
- [ ] Restore production salted-password/session authentication.
- [ ] Reject production builds when authentication bypass is enabled.
- [ ] Verify every configuration, restart, profile and control endpoint is protected.

## Live meter and web architecture remaining

- [ ] Move full EM500 scans out of synchronous HTTP handlers.
- [ ] Add dedicated meter acquisition scheduler.
- [ ] Cache fast operational values.
- [ ] Cache medium/slow analyser and energy groups.
- [ ] Add bounded jobs for history, setup and raw diagnostics.
- [ ] Expose group freshness, quality, response time and active backoff.
- [ ] Preserve and label last-good analyser data.
- [ ] Preserve Modbus exception function and code.
- [ ] Qualify socket modes and TCP resource endurance.
- [ ] Stop Wi-Fi polling after route exit/deadline.
- [ ] Add timeout/finally protection to all remaining pollers.

## Live Solar + Grid integration remaining

- [ ] Connect the tested control policy to the live control task.
- [ ] Persist selected grid policy and limits.
- [ ] Add signed grid-meter orientation commissioning.
- [ ] Add grid-loss shutdown and recovery stabilization.
- [ ] Reset PI/ramp state after mode or communication changes.
- [ ] Confirm inverter command readback before reporting applied power.
- [ ] Simulator load-step and communication-loss tests.
- [ ] Physical FAT.

## Live Solar + Generator integration remaining

- [ ] Add Generator 1-3 meter roles to persisted configuration.
- [ ] Acquire run, breaker and ATS/synchronization evidence.
- [ ] Persist rated capacity, minimum loading, reserve and reverse margin per generator.
- [ ] Connect generator aggregation to live source evidence.
- [ ] Connect generator safe-PV cap to live inverter commands.
- [ ] Add fast curtailment after load rejection.
- [ ] Add warm-up, synchronized, loaded, cooling and stopped timers.
- [ ] Add transition stabilization and PI/ramp reset.
- [ ] Simulator tests and physical FAT.

## Live inverter integration remaining

- [ ] Execute readback after every production command.
- [ ] Apply tolerance confirmation to runtime state.
- [ ] Execute bounded retries and rollback writes.
- [ ] Mark applied output only after confirmation.
- [ ] Complete manufacturer/manual register inventory.
- [ ] Complete model-specific identity, telemetry, command and readback maps.
- [ ] Physically qualify each supported inverter profile.

## Physical release gates

- [ ] Real meter register, sign, scale and scan-rate FAT.
- [ ] One-hour browser/controller healthy, slow and failure soak.
- [ ] Meter-loss, inverter-loss, Wi-Fi-loss and restart FAT.
- [ ] Grid zero/limited-export FAT.
- [ ] Generator minimum-load and reverse-power FAT.
- [ ] Source-transfer FAT.
- [ ] Signed site SAT tied to firmware SHA, sdkconfig hash, ESP-IDF version, board MAC and timestamp.
- [ ] Production release approved.

Current result: the reusable source-state, grid policy, generator safety, multi-generator aggregation and inverter confirmation cores are implemented with host tests. Live task integration, production authentication, analyser caching and physical qualification remain blocked.
