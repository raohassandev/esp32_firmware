# Multi-brand inverter profile implementation TODO

Status: software framework, commissioning UI, periodic telemetry engine and SolTrix simulator qualification implemented on `feature/multibrand-inverter-profiles`; exact manufacturer manuals and physical production qualification remain gated.

## 1. Manual inventory and evidence

- [ ] Inventory exact solar inverter manuals from `raohassandev/SolTrix/Manuals`.
- [ ] Record manufacturer, exact model family, protocol, connection path and document revision.
- [ ] Extract only documented read/write registers; never infer unsupported commands.
- [ ] Record PDU addressing, function code, data type, word order, scale and units.
- [ ] Record enable/unlock sequence, timing limits and command readback requirements.
- [x] Define qualification states from documented through production approved.
- [x] Extract the SolTrix proof-simulator contract from commit `fe84696e1280788f144d170d21bd8aa6834f604d`.

## 2. Firmware profile catalogue

- [x] Compact static profile catalogue.
- [x] Manufacturer/model identifiers.
- [x] Protocol and connection metadata.
- [x] Identity, telemetry, command and readback descriptors.
- [x] Minimum/maximum command limits.
- [x] Profile lookup and validation.
- [x] Central production-approval write gate.
- [x] Generic U16/S16/U32/S32 and AB/BA decoder.
- [x] Generic command/readback tolerance comparator.
- [x] Explicit simulator-only profile classification that can never pass the production write gate.
- [x] SolTrix simulator profiles for Huawei, GoodWe and Solis synthetic contracts.
- [ ] Replace pending real manufacturer family entries with exact manual-backed profiles.

## 3. Configuration and user interface

- [x] Persistent profile assignment per inverter channel.
- [x] Safe custom/pending default for existing configurations.
- [x] Manufacturer and model-family picker.
- [x] Inverter channel picker.
- [x] Profile assignment validation.
- [x] Automatic control disabled after profile changes.
- [x] Restart-required response.
- [x] Qualification and write-lock state displayed.
- [x] Raw registers hidden from the normal picker.
- [x] Full 12-channel inverter endpoint/rated-power editor.
- [x] Complete inverter-array persistence through a dedicated API.
- [x] Duplicate enabled endpoint rejection.
- [x] Removed inverter slots cleared on save.
- [x] Command-register fields excluded from the normal commissioning editor.
- [x] Live inverter telemetry/readback browser panel.

## 4. Inverter manager

- [x] Resolve saved profile during initialization.
- [x] Exclude non-production-approved profiles from commandable capacity.
- [x] Reject all command attempts when no online production-approved channel exists.
- [x] Use profile command metadata instead of legacy raw-register fields.
- [x] Read-only profile probe with zero Modbus writes.
- [x] Runtime state fields for telemetry, readback and mismatch reporting.
- [x] Periodic FreeRTOS telemetry task.
- [x] Profile-defined identity-value matching.
- [x] Profile-driven active-power decoding.
- [x] Command readback execution and tolerance-based mismatch tracking.
- [x] Remove stale/offline/identity-mismatched channels dynamically from commandable capacity.
- [x] Serialize telemetry, probe and command Modbus traffic with a per-inverter I/O mutex.
- [ ] Per-profile command interval and ramp enforcement after real manuals define limits.

## 5. Web API

- [x] `GET /api/inverter-profiles`.
- [x] `GET /api/inverter-telemetry` with explicit `read_only_endpoint: true` and `writes_issued: false`.
- [x] `POST /api/inverter-profile-assignment`.
- [x] `POST /api/inverter-probe` with explicit `writes_issued: false`.
- [x] `POST /api/inverters/config` for full endpoint/rating persistence.
- [x] Manufacturer/model picker and read-only test action.
- [x] Full inverter configuration editor.
- [x] Decoded telemetry, identity, freshness, readback and mismatch fields for simulator/read-qualified profiles.
- [ ] Profile import/export bundled with exact manual-backed profile metadata.

## 6. Simulator tests and release gates

- [x] Profile catalogue safety contract.
- [x] Profile API and persistence contract.
- [x] Browser picker contract.
- [x] Full inverter configuration safety contract.
- [x] Runtime write-gate contract.
- [x] Read-only probe contract.
- [x] Generic decoder/readback contract.
- [x] SolTrix Modbus TCP simulator on port 1502 with units 21, 22 and 23.
- [x] Huawei simulator identity, active power, register 40125 percent-x10 command encoding and readback test.
- [x] GoodWe simulator identity, active power, command encoding and readback test.
- [x] Solis simulator identity, active power, command encoding and readback test.
- [x] Normal, rollback, timeout and communication-loss scenarios.
- [x] Firmware stale-data age gate and dynamic capacity-removal contract.
- [x] ESP-IDF v6.0.1 build gate with zero project warnings.
- [ ] Bench read qualification for each exact physical model family.
- [ ] Bench command/readback qualification for each real writable profile.
- [ ] Explicit production approval before automatic PV-DG control.

## Release truth

The reusable multi-brand architecture, complete endpoint/rating editor, profile picker, persistent assignments, periodic telemetry, identity verification, active-power decoding, readback/mismatch tracking, stale/offline capacity removal, read-only APIs and SolTrix simulator harness are implemented.

The dedicated Modbus simulator is derived from the SolTrix proof contract and is executed in the firmware CI. It is synthetic simulator evidence only, not manufacturer manual evidence and not physical inverter proof. Simulator-only profiles are explicitly excluded from production writes.

Real Huawei, GoodWe, Solis and FoxESS/Knox production control remains locked until the exact manuals are enumerated and each model-specific read/write map is bench and field qualified. Automatic PV-DG control must remain disabled until explicit production approval.
