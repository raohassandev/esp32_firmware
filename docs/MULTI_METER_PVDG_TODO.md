# Multi-meter and PV-DG implementation TODO

This checklist is authoritative for the `feature/pvdg-multi-meter-commissioning` workstream.

## A. Physical meter discovery

- [x] Confirm meter gateway `192.168.0.200:502`.
- [x] Confirm available slave IDs `1`, `2`, `3`.
- [x] Confirm device mix: two EM500 meters and one Carlo Gavazzi WM15.
- [x] Confirm ESP32 current IP `192.168.0.102` on `Tenda_69B540`.
- [x] Confirm slave 1 responds to EM500 instantaneous registers.
- [ ] Identify the exact meter model assigned to each slave ID using Function 17/43 and register fingerprints.
- [ ] Capture ten stable read samples for every fast-control register on all three slaves.
- [ ] Verify total active-power sign and scale against an independent reference.
- [ ] Verify direct-PDU versus one-based addressing per meter profile.
- [ ] Verify `0x2160` / decimal 8544 with at least 100 energized/de-energized transitions.
- [ ] Determine whether `0x2160` uses FC03 or FC04 and whether it exists on all EM500 units.
- [ ] Verify WM15 measurement and setup maps from the exact Carlo Gavazzi model manual.

## B. Register catalogue and decoding

- [x] Document EM500 instantaneous voltage/current/power/PF/frequency registers.
- [x] Document THD and asymmetry registers.
- [x] Document maximum, minimum, average and maximum-demand blocks.
- [x] Document total, partial, tariff and phase-energy blocks.
- [x] Document CT, PT/VT and wiring setup registers.
- [x] Document communication, limits, alarms, counters, inputs, outputs and power-quality menus.
- [ ] Add U64 four-word energy decoding to the Modbus library.
- [ ] Add per-register signedness, word count, scale and engineering-unit metadata.
- [ ] Add profile-specific unsupported-register handling as JSON `null`.
- [ ] Add configurable `address_base` to every meter profile.
- [ ] Add typed register groups: `control_fast`, `telemetry_fast`, `telemetry_slow`, `energy`, `setup`.

## C. Multi-meter configuration

- [ ] Fix JSON import so it validates and imports up to `APP_MAX_METERS` instead of only `meters[0]`.
- [ ] Preserve unrelated Wi-Fi, inverter and control configuration during meter-only updates.
- [ ] Reject duplicate `(host, port, unit_id)` meter endpoints unless explicitly allowed.
- [ ] Validate name, host, port, unit ID, timeout, function, address, type, word order, scale and poll interval.
- [ ] Add meter roles: `grid`, `generator`, `generator_2`, `load`, `auxiliary`.
- [ ] Add import/export round-trip tests for three meters.
- [ ] Add per-meter connection and polling-rate budget checks.
- [ ] Add browser UI to add, edit, disable, reorder, import and export meter profiles.
- [ ] Keep automatic control disabled after any meter configuration change.

## D. Meter setup UI and API

- [ ] Add read-only setup snapshot endpoint.
- [ ] Add model/fingerprint endpoint and compatibility classification.
- [ ] Show CT primary/secondary, rated voltage, PT use, PT primary/secondary and wiring mode.
- [ ] Show integration, communication, limits, alarms, counters, inputs, outputs and power-quality settings.
- [ ] Show tariff-energy counters and active tariff mode.
- [ ] Gate setup writes behind service/admin authorization.
- [ ] Require control disabled and inverter writes inhibited before meter setup writes.
- [ ] Implement snapshot -> validate -> write -> readback -> save -> reconnect -> verify transaction.
- [ ] Add rollback from the pre-write snapshot.
- [ ] Add audit log with operator, meter, timestamp and exact before/after values.
- [ ] Keep reset-energy, restore-defaults, reboot and wiring-test commands in a separate maintenance workflow.
- [ ] Do not expose destructive commands on normal operator screens.

## E. Source detection

- [x] Define source states and conflict/fail-safe behavior.
- [x] Define default `0 = grid`, `1 = generator` mapping for `0x2160`.
- [ ] Make register, function, address base, polarity, debounce and stale timeout configurable.
- [ ] Poll source input in the fast loop without delaying power-meter reads.
- [ ] Cross-check digital source state against grid/generator voltage, frequency, current and kW.
- [ ] Add `GRID`, `GENERATOR`, `TRANSFER`, `NONE`, `CONFLICT` and `FAULT` runtime states.
- [ ] Add startup and transfer stabilization timers.
- [ ] Add mismatch and chatter alarms.
- [ ] Command PV zero for `NONE`, `CONFLICT`, stale and invalid-source states.

## F. Grid control

- [x] Specify positive-import / negative-export sign convention.
- [x] Specify zero-export target with positive import bias.
- [x] Specify limited-export target.
- [x] Specify limited-import upper bound and unachievable alarm.
- [ ] Implement configurable grid operating window.
- [ ] Implement bounded PI with anti-windup and bumpless transfer.
- [ ] Implement normal and emergency export thresholds.
- [ ] Add hard export fault and supervised recovery.
- [ ] Add simulator tests for load steps, meter noise and PV saturation.

## G. Generator control

- [x] Specify percent, fixed-kW and maximum-of-both minimum-loading modes.
- [x] Specify reverse-power warning and trip layers.
- [x] Specify generator overload support and non-critical load-shed request.
- [x] Specify multi-generator aggregate extension.
- [ ] Implement generator warmup and cooldown states.
- [ ] Implement generator minimum-loading target.
- [ ] Implement faster PV decrease than increase.
- [ ] Implement reverse-power emergency PI bypass.
- [ ] Add latched reverse-power fault and manual/supervised recovery.
- [ ] Integrate external generator-protection status where available.
- [ ] Add simulator tests for reverse-power and overload steps.
- [ ] Verify a dedicated generator relay/controller remains the final reverse-power protection layer.

## H. Inverter control safety

- [ ] Verify manufacturer-specific limit write and readback registers.
- [ ] Add command/readback mismatch detection.
- [ ] Remove failed inverter channels from available capacity.
- [ ] Add per-inverter and aggregate ramp limits.
- [ ] Keep live inverter control disabled until bench qualification passes.
- [ ] Add an approved hard PV inhibit/trip output where required by the site design.

## I. UI, monitoring and logging

- [ ] Display raw and debounced source input.
- [ ] Display detected source and supporting electrical evidence.
- [ ] Display grid/gen target, measured kW, control error and final PV command.
- [ ] Display generator reverse margin and minimum-load margin.
- [ ] Display grid import/export margin.
- [ ] Display all phase measurements, THD, asymmetry, energy and meter settings.
- [ ] Log one-minute operational history plus event-resolution fault records.
- [ ] Record source transfers, stale data, export events, reverse events and command failures.
- [ ] Add CSV export for measurements, energy and events.

## J. Validation and release

- [ ] Pass static checks and source-contract tests.
- [ ] Pass ESP-IDF v6.0.1 build with zero project warnings.
- [ ] Pass three-meter 30-minute read-only soak test.
- [ ] Pass source-input 100-transition test.
- [ ] Pass all grid-mode simulator scenarios.
- [ ] Pass all generator-mode simulator scenarios.
- [ ] Pass physical zero-export load-step test.
- [ ] Pass physical generator minimum-loading and reverse-response test.
- [ ] Verify no configuration mutation during read-only diagnostics.
- [ ] Verify final `/api/config` matches the approved commissioning backup.
- [ ] Obtain explicit approval before enabling automatic control or merging.