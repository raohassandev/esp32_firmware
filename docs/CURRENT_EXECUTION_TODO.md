# Current Execution TODO

**Branch:** `feature/multibrand-inverter-profiles`  
**Verified software head:** `b6f51f910769000a10754a23305b8d88ef545506`  
**CI:** complete web suite passed; ESP-IDF 6.0.1 build passed with zero project warnings.

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
- [x] Complete web CI and ESP-IDF 6.0.1 build for the verified head.

## P0 remaining software blockers

- [ ] Preserve schema-2 commissioned Wi-Fi credentials against later build-provision generations.
- [ ] Add schema migration regression tests.
- [ ] Add bounded JSON nesting validation before every cJSON parse.
- [ ] Prevent generic `/api/config` import from enabling automatic control.
- [ ] Validate every imported numeric control/configuration value for range and finiteness.
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

- [ ] Generator 1–3 meter roles.
- [ ] Generator run and breaker feedback model.
- [ ] Rated capacity, minimum loading and reserve/headroom configuration.
- [ ] Minimum generator-loading controller.
- [ ] Reverse-power prevention.
- [ ] Fast PV curtailment after load rejection.
- [ ] Multi-generator available-capacity calculation.
- [ ] Warm-up, synchronized, loaded, cooling and stopped states.
- [ ] Stale/conflicting-evidence fail-safe.
- [ ] Simulator tests and physical FAT.

## Grid/Generator source transitions remaining

- [ ] Grid only.
- [ ] Generator only.
- [ ] Grid + generator synchronized.
- [ ] Island.
- [ ] Transfer in progress.
- [ ] No source.
- [ ] Conflict.
- [ ] Unknown.
- [ ] Transition stabilization timers and PI/ramp reset.

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

Current result: software head builds and all current source/simulator contracts pass, but production release remains blocked by authentication, acquisition architecture, Solar-Generator implementation and physical FAT/SAT evidence.
