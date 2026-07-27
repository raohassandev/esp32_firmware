# PV-DG grid / generator control specification

Status: design specification only. Live inverter control remains disabled until all simulator and physical qualification gates pass.

## 1. Scope and assumptions

- On-grid solar inverters synchronize themselves to the active AC source.
- The ESP32 controller does not synchronize the inverter waveform.
- The controller reads grid and generator meters, determines the source currently carrying the plant, then limits inverter output.
- Phase 1 assumes only one source is on load at a time.
- Grid and generator source meters are independent of any DSE/ComAp synchronizing controller.
- No new breaker digital input is required for Phase 1; source detection uses the EM500 clone digital input plus measured voltage, frequency, current and active power.
- Automatic control is fail-safe: unknown, stale, contradictory or out-of-range source evidence forces PV toward zero.

## 2. Sign convention

Use one sign convention everywhere:

```text
Grid meter:
  positive kW = import from utility into plant
  negative kW = export from plant to utility

Generator meter:
  positive kW = generator supplying plant
  negative kW = reverse power into generator
```

A meter profile must include a configurable sign multiplier so site CT polarity can be corrected without changing control equations.

## 3. Inputs

### Required fast inputs

- `source_status_raw` from clone register `0x2160` / decimal 8544, physically verified as 0/1.
- Grid total active power and freshness.
- Generator total active power and freshness.
- Grid and generator phase/equivalent voltage.
- Grid and generator frequency.
- Inverter command-channel availability.
- Applied inverter power-limit feedback when available.

### Required slow inputs

- Per-phase voltage, current, active/reactive/apparent power and PF.
- THD and asymmetry.
- Imported/exported energy.
- Generator rated kW and per-generator online state.
- Meter CT/PT/wiring setup.

## 4. Source state machine

States:

```text
STARTUP
GRID_CANDIDATE
GRID_ACTIVE
GENERATOR_CANDIDATE
GENERATOR_WARMUP
GENERATOR_ACTIVE
TRANSFER_TO_GRID
TRANSFER_TO_GENERATOR
NONE
CONFLICT
FAULT
```

### Digital input interpretation

Default site mapping:

```text
source_status_raw = 0 -> grid requested
source_status_raw = 1 -> generator requested
```

The polarity is configurable and must never be hard-coded globally.

### Electrical confirmation

A requested source becomes active only when all configured checks pass for a stable confirmation interval:

- meter communication fresh;
- voltage within configured range;
- frequency within configured range;
- at least one of source current or absolute active power exceeds its minimum detection threshold;
- the other source is not simultaneously confirmed on load.

Recommended default confirmation logic:

```text
source_voltage_ok = V_min <= V_equivalent <= V_max
source_frequency_ok = F_min <= Hz <= F_max
source_loaded = abs(kW) >= source_power_min_kw OR I_equivalent >= source_current_min_a
source_confirmed = fresh AND source_voltage_ok AND source_frequency_ok AND source_loaded
```

### Conflict rules

Enter `CONFLICT` when:

- digital input requests grid but generator electrical evidence is active;
- digital input requests generator but grid electrical evidence is active;
- both grid and generator are electrically on load;
- neither source can be confidently identified during a transfer timeout;
- the input changes repeatedly faster than the debounce interval.

`CONFLICT`, `NONE` and `FAULT` command PV to zero and raise a latched alarm.

## 5. Transfer behavior

Every source change follows this sequence:

1. Detect a debounced source-input change.
2. Immediately freeze the PI integrator.
3. Ramp PV down using the emergency downward ramp.
4. Require the old source to become unloaded or invalid.
5. Require the new source to become electrically stable.
6. Reset/seed the controller integrator for bumpless transfer.
7. Apply the source-specific warmup delay.
8. Ramp PV upward using the normal upward ramp.

No source-to-source transfer may retain the old control target.

## 6. Unified source-power controller

For both grid and generator modes, use source meter power as the controlled process value.

```text
error_kw = measured_source_kw - target_source_kw
```

Interpretation:

- positive error: source is carrying more power than desired, so PV may increase;
- negative error: source is carrying less power than desired, so PV must decrease.

Use a bounded incremental PI controller with anti-windup:

```text
integrator += Ki * error_kw * dt
integrator = clamp(integrator, integrator_min, integrator_max)
raw_pv_target_kw = pv_reference_kw + Kp * error_kw + integrator
```

