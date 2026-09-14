# Inverter profile foundation status

Current integration target: `dev`

## Implemented

- Compact profile catalogue API in the inverter manager component.
- Manufacturer/model-family catalogue entries with fail-closed pending states for real manufacturer profiles.
- Explicit qualification lifecycle from documented through production approved.
- Central read eligibility and write eligibility functions.
- Hard write rule: a real profile must contain verified identity, command and readback mappings and be explicitly production approved.
- Simulator-only Huawei/GoodWe/Solis contracts that can never pass the production write gate.
- Backward-compatible persistent profile assignment per inverter channel.
- Atomic all-channel profile assignment backup/restore. Export binds every channel to the exact compiled profile identity and definition fingerprint; import validates all channels before one NVS commit, stops the running control task, disables persisted automatic control and requires restart.
- Dynamic register definitions, qualification state and production approval are explicitly non-importable.
- Full inverter endpoint/rating editor and safe profile picker.
- Read-only profile probe with zero writes.
- Periodic identity/active-power/readback telemetry with stale/offline capacity removal.
- Generic U16/S16/U32/S32 decoding, AB/BA word order and tolerance-based readback comparison.
- Per-inverter Modbus I/O serialization.
- Profile API, telemetry API, assignment-manifest API and configuration persistence.
- Source-contract, API, browser, manifest-safety, runtime write-gate, simulator, stale-data and ESP-IDF build regressions.
- Fail-closed physical qualification evidence tooling from PR #158, hardened by PR #193 to exact manufacturer/model/firmware/manual/controller/endpoint identity, immutable evidence and signed approval.
- Current `raohassandev/SolTrix/Manuals/Inverter` file inventory with immutable source SHAs in `docs/INVERTER_MANUAL_INVENTORY.md`.

## Current authority boundary

All real manufacturer profiles remain non-commandable. The manual inventory establishes what source material exists; it does not establish that a particular document applies to a particular installed inverter/firmware or that any write is safe.

The assignment backup is deliberately not a profile-definition import format. A saved file can restore choices among definitions already compiled in the exact controller firmware. It cannot introduce an address, scale, status mapping, qualification state or production approval. A manifest whose profile definition fingerprint or descriptive identity no longer matches the running firmware is rejected in full rather than partially applied.

Before a pending real profile can replace a generic catalogue entry, Issue #82 requires:

1. exact installed manufacturer/model/firmware identity;
2. accepted official applicable manual revision/document digest;
3. exact transport/topology, endpoint and Unit ID;
4. documented identity/status/telemetry/command/readback map with type/word-order/scale/range semantics;
5. physical read-only identity and telemetry proof;
6. controlled command/readback, failure, safe-zero and rollback proof;
7. reconnect/revalidation evidence;
8. signed production approval repeating the complete exact identity and evidence digest.

## Remaining implementation work

The remaining work is driven by real deployment evidence, not generic framework code:

- select the exact installed inverter identities for each deployment;
- extract the accepted exact applicable register definitions from the inventoried/official source documents;
- replace a pending catalogue entry only when that exact profile has enough documented metadata for safe read-only qualification;
- add manual-backed command interval/ramp limits only where the accepted document defines them;
- execute bench read qualification;
- execute bench write/readback/failure/rollback qualification;
- obtain signed production approval.

Do not add guessed status registers, control addresses, scales, timings or command values merely to make a pending profile look complete. No live manufacturer inverter write is enabled by manual discovery or assignment backup/restore.