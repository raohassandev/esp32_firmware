# Multi-brand inverter profile implementation TODO

Status: active implementation roadmap for the ESP32 PV-DG controller.

## 1. Manual inventory and evidence

- [ ] Inventory solar inverter manuals from `raohassandev/SolTrix/Manuals`.
- [ ] Record manufacturer, family, protocol, supported connection path and document source.
- [ ] Extract only documented read/write registers; do not infer unsupported commands.
- [ ] Record register address convention, function code, data type, word order, scale and units.
- [ ] Record required enable/unlock sequence, timing limits and readback requirements.
- [ ] Assign qualification state: documented, simulator verified, bench verified, read-only qualified, write qualified, production approved.

## 2. Firmware profile catalogue

- [ ] Add compact static inverter profile definitions.
- [ ] Add manufacturer and model-family identifiers.
- [ ] Add protocol and connection-mode metadata.
- [ ] Add telemetry, command and readback register descriptors.
- [ ] Add per-profile minimum/maximum command and ramp limits.
- [ ] Add profile lookup and validation APIs.
- [ ] Keep unsupported or unqualified write paths locked.

## 3. Configuration model

- [ ] Add `profile_id` to inverter configuration.
- [ ] Preserve advanced custom-register mode for unsupported devices.
- [ ] Validate endpoint, slave ID, rated power, profile compatibility and duplicate endpoints.
- [ ] Preserve import/export compatibility.
- [ ] Force automatic control disabled after any inverter configuration change.

## 4. Inverter manager

- [ ] Resolve selected profile at initialization.
- [ ] Add safe identity/read-only probe where documented.
- [ ] Use common profile-driven read/write helpers.
- [ ] Add command readback and mismatch detection.
- [ ] Remove failed channels from available controllable capacity.
- [ ] Add per-inverter and aggregate ramp limits.
- [ ] Keep live command execution disabled for non-production-approved profiles.

## 5. Web API and UI

- [ ] Add read-only profile catalogue endpoint.
- [ ] Add manufacturer then model-family picker.
- [ ] Show connection requirements and qualification status.
- [ ] Hide raw registers in normal mode.
- [ ] Add advanced custom profile mode.
- [ ] Add safe connection test with no writes.
- [ ] Add import/export support for inverter profiles.
- [ ] Display write eligibility, command readback and mismatch state.

## 6. Tests and release gates

- [ ] Profile catalogue source contract.
- [ ] Configuration round-trip tests.
- [ ] Duplicate endpoint and invalid-profile rejection tests.
- [ ] Browser picker tests.
- [ ] Safety contract proving unqualified profiles cannot write.
- [ ] Simulator tests for command encoding and readback.
- [ ] ESP-IDF build with zero project warnings.
- [ ] Bench qualification for each supported writable profile.
- [ ] Explicit approval before enabling production automatic control.
