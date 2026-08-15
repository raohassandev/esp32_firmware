# PV-DG Control Science

**The plant owner's own rules for how this controller must behave.**

## Why this file exists

The product owner explained the whole control model in conversation over several
days: the grid policies, the generator protections, the setpoint arithmetic, the
per-brand register scales, the source-detection topologies. It was implemented,
and it went into code comments spread across a dozen files.

It was never written down in one place. That meant the owner had no way to check
the implementation against their own intent without reading the firmware, and no
way to tell an implemented rule from a remembered one. This file is that place.

**Status of this document: RECONSTRUCTED, AWAITING THE OWNER'S CONFIRMATION.**
It is written from the conversation and cross-checked against the code that
exists today. Every rule carries the file that implements it and the test that
executes it, so each line can be verified independently. Where the code and the
stated rule disagree, that is recorded rather than smoothed over.

Correct anything wrong here **before** it is used to drive work. A specification
reconstructed from memory and then trusted is worse than no specification.

---

## 1. Grid side

### 1.1 Policies

| Policy | Meaning |
|---|---|
| ZERO EXPORT | No power may flow back to the utility |
| LIMITED EXPORT | Export permitted up to a commissioned cap |
| LIMITED IMPORT | A minimum import from the utility is held |

- Implemented: `components/control_engine/control_engine.c`
- Test: `tests/solar_grid_control_source_contract.py`
- Configurable: yes, `web/solar-grid.js`
- In the commissioning flow: **NO**

### 1.2 Control basis

The limit is enforced on **one of two measurements**, chosen at commissioning:

- **Lowest phase — the default.** The most negative signed phase, which under an
  import-positive convention is simultaneously the phase closest to exporting and
  the phase exporting hardest.
- **Total kW.**

Why it matters: a three-phase load that is badly out of balance can satisfy a
limit on the total while one phase is already exporting.

- Implemented: `components/control_engine/phase_selection.c`
- Test: `tests/phase_selection_test.c`
- In the commissioning flow: yes (`phase_basis`)

**Fallback rule:** if any phase reading is missing, the controller falls back to
the TOTAL. It never picks the worst of the two that answered — that would be a
guess presented as a measurement.

### 1.3 Grid loss and recovery

- Grid loss trip time and a recovery-stable time before control resumes.
- Implemented: `components/control_engine/grid_control_gate.c`
- Test: `tests/grid_control_gate_test.c`
- Configurable: yes, `web/solar-grid.js`
- In the commissioning flow: **NO**

---

## 2. Generator side

### 2.1 The load identity

```
Total kW = Grid + Generator + Solar
```

This is the site's own definition of plant load. Solar is the **measured**
output of the inverters, not the commanded setpoint.

- Implemented: `components/control_engine/control_engine.c:708`
- Only inverters reporting a VALID measurement contribute. A stale one
  contributes zero, which understates the load and therefore understates the PV
  cap — the error lands on the side of a **more** loaded generator, which is the
  safe direction.

### 2.2 Minimum loading

```
Required kW = Generator minimum loading - Total kW
```

A diesel generator that runs too lightly loaded wet-stacks and is damaged. When
the plant load falls, PV must be curtailed to keep the engine loaded.

- **Default: 30% of rating** — TO BE CONFIRMED. The firmware does not carry 30
  as a default; `solar_grid_config.c:657` initialises it to `0.0`, which means
  "not commissioned" and keeps control fail-closed. Whether 30% should be
  *proposed* at commissioning is an open decision.
- Implemented: `components/control_engine/generator_fleet_limit.c`
- Test: `tests/generator_safe_pv_convergence_test.c`
- In the commissioning flow: **NO**

### 2.3 Reverse-power protection

- **Margin = 5% of generator rating.**
- Power must never flow back into a generator.
- Implemented: `components/control_engine/control_engine.c`
- Default in firmware: `0.0` (not commissioned, fail-closed).
- In the commissioning flow: **NO**

### 2.4 Reserve / headroom

Spare generator capacity kept for load steps, so a sudden load does not stall
the engine before PV can be curtailed.