Then apply:

- mode-specific upper/lower bounds;
- inverter available capacity;
- normal and emergency ramp limits;
- minimum command step/deadband;
- command freshness/readback checks.

The final command is distributed only among initialized and commandable inverter channels.

## 7. Grid mode

### Grid policy variables

```text
grid_zero_export_enabled
grid_import_bias_kw
grid_export_limit_kw
grid_import_limit_kw
grid_max_pv_kw
grid_deadband_kw
```

### ZERO_EXPORT

Purpose: prevent export while maximizing solar self-consumption.

Target:

```text
target_grid_kw = +grid_import_bias_kw
```

The small positive import bias prevents oscillation around exactly zero.

Behavior:

- grid import above target -> increase PV;
- grid export / grid power below target -> reduce PV;
- export beyond emergency threshold -> immediate fast PV reduction;
- sustained export beyond trip threshold -> command PV zero and alarm.

### LIMITED_EXPORT

Purpose: permit export up to an approved limit.

Target:

```text
target_grid_kw = -grid_export_limit_kw
```

PV is increased while import remains above the target and reduced when export exceeds the limit.

### LIMITED_IMPORT

Purpose: keep utility import below a contract or transformer limit when PV capacity is available.

Constraint:

```text
grid_kw <= grid_import_limit_kw
```

Control priority:

1. Never exceed the export lower bound.
2. Maximize PV inside the allowed grid-power window.
3. Increase PV aggressively when import exceeds the import limit.
4. If PV is already at available maximum and import still exceeds the limit, raise `GRID_IMPORT_LIMIT_UNACHIEVABLE`; optional non-critical load shedding is a separate site feature.

### Grid operating window

Represent grid policy as an allowed interval:

```text
minimum_grid_kw = +bias            # zero export
minimum_grid_kw = -export_limit    # limited export
maximum_grid_kw = import_limit     # optional
```

The controller maximizes PV while keeping measured grid power inside this interval.

## 8. Generator mode

### Generator policy variables

```text
generator_rated_kw
generator_min_load_percent
generator_min_load_fixed_kw
generator_min_load_mode = percent | fixed | maximum_of_both
generator_reverse_warning_kw
generator_reverse_trip_kw
generator_reverse_delay_ms
generator_overload_warning_percent
generator_overload_trip_percent
generator_warmup_seconds
generator_cooldown_seconds
generator_deadband_kw
```

### Minimum loading

Compute the required generator contribution:

```text
percent_min_kw = generator_rated_kw * generator_min_load_percent / 100
fixed_min_kw = generator_min_load_fixed_kw

target_generator_kw =
  percent mode: percent_min_kw
  fixed mode: fixed_min_kw
  maximum_of_both: max(percent_min_kw, fixed_min_kw)
```

Default commissioning value may be approximately 30% of rated generator power, but the final value is site/generator specific.

Control behavior:

- generator kW above target -> increase PV gradually;
- generator kW below target -> decrease PV faster;
- generator near reverse-power region -> decrease PV immediately;
- no valid generator meter -> PV zero.

Equivalent feed-forward limit:

```text
allowed_pv_kw = max(0, estimated_site_load_kw - target_generator_kw)
```

The closed-loop generator-meter target remains authoritative; feed-forward only improves response.

### Reverse-power protection

Use two independent levels:

1. Software curtailment layer:
   - warning threshold;
   - fast downward ramp;
   - PI bypass when necessary.
2. Hard protection layer:
   - dedicated generator protection relay/controller remains the final trip protection;
   - ESP32 software is not the sole reverse-power protective device.

Suggested sequence:

```text
if generator_kw <= reverse_warning_kw:
    command rapid PV reduction

if generator_kw <= reverse_trip_kw continuously for reverse_delay_ms:
    command PV = 0
    latch reverse-power alarm
    optionally open an approved PV-enable contactor/output
```

A reverse event must require manual or timed supervised recovery; the controller must not immediately ramp PV back up.

### Generator overload support

When generator load rises above the warning threshold:

- increase PV up to available capacity;
- temporarily relax the minimum-loading target only in the safe direction;
- if generator remains overloaded at maximum PV, request non-critical load shedding;
- never use a dump load as the normal overload solution;
- never hide an overload condition by falsifying meter data.

### Multiple generators

