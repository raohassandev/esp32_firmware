# ESP32 PV-DG Controller — Project Status and TODO

Branch: `feature/multibrand-inverter-profiles`

Latest validated commit at creation: `1ed324893a77b6d9821049c488ed3667e05d8348`

## Overall status

- Software implementation for field commissioning: **COMPLETE**
- Meter scaling correction: **COMPLETE**
- Complete EM500 parameter workspace: **COMPLETE**
- Multi-meter configuration: **COMPLETE**
- Multi-inverter endpoint/rated-power configuration: **COMPLETE**
- Multi-brand inverter profile framework: **COMPLETE**
- Read-only inverter probing: **COMPLETE**
- Automatic/live inverter control: **LOCKED — physical qualification required**
- Field qualification: **IN PROGRESS**
- Production approval: **NOT APPROVED**

## Completed software TODO

- [x] ESP-IDF 6.0.1 build with zero project warnings.
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
- [x] Field test checklist added at `docs/FIELD_TEST_RELEASE_CHECKLIST.md`.

## Active field qualification TODO

### A. Boot and firmware health

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

### C. Inverter read-only qualification

For every configured inverter:

- [ ] Record manufacturer, exact model, firmware version and communication path.
- [ ] Record IP, port, unit/slave ID and rated kW.
- [ ] Select the intended profile.
- [ ] Run only `Test connection (read-only)`.
- [ ] Confirm the response states `writes_issued: false`.
- [ ] Record connection result and exact error when unsuccessful.
- [ ] Do not infer or enter control registers from memory or third-party projects.

### D. Stability soak

- [ ] Run for at least 30 minutes with automatic control disabled.
- [ ] Confirm the web interface remains responsive.
- [ ] Confirm meter polling remains stable.
- [ ] Confirm no spontaneous reset.
- [ ] Confirm no continuous memory, socket or Modbus failure growth.
- [ ] Save screenshots and serial/API evidence.

## Blocked until exact manual and physical evidence

- [ ] Inventory exact inverter manuals and revisions from `SolTrix/Manuals`.
- [ ] Extract model-specific identity and telemetry registers.
- [ ] Extract model-specific power-limit command and readback registers.
- [ ] Verify PDU addressing, FC03/FC04/FC06/FC16, data type, word order and scaling.
- [ ] Verify unlock/enable sequence and command timing limits.
- [ ] Implement periodic profile-driven telemetry for each exact qualified family.
- [ ] Implement identity-match rejection.
- [ ] Implement command readback execution and mismatch handling.
- [ ] Implement stale/offline inverter removal from available capacity.
- [ ] Bench-qualify each exact inverter model.
- [ ] Physically qualify command/readback with conservative limits.
- [ ] Approve each profile explicitly before enabling automatic control.

## Safety gates

The following must remain false until all profile-specific physical gates pass:

- Automatic PV-DG control
- Live inverter commands
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
- `/api/telemetry`
- Meter live/energy/history/settings screenshots
- Inverter configuration screenshot
- Read-only inverter probe response
- 30-minute soak observations

## Release decision

Current classification:

`FIELD-COMMISSIONING READY — READ-ONLY QUALIFICATION ONLY`

Not yet permitted:

`AUTOMATIC PV-DG CONTROL / LIVE INVERTER WRITE / PRODUCTION APPROVAL`