- Implemented: `components/solar_grid_config/solar_grid_config.c:228`
- In the commissioning flow: **NO**

### 2.5 Urgent ramp

> "Jab generator par load 25% se kam ho to ramp rate 2x ho jaye."

```
generator load < 25% of online rating  ->  PV ramp-DOWN runs at 2x
```

**Ramp-DOWN only.** The urgency is to raise generator loading, and th is done
by reducing PV faster. Ramping PV *up* faster would do the opposite.at

**DIRECTION CONFIRMED BY THE OWNER, 2026-08-01.** An earlier example doubled the
UP rate ("uprate 2 becomes 4"); asked directly, the owner confirmed the intent is
the DOWN rate. The reasoning holds: a generator below 25% loading is
under-loaded, and loading it means taking PV OFF faster. Raising PV faster would
unload it further.

- Implemented: `components/control_engine/generator_fleet_limit.c`
- Test: `tests/generator_urgent_ramp_test.c`,
  `tests/generator_ramp_direction_source_contract.py` (which pins down-only)
- **Commissioned since schema 9.** Threshold and multiplier are stored,
  validated and editable in the Solar-Grid ramp editor, which states what the
  configured rate becomes. The firmware constants remain as the defaults every
  earlier schema migrates to, so no commissioned plant changed behaviour.
- Zero in either field disables the boost. A multiplier below 1 is refused: it
  would shed PV more slowly on an under-loaded engine.

### 2.6 Parallel generators

- Per-engine slots, roles, base load, and an aggregate floor.
- Two live sources is a FAULT unless the plant was commissioned as
  synchronisation-capable.
- Implemented: `components/control_engine/generator_fleet_limit.c`
- Test: `tests/generator_fleet_limit_test.c`
- In the commissioning flow: **NO**

---

## 3. Inverter side

### 3.1 The setpoint

```
Setpoint % = clamp( headroom / Solar Capacity x 100 , 0 , 100 )
```

- **Solar Capacity = the sum of CONNECTED inverters**, not of every inverter
  ever commissioned. An inverter that is offline cannot be curtailed, so counting
  it inflates the capacity and understates the percentage the rest must take.
- The clamp is not cosmetic: **above 100% is a problem, below 100 is fine.**

### 3.2 The register value

```
raw = setpoint % x per-brand scale
```

The inverter always takes an **unsigned** value.

| Profile | Scale |
|---|---|
| Huawei (SUN2000, SmartLogger) | x10 |
| GoodWe | x10 |
| Sungrow | x10 |
| Chint / CPS | x10 |
| FoxESS | x10 |
| Solis | x100 |
| Knox / AISWEI | x100 |
| Growatt (TL3-X, TLX) | x1 |
| SolarEdge TerraMax | x1 |

- Implemented: `components/inverter_manager/inverter_profiles.c`
  (`raw_units_per_percent`)
- **Why this matters:** 45% on a x10 register is the word 450. Writing 45 commands
  4.5% — the inverter would be told to stop, and the readback would echo 45,
  decode with the same wrong scale, agree with the request, and report the
  command CONFIRMED. Nothing downstream can catch that; only the word can.

### 3.3 Communication fail-safe ordering

Two independent safeties, and the ORDER between them is what makes it safe:

| Safety | Owner | Default |
|---|---|---|
| Inverter's own comms fail-safe | the inverter | **1 minute** |
| Controller's offline debounce | this controller | **2 minutes** |

The controller must wait LONGER than the inverter. If the link drops, the
inverter reaches its own fail-safe first and protects itself; the controller does
not have to guess.

- **NOT IMPLEMENTED.** No `offline_debounce` field exists in the configuration,
  and the ordering is not enforced or asserted anywhere. Open item.

### 3.4 Rewrite cadence

> "Isko chahe 30 minute ke baad 1 dafa write karwa dein, aur 1 dafa
> communication restore hone ke baad."

- Write once on communication restore, and periodically thereafter (~30 min).
- **NOT IMPLEMENTED as a periodic refresh.** Commands are issued on change.
  Open item.