For multiple online generators:

```text
online_rated_kw = sum(rated kW of confirmed online generators)
minimum_required_kw = sum(per-generator minimum kW) or configured aggregate minimum
```

Aggregate meter control is permitted only when per-generator reverse protection remains available from each genset controller/relay.

## 9. Ramps and dynamic response

Use asymmetric limits:

- PV increase: conservative, typically 2–5 % of available capacity per second during normal operation.
- PV decrease: substantially faster, typically 20–100 %/s depending on inverter interface and generator transient capability.
- emergency reverse/export reduction: fastest safe command path.

All values are configurable. Do not hard-code site tuning constants.

Add:

- measurement low-pass filter with bounded lag;
- command deadband;
- minimum command interval;
- anti-windup;
- derivative-free controller to avoid amplifying meter noise;
- optional load-step detector for temporary faster response.

## 10. Fail-safe matrix

| Condition | Required action |
|---|---|
| Active source meter stale | PV -> 0 |
| Source digital input stale | Use electrical evidence only for a short grace period, then PV -> 0 |
| Digital input and meters disagree | CONFLICT, PV -> 0 |
| Both sources on load | CONFLICT, PV -> 0 |
| Voltage/frequency invalid | PV -> 0 |
| Inverter command write failure | Remove channel from available capacity; repeated failure -> PV system fault |
| Command readback mismatch | Stop increasing; repeated mismatch -> command 0 / trip path |
| Reverse power | Emergency curtailment then latched fault |
| Export above hard limit | Emergency curtailment then latched fault |
| Controller reboot | Control remains disabled until all startup checks and operator enable conditions pass |
| Configuration changed | Force control disabled and require recommissioning |

## 11. Configuration model additions

```text
source_detection:
  register
  function_code
  address_base
  data_type
  word_order
  grid_value
  generator_value
  debounce_ms
  stale_timeout_ms
  conflict_timeout_ms

source_validation:
  voltage_min
  voltage_max
  frequency_min
  frequency_max
  power_min_kw
  current_min_a
  stable_ms

grid_policy:
  mode
  import_bias_kw
  export_limit_kw
  import_limit_kw
  emergency_export_kw
  deadband_kw

generator_policy:
  rated_kw
  minimum_load_mode
  minimum_load_percent
  minimum_load_fixed_kw
  reverse_warning_kw
  reverse_trip_kw
  reverse_delay_ms
  overload_warning_percent
  overload_trip_percent
  warmup_seconds

controller_tuning:
  kp
  ki
  interval_ms
  normal_ramp_up_pct_s
  normal_ramp_down_pct_s
  emergency_ramp_down_pct_s
  command_deadband_pct
```

## 12. Operator UI

The control page must show:

- raw source input and debounced value;
- detected source and evidence used;
- grid and generator meter freshness;
- current operating policy;
- source target kW, measured kW, control error and final PV command;
- minimum generator loading and reverse margin;
- import/export margin;
- active constraints and alarms;
- explicit `CONTROL DISABLED`, `SIMULATION`, `MANUAL` or `AUTOMATIC` banner.

Operator-editable values are policy targets and tuning limits. Meter reset/default/reboot commands are not shown here.

## 13. Qualification plan

### Simulator

- Stable grid zero-export.
- Limited export.
- Limited import with sufficient and insufficient PV.
- Generator minimum loading.
- Reverse-power step.
- Generator overload step.
- Grid-to-generator and generator-to-grid transfer.
- Source-input bounce.
- Digital input/meter disagreement.
- Meter timeout and stale data.
- Inverter command timeout and readback mismatch.
- Multiple generators online/offline.

### Physical bench

- Confirm source register `0x2160` for at least 100 ON/OFF transitions.
- Confirm input debounce and no false source changes.
- Confirm grid/gen meter sign using known import/export/load direction.
- Confirm PV command decreases generator reverse tendency.
- Confirm emergency downward response timing.
- Confirm zero-export bias under load steps.
- Confirm configuration remains intact after reboot.
- Confirm control is disabled after any failed validation.

### Release gate

Live automatic control remains disabled until:

- all source states and transitions pass;
- meter register maps are physically verified;
- inverter write/readback is qualified;
- reverse/export emergency response passes;
- a hardware protection layer is confirmed for generator reverse power;
- final site-specific settings are approved and backed up.