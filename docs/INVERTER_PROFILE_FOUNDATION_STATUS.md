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
- Full inverter endpoint/rating editor and safe profile picker.
- Read-only profile probe with zero writes.
- Periodic identity/active-power/readback telemetry with stale/offline capacity removal.
- Generic U16/S16/U32/S32 decoding, AB/BA word order and tolerance-based readback comparison.
- Per-inverter Modbus I/O serialization.
- Profile API, telemetry API and configuration persistence.
- Source-contract, API, browser, runtime write-gate, simulator, stale-data and ESP-IDF build regressions.
- Fail-closed physical qualification evidence tooling from PR #158, hardened by PR #193 to exact manufacturer/model/firmware/manual/controller/endpoint identity, immutable evidence and signed approval.
- Current `raohassandev/SolTrix/Manuals/Inverter` file inventory with immutable source SHAs in `docs/INVERTER_MANUAL_INVENTORY.md`.

## Current authority boundary

All real manufacturer profiles remain non-commandable. The manual inventory establishes what source material exists; it does not establish that a particular document applies to a particular installed inverter/firmware or that any write is safe.

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
- add profile import/export only with exact manual/profile identity and digest;
- execute bench read qualification;
- execute bench write/readback/failure/rollback qualification;
- obtain signed production approval.

Do not add guessed status registers, control addresses, scales, timings or command values merely to make a pending profile look complete. No live manufacturer inverter write is enabled by the manual inventory work.