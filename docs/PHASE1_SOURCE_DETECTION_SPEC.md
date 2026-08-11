# Phase 1 — EM500 source detection (grid vs generator)

**Status:** specification, ready to implement
**Scope:** EM500 only. No genset-controller integration in this phase.
**Supersedes** the "Phase 1 endurance" item in the execution brief (since deleted as spent) as the first task.

The controller must know whether the solar plant is currently running **with the grid** or
**with a generator**, because the control policy differs: grid mode regulates to the grid import
target; generator mode must additionally enforce reverse-power protection and generator minimum
loading.

Two site topologies must be supported. Both resolve to the same internal source state.

---

## Mode A — single EM500, digital source input

Some sites use one EM500 and wire a 220 VAC source-detection signal into a digital input.

- **Register:** hex `0x2100` = decimal `8448`, read with **function code 3**
- `0` → **grid** is running with solar
- `1` → **generator** is running with solar

**Verified on site 2026-07-29** by energising and de-energising the 220 VAC source-detection
input and observing the register change on the installed meter (slave 3): `0` with no supply,
`1` with supply present. `0x2101` and `0x2102` stayed `0` throughout.

`0x2100` is the Lovato DMG610 "OR of all digital inputs" register — a documented address, not a
clone-specific guess. The earlier default of `0x2160`, recorded as `CLONE_SPECIFIC` in
`docs/EM500_DMG610_REGISTER_CATALOG.md`, returns **exception 0x02, illegal data address**, on
these units for a single-register read and must not be used.

Note the source input is wired to **slave 3**, while a second meter on the same daisy chain
answers as slave 1. Both report total active power, so either can serve as a power source; only
slave 3 carries the source-detection input.

Because of that, every one of these must be site-configurable, not hardcoded:

```
source_status_register          default 0x2100
source_status_function          3 or 4, physically verified per site
source_status_address_base      0 or 1 (PDU vs display convention)
source_status_grid_value        default 0
source_status_generator_value   default 1
source_status_debounce_ms
source_status_stale_timeout_ms
```

**Any value that is neither the configured grid value nor the configured generator value is
"unknown", not a default to grid.** Unknown must fail closed.

### Known limitation — state it in the UI, do not hide it

In Mode A the source identification rests on a **single digital input**. One meter measuring a
common bus cannot electrically distinguish grid from generator, so there is no independent
corroboration available. A stuck or miswired input is therefore undetectable by the controller.

This is a site-level risk the operator accepts by choosing Mode A. The commissioning UI must show
which mode is active and that Mode A has no redundant confirmation. Do not describe Mode A as
"verified" anywhere in the interface.

---

## Mode B — two meters, power-based detection

Other sites use one meter on the grid and one per generator.

- Grid meter active power **> threshold** → grid is running with solar
- Generator meter active power **> threshold** → solar is running with that generator

The threshold must be configurable per meter and must **not** default to exactly zero. Real
installations show small non-zero readings from leakage and CT error; a zero threshold would
oscillate. Default to `1.0 kW` and allow per-site adjustment.

Rules:
- Both above threshold → **unknown/conflict** → fail closed. Do not silently prefer one.
- Neither above threshold → no source carrying load → fail closed.
- A stale or non-finite reading is **not** "below threshold" — it is unknown.

Mode B is the preferred topology because the two measurements corroborate each other.

---

## Behaviour driven by the resolved source state

| Source state | Control policy | Energy counters |
|---|---|---|
| Grid | Existing grid import target policy | **Tariff 1** |
| Generator | Reverse-power protection **and** generator minimum loading | **Tariff 2** |
| Unknown | **Fail closed** — command safe PV, log the reason | none |

### Tariff register addresses

From `docs/EM500_DMG610_REGISTER_CATALOG.md`, confirmed against the EM500 register manual:

| Address | Meaning |
|---:|---|
| `0x1B48` | Imported active energy, **tariff 1** |
| `0x1B4C` | Exported active energy, tariff 1 |
| `0x1B5C` | Imported active energy, **tariff 2** |
| `0x1B60` | Exported active energy, tariff 2 |

32-bit unsigned, 2 registers each, scale `/100` to kWh. Confirm the address base (PDU vs display)
against the physical meter before trusting any reading — this project has already been bitten by
the PDU/display off-by-one convention.

---

## Mandatory safety behaviour

1. **Debounce every transition.** A flapping input or a reading hovering at the threshold must not
   chatter the PV command. Require the new state to persist for `debounce_ms` before acting.
2. **Staleness is unknown, never a value.** If the source evidence is older than
   `stale_timeout_ms`, the state is unknown and control fails closed.
3. **Reject non-finite values.** Already a project rule — `clampf(NaN, lo, hi)` returns the
   maximum, which for a power command is the worst possible direction.
4. **On any transition into unknown, command safe PV immediately** — do not wait for the next
   control interval.
5. **Report the reason.** The existing boot message pattern
   (`Automatic Solar-Grid control remains fail-closed: ...`) must be extended so an engineer can
   see exactly why the source is unknown. Surface it verbatim in the UI; do not re-word it in the
   browser.

---

## Acceptance criteria

Implementation is complete when all of the following exist:

- Configuration surface for Mode A and Mode B, persisted with schema migration that preserves
  existing commissioned configuration. **A commissioned controller is in the field.**
- Source state exposed on the status API with its evidence and freshness.
- Host C unit tests covering: grid, generator, unknown-value, both-above-threshold conflict,
  neither-above-threshold, stale evidence, non-finite reading, and debounce behaviour on a
  flapping input.
- A source contract test asserting that unknown fails closed and that tariff selection follows the
  source state.
- Simulator scenarios in `tools/soltrix_modbus_simulator.js` for both modes, including the
  input toggling and a threshold-hovering generator reading.

Do **not** enable automatic control on hardware as part of this phase. Detection and reporting
only; the control path stays gated behind the existing commissioning prerequisites.
