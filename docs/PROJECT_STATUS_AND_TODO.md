# ESP32 PV-DG Controller — Project Status and TODO

Branch: `feature/multibrand-inverter-profiles`

Latest implementation head at this update: `ee63c35469a056e51490eba26e2b5e1ace094757`

## Overall status

- Software implementation for field commissioning: **COMPLETE**
- Meter scaling correction: **COMPLETE**
- Complete EM500 parameter workspace: **COMPLETE**
- Multi-meter configuration: **COMPLETE**
- Multi-inverter endpoint/rated-power configuration: **COMPLETE**
- Multi-brand inverter profile framework: **COMPLETE**
- Periodic inverter telemetry/readback engine: **COMPLETE — simulator qualified**
- SolTrix three-inverter Modbus simulator tests: **COMPLETE**
- Read-only inverter probing: **COMPLETE**
- Automatic/live physical inverter control: **LOCKED — manual and physical qualification required**
- Field qualification: **IN PROGRESS**
- Production approval: **NOT APPROVED**

## Completed software TODO

- [x] ESP-IDF 6.0.1 build gate with zero project warnings.
- [x] Full browser and source-contract CI suite.
- [x] Safe, non-destructive flash path that preserves NVS.
- [x] Meter active-power scale correction from `0.01` to `0.00001` for the observed EM500 configuration.
- [x] Corrected power used consistently by UI, telemetry and control calculations.
- [x] Live voltage, current, active/reactive/apparent power, power factor, frequency, THD and asymmetry views.
- [x] Energy counters, history and settings M01–M18.
- [x] Up to four meter profiles with duplicate endpoint validation.
- [x] Up to twelve inverter endpoint/rated-power configurations.
- [x] Manufacturer/model profile catalogue and picker.
- [x] Persistent inverter profile assignment.
- [x] Read-only inverter probe with explicit zero-write guarantee.
- [x] Fail-closed production write gate.
- [x] Automatic control disabled after meter, inverter or profile changes.
- [x] Runtime command path cannot bypass profile qualification.
- [x] Safe default profile ID corrected to match the actual catalogue entry.
- [x] Periodic FreeRTOS inverter telemetry task.
- [x] Per-inverter serialized Modbus I/O.
- [x] Profile-driven identity verification.
- [x] Profile-driven active-power decoding.
- [x] Command readback and tolerance-based mismatch tracking.
- [x] Dynamic removal of stale, offline and identity-mismatched channels from commandable capacity.
- [x] Read-only `/api/inverter-telemetry` endpoint.
- [x] Browser view for measured power, freshness, identity, readback and mismatch status.
- [x] Field test checklist added at `docs/FIELD_TEST_RELEASE_CHECKLIST.md`.

## SolTrix simulator qualification

Source contract reviewed:

- SolTrix commit `fe84696e1280788f144d170d21bd8aa6834f604d`
- Huawei simulator limit register `40125`
- Format `percent_x10`
- Function code `6`
- Simulator scenarios: normal, stale, communication loss, timeout and rollback

Dedicated firmware-repository Modbus TCP simulator:

- Port: `1502`
- Unit `21`: Huawei SUN2000 synthetic contract
- Unit `22`: GoodWe commercial synthetic contract
- Unit `23`: Solis commercial synthetic contract

Completed simulator tests:

- [x] Identity reads for all three simulated inverter units.
- [x] Signed 32-bit active-power reads and kW decoding.
- [x] Percentage command raw encoding.
- [x] Huawei register `40125` percent-x10 behavior.
- [x] Command readback.
- [x] Rollback mismatch scenario.
- [x] Timeout scenario.
- [x] Communication-loss scenario.
- [x] Firmware stale-age and dynamic-capacity-removal contracts.
- [x] Explicit proof that simulator-only profiles cannot pass the production write gate.

This is simulator evidence only. Synthetic simulator addresses for GoodWe and Solis are not manufacturer register evidence and must never be copied into a physical profile.

## Active field qualification TODO

### A. Boot and firmware health

- [ ] Pull and flash the final CI-green commit.
- [ ] Record flashed commit SHA.
- [ ] Capture one complete serial boot log.
- [ ] Confirm no panic, watchdog, abort, Guru Meditation or reboot loop.
- [ ] Confirm Wi-Fi station reconnects and the controller remains reachable.
- [ ] Confirm recovery AP remains inactive during normal operation.

### B. Meter qualification

- [ ] Verify displayed total active power against the physical meter display.
- [ ] Verify each phase voltage and current.
- [ ] Verify active, reactive and apparent power signs.
- [ ] Verify frequency and power factor.
- [ ] Verify energy counters are plausible and monotonic.
- [ ] Verify history and settings pages load without repeated Modbus errors.
- [ ] Confirm error and consecutive-failure counters remain stable.
- [ ] Record the actual CT/PT and wiring configuration used at the site.
- [ ] Treat register `0x2160` as untrusted until compared with physical source state.

### C. Physical inverter read qualification

For every configured physical inverter:

- [ ] Record manufacturer, exact model, firmware version and communication path.
- [ ] Record IP, port, unit/slave ID and rated kW.
- [ ] Select only the intended pending/read-qualified physical profile.
- [ ] Run only `Test connection (read-only)`.
- [ ] Confirm the response states `writes_issued: false`.
- [ ] Record connection result and exact error when unsuccessful.
- [ ] Compare physical inverter power with the inverter display/portal.
- [ ] Do not use a `SolTrix Simulator` profile on physical equipment.
- [ ] Do not infer control registers from simulator addresses, memory or third-party projects.

### D. Stability soak

- [ ] Run for at least 30 minutes with automatic control disabled.
- [ ] Confirm the web interface remains responsive.
- [ ] Confirm meter and inverter polling remain stable.
- [ ] Confirm no spontaneous reset.
- [ ] Confirm no continuous memory, socket or Modbus failure growth.
- [ ] Confirm stale/offline devices are visibly marked and removed from commandable capacity.
- [ ] Save screenshots and serial/API evidence.

## Blocked until exact manufacturer manuals and physical evidence

- [ ] Inventory exact inverter manuals and revisions from `SolTrix/Manuals`.
- [ ] Extract real model-specific identity and telemetry registers.
- [ ] Extract real model-specific power-limit command and readback registers.
- [ ] Verify PDU addressing, FC03/FC04/FC06/FC16, data type, word order and scaling.
- [ ] Verify unlock/enable sequence, command interval and ramp limits.
- [ ] Replace pending real profiles with exact manual-backed profiles.
- [ ] Bench-qualify each exact physical inverter model.
- [ ] Physically qualify conservative command/readback and rollback behavior.
- [ ] Approve each real profile explicitly before enabling automatic control.

## Safety gates

The following must remain false until all physical profile gates pass:

- Automatic PV-DG control
- Live physical inverter commands
- Simulator profile production writes
- Physical meter-setting writes
- Destructive meter commands
- NVS erase
- Full-flash erase

## Evidence handoff

Store or report field results using:

- Commit flashed
- Serial boot log
- `/api/status`
- `/api/config`
- `/api/meters`
- `/api/inverters`
- `/api/inverter-telemetry`
- `/api/telemetry`
- Meter live/energy/history/settings screenshots
- Inverter configuration screenshot
- Read-only inverter probe response
- 30-minute soak observations

## Release decision

Current classification:

`FIELD-COMMISSIONING READY — SIMULATOR-QUALIFIED TELEMETRY, PHYSICAL READ-ONLY QUALIFICATION`

Not yet permitted:

`AUTOMATIC PV-DG CONTROL / LIVE PHYSICAL INVERTER WRITE / PRODUCTION APPROVAL`