---

## 4. Source detection

### 4.1 One meter — tariff based

EM500 register **`0x2100`** is the digital-input word.

- `0` = grid + solar
- non-zero = generator + solar

The generator's 220 V AC feeds the tariff input. The register is documented as
an **OR of all digital inputs** — a bitmask, not an enumeration — so any
energised input means generator, whichever bit carries it.

- Implemented: `components/source_detection/source_detection_engine.c:33`
- Test: `tests/em500_source_detection_source_contract.py`,
  `tests/source_detection_engine_test.c`
- Configurable: yes, `web/source-detection.js`
- In the commissioning flow: **NO**

**Why the bitmask rule:** with the generator value commissioned as 1, a second
input wired on the same meter makes the register read 2 or 3 while the genset
carries the plant. Exact equality matched neither configured value, detection
reported UNKNOWN, control stayed fail-closed, no curtailment was issued, and PV
kept exporting into a generator that was never protected.

### 4.2 Two meters — threshold based

Whichever meter reads above its commissioned threshold is the source carrying
the load.

- Both above threshold = **FAULT**, unless the plant was commissioned as
  synchronisation-capable, in which case it is SYNCHRONISED.
- Implemented: `components/source_detection/source_detection_engine.c:103`
- Test: same as above.
- In the commissioning flow: **NO**

---

## 5. Recorded for phase 2

### 5.1 Ramp rate must scale with engine size

> "Ramp rate engine size ke sath scale hona chahiye — 50 kVA set par plant
> destabilise hota hai aur inverters de-synchronise ho jaate hain."

A ramp rate that is safe on a 500 kVA set is violent on a 50 kVA one. Not
implemented; the ramp is per-source, not per-engine-size.

### 5.2 Troubleshooting values for small factories

> "Pakistan mein choti factories ke log deep cheezon ko nahi samajhte... koi aisa
> variable, koi value jo fault tracing aasan kar sake, ya user ko controller ke
> theek hone ka proof de sake."

Partly addressed: the controller now reports uptime, a memory verdict and
whether the last restart was unexpected. The control-decision evidence
(grid min/max/average, error kW, safe PV, gate state) is published by the API and
reaches no screen.

---

## 6. Where the gap is

Every rule above except two is implemented and tested. The gap is not the
control science — it is that **the commissioning flow walks past almost all of
it**. Of the parameters in this document, only the control basis and the
inverter profile are set during commissioning. The rest are scattered across
separate pages an engineer has to know to find.

| Rule | Implemented | Tested | Has a UI | In commissioning |
|---|---|---|---|---|
| Grid policies | yes | yes | yes | no |
| Control basis (lowest phase) | yes | yes | yes | **yes** |
| Grid loss / recovery | yes | yes | yes | no |
| Total kW identity | yes | yes | n/a | n/a |
| Minimum loading | yes | yes | yes | no |
| Reverse-power margin | yes | yes | yes | no |
| Reserve / headroom | yes | yes | yes | no |
| Urgent ramp 2x | yes | yes | **no — hardcoded** | no |
| Parallel generators | yes | yes | yes | no |
| Setpoint clamp | yes | yes | yes | no |
| Per-brand scale | yes | yes | yes | **yes** |
| Comms fail-safe ordering | **no** | no | no | no |
| Periodic setpoint refresh | **no** | no | no | no |
| Source: 1 meter tariff | yes | yes | yes | no |
| Source: 2 meters | yes | yes | yes | no |

---

## Open questions for the owner

1. **Minimum loading default 30%** — should commissioning *propose* 30, or keep
   requiring an explicit value? Today it defaults to 0, which is fail-closed.
2. ~~**Urgent ramp 25% / 2x**~~ — ANSWERED 2026-08-01: commissioned, with a
   control in the interface, applied to the DOWN rate. Done in schema 9.
3. **Offline debounce 2 min** — confirm, and confirm that the controller must
   always be longer than the inverter's own fail-safe.
4. **Periodic setpoint refresh ~30 min** — confirm the interval.
5. Anything in this document that is wrong or missing.
