# Multi-brand inverter profile implementation TODO

Status: software framework, commissioning UI, periodic telemetry engine, SolTrix simulator qualification, manual-source inventory and fail-closed profile-assignment backup/restore are implemented. Exact deployment applicability and physical production qualification remain gated.

## 1. Manual inventory and evidence

- [x] Inventory the current `raohassandev/SolTrix/Manuals/Inverter` tree and record immutable manual/protocol source SHAs. See `docs/INVERTER_MANUAL_INVENTORY.md`.
- [x] Recheck current official public manufacturer discovery sources for Huawei, GoodWe, Solis and Growatt without promoting non-authoritative register maps.
- [ ] For each actual deployment, record manufacturer, exact model, inverter firmware, protocol, connection path and accepted official document revision/digest.
- [ ] Extract only documented read/write registers from the accepted exact applicable manual; never infer unsupported commands.
- [ ] Record PDU addressing, function code, data type, word order, scale and units for each accepted exact profile.
- [ ] Record enable/unlock sequence, timing limits, legal raw/engineering ranges and command readback requirements.
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
- [ ] Replace pending real manufacturer family entries only after an exact model/firmware/manual identity is selected and its register map is accepted.

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
- [x] Engineering UI export/import for all 12 compiled profile assignments with explicit restart/control-disable warning.

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
- [x] Atomic all-channel assignment persistence: validate every compiled profile ID first, disable persisted automatic control, then commit the whole assignment map once.
- [ ] Per-profile command interval and ramp enforcement after the accepted real manual defines limits.

## 5. Web API

- [x] `GET /api/inverter-profiles`.
- [x] `GET /api/inverter-telemetry` with explicit `read_only_endpoint: true` and `writes_issued: false`.
- [x] `POST /api/inverter-profile-assignment`.
- [x] `POST /api/inverter-probe` with explicit `writes_issued: false`.
- [x] `POST /api/inverters/config` for full endpoint/rating persistence.
- [x] Manufacturer/model picker and read-only test action.
- [x] Full inverter configuration editor.
- [x] Decoded telemetry, identity, freshness, readback and mismatch fields for simulator/read-qualified profiles.
- [x] `GET/POST /api/inverter-profile-manifest` assignment backup/restore. Every imported entry must match the currently compiled profile ID, manufacturer, model family, protocol, connection, qualification, manual reference, simulator classification and definition fingerprint. Dynamic register definitions, qualification and production approval are not importable.

## 6. Simulator tests and release gates

- [x] Profile catalogue safety contract.
- [x] Profile API and persistence contract.
- [x] Profile assignment manifest fail-closed source contract in always-on Firmware/Web CI.
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
- [x] Fail-closed physical qualification evidence validator, including exact manufacturer/model/firmware/manual/controller/endpoint identity and immutable evidence digests (PR #158 + PR #193).
- [ ] Bench read qualification for each exact physical model family selected for deployment.
- [ ] Bench command/readback/failure/rollback qualification for each real writable profile.
- [ ] Signed production approval before automatic PV-DG control.

## Release truth

The reusable multi-brand architecture, endpoint/rating editor, profile picker, persistent assignments, atomic assignment manifest backup/restore, periodic telemetry, identity verification, active-power decoding, readback/mismatch tracking, stale/offline capacity removal, read-only APIs, simulator harness and physical-evidence validator are implemented.

The assignment manifest is deliberately **not** a dynamic profile format. It can only restore choices among profile definitions already compiled into the exact firmware. Import compares descriptive identity plus a definition fingerprint and then disables both the running control task and persisted automatic control before one atomic NVS assignment-map write. It cannot import register maps, qualification state or production approval.

The current SolTrix inverter-manual tree has been inventoried. That inventory identifies useful Huawei, Growatt, Solis, ASW/Knox, CPS/Chint, SMA, SolarEdge, SolaX, Sungrow and other source documents, but document presence alone does not establish exact installed-model/firmware applicability or production control authority.

The dedicated Modbus simulator is synthetic evidence only, not manufacturer manual evidence and not physical inverter proof. Simulator-only profiles are explicitly excluded from production writes.

Real manufacturer production control remains locked until each deployed inverter has an exact accepted manual/profile identity, physical read-only proof, controlled write/readback/failure/rollback evidence and signed production approval. Automatic PV-DG control must remain disabled until explicit production approval.