# Multi-brand inverter profile implementation TODO

Status: software framework and commissioning UI implemented on `feature/multibrand-inverter-profiles`; exact manual-specific profiles and physical qualification remain gated.

## 1. Manual inventory and evidence

- [ ] Inventory exact solar inverter manuals from `raohassandev/SolTrix/Manuals`.
- [ ] Record manufacturer, exact model family, protocol, connection path and document revision.
- [ ] Extract only documented read/write registers; never infer unsupported commands.
- [ ] Record PDU addressing, function code, data type, word order, scale and units.
- [ ] Record enable/unlock sequence, timing limits and command readback requirements.
- [x] Define qualification states from documented through production approved.

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
- [ ] Replace pending family entries with exact manual-backed profiles.

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

## 4. Inverter manager

- [x] Resolve saved profile during initialization.
- [x] Exclude non-production-approved profiles from commandable capacity.
- [x] Reject all command attempts when no production-approved channel exists.
- [x] Use profile command metadata instead of legacy raw-register fields.
- [x] Read-only profile probe with zero Modbus writes.
- [x] Runtime state fields for telemetry, readback and mismatch reporting.
- [ ] Periodic telemetry task after exact read maps exist.
- [ ] Identity-value matching after exact expected identifiers exist.
- [ ] Command readback execution after exact write/readback maps are bench qualified.
- [ ] Remove stale/offline channels dynamically after profile telemetry is enabled.
- [ ] Per-profile command interval and ramp enforcement after manuals define limits.

## 5. Web API

- [x] `GET /api/inverter-profiles`.
- [x] `POST /api/inverter-profile-assignment`.
- [x] `POST /api/inverter-probe` with explicit `writes_issued: false`.
- [x] `POST /api/inverters/config` for full endpoint/rating persistence.
- [x] Manufacturer/model picker and read-only test action.
- [x] Full inverter configuration editor.
- [ ] Profile import/export bundled with exact manual-backed profile metadata.
- [ ] Decoded telemetry and command-readback fields after exact maps exist.

## 6. Tests and release gates

- [x] Profile catalogue safety contract.
- [x] Profile API and persistence contract.
- [x] Browser picker contract.
- [x] Full inverter configuration safety contract.
- [x] Runtime write-gate contract.
- [x] Read-only probe contract.
- [x] Generic decoder/readback contract.
- [x] ESP-IDF v6.0.1 build gate with zero project warnings.
- [ ] Simulator tests for each exact profile.
- [ ] Bench read qualification for each exact model family.
- [ ] Bench command/readback qualification for each writable profile.
- [ ] Explicit production approval before automatic PV-DG control.

## Release truth

The reusable multi-brand architecture, complete endpoint/rating editor, profile picker, persistence, safe read probe and fail-closed command gate are implemented. The branch is suitable for field commissioning and read-only communication qualification after CI passes. Exact manufacturer control remains intentionally locked until the actual manuals are enumerated and each model-specific read/write map is physically qualified. No pending manufacturer profile can issue a write, and no production automatic-control claim is allowed without command/readback evidence.
