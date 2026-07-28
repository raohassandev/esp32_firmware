# Current Execution TODO

**Branch:** `feature/multibrand-inverter-profiles`  
**Current software head:** `7fc13a14dccd7d0beef556a21b1f9857655a4b8a`  
**CI:** queued for the current head.

This file is the concise current execution register. `MASTER_EXECUTION_TODO.md` remains the complete product scope.

## Completed and software-verified

- [x] Correct recursive Meters-page error observer.
- [x] Preserve Retry and interactive diagnostics controls.
- [x] Correct Alarms-page observer recursion.
- [x] Add bounded/cancellable browser meter requests.
- [x] Remove duplicate full EM500 consistency polling.
- [x] Disable build-time Wi-Fi provisioning by default.
- [x] Remove compiled primary/fallback station credentials.
- [x] Reject NaN and infinity in Modbus numeric decoding.
- [x] Reject non-finite meter and inverter boundary values.
- [x] Fail control calculations toward zero on invalid source data.
- [x] Move operational history/event JSON allocation outside spinlocks.
- [x] Add cumulative Modbus connect/send/receive transaction deadline.
- [x] Bypass DNS for literal IPv4 endpoints.
- [x] Bound Modbus endpoint timeout configuration.
- [x] Handle interrupted socket operations.
- [x] Clamp configured meter count to firmware capacity.
- [x] Prevent degraded polling from becoming faster than normal polling.
- [x] Expire inverter identity after stale/disconnect/write failure.
- [x] Reverify inverter identity before later command eligibility.
- [x] Build one immutable eligible-inverter snapshot per command cycle.
- [x] Validate inverter command scale, width, range and finiteness.
- [x] Support profile-declared one-word and two-word commands.
- [x] Record command state only after a successful write.
- [x] Enforce aggregate commanded-power cap.
- [x] Return a complete HTTP 401 denial response.
- [x] Add and pass Modbus/numeric runtime safety contract.
- [x] Align and pass inverter runtime write-gate contract.
- [x] Preserve schema-2 commissioned Wi-Fi credentials against build provisioning.
- [x] Preserve schema-1 migrated credentials against build provisioning.
- [x] Add bounded configuration JSON nesting validation before cJSON parsing.
- [x] Prevent generic `/api/config` import from enabling automatic control.
- [x] Validate imported meter, inverter, endpoint and control numeric values.
- [x] Add and pass configuration migration/import safety contract.
- [x] Add conservative Grid/Generator source-state evaluator.
- [x] Add explicit Grid Only, Generator Only, synchronized, Island, Transfer, No Source, Conflict and Unknown states.
- [x] Fail closed on stale or conflicting source evidence.
- [x] Add generator safe-PV calculation using minimum loading, reserve and reverse-power margin.
- [x] Add executable host unit tests for source-state and generator-limit calculations.
- [x] Compile the source-state module into the ESP-IDF control component.

## Validation in progress

- [ ] Source-mode host unit test on GitHub Actions for `7fc13a14dccd7d0beef556a21b1f9857655a4b8a`.
- [ ] Complete web/source-contract suite for the current head.
- [ ] ESP-IDF 6.0.1 build for the current head.
- [ ] Zero compiler-warning confirmation.

## P0 remaining software blockers

- [ ] Apply bounded JSON nesting validation to every remaining independent JSON parser.
- [ ] Add request-body receive deadlines consistently across all write endpoints.
- [ ] Return JSON null—not `0.00`—for unavailable status power values.
- [ ] Restore production salted-password/session authentication.
- [ ] Add production build failure when authentication bypass is enabled.
- [ ] Protect every configuration, restart, profile-assignment and control endpoint.

## Meter and web architecture remaining

- [ ] Move full EM500 scans out of synchronous HTTP handlers.
- [ ] Add dedicated meter acquisition scheduler.
- [ ] Cache fast operational electrical values.
- [ ] Cache medium/slow analyser and energy groups.
- [ ] Add bounded background jobs for history/setup/raw diagnostics.
- [ ] Expose group freshness, quality, response time and active backoff.
- [ ] Preserve and label last-good full-analyser data.
- [ ] Preserve Modbus exception function/code in diagnostics.
- [ ] Qualify persistent, reconnect-on-error and per-transaction socket modes.
- [ ] Perform TCP PCB/TIME_WAIT endurance test.
- [ ] Stop Wi-Fi scan polling after route exit or deadline.
- [ ] Add timeout/finally protection to every remaining web poller.

## Solar + Grid remaining

- [ ] Integrate source-state result into the live control loop.
- [ ] Zero-export mode.
- [ ] Limited-export mode.
- [ ] Minimum grid-import hold.
- [ ] Signed grid-meter orientation commissioning.
- [ ] PI anti-windup and independent ramp rates.
- [ ] Grid-loss/recovery sequence.
- [ ] Command readback before applied-power confirmation.
- [ ] Simulator load-step and communication-loss tests.
- [ ] Physical FAT.

## Solar + Generator remaining

- [x] Pure generator minimum-loading/reserve/reverse-margin safe-PV calculation.
- [ ] Generator 1–3 meter roles.
- [ ] Generator run and breaker feedback acquisition.
- [ ] Persist rated capacity, minimum loading and reserve/headroom configuration.
- [ ] Integrate minimum generator-loading limit into live inverter commands.
- [ ] Add fast PV curtailment after load rejection.
- [ ] Multi-generator available-capacity calculation from live evidence.
- [ ] Warm-up, synchronized, loaded, cooling and stopped timers.
- [ ] Simulator tests and physical FAT.

## Grid/Generator source transitions

- [x] Deterministic pure source-state classification.
- [x] Grid only.
- [x] Generator only.
- [x] Grid + generator synchronized.
- [x] Island.
- [x] Transfer in progress.
- [x] No source.
- [x] Conflict.
- [x] Unknown.
- [ ] Connect real breaker/run/ATS evidence to the source-state engine.
- [ ] Add transition stabilization timers and PI/ramp reset.

## Inverter qualification remaining

- [ ] Exact manufacturer/manual register inventory.
- [ ] Model-specific identity, telemetry, command and readback maps.
- [ ] Command timeout/readback/rollback implementation and tests.
- [ ] Physical write/readback qualification per supported model.
- [ ] Signed production approval per profile.

## Physical release gates

- [ ] Real meter register, sign, scale and scan-rate FAT.
- [ ] Browser/controller one-hour healthy/slow/failure soak.
- [ ] Meter-loss, inverter-loss, Wi-Fi-loss and restart FAT.
- [ ] Grid zero/limited-export FAT.
- [ ] Generator minimum-load/reverse-power FAT.
- [ ] Source-transfer FAT.
- [ ] Signed site SAT tied to exact firmware SHA, sdkconfig hash, ESP-IDF version, board MAC and timestamp.

## Release state

- [ ] Production release approved.

Current result: the conservative source-state and generator safe-limit core is implemented and executable-tested in CI. Live hardware evidence integration, production authentication, analyser caching and physical FAT/SAT remain release blockers.
