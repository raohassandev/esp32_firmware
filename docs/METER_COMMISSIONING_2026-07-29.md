# Grid meter commissioning record — 2026-07-29

Measured against the physical meter at `192.168.100.200:502`, unit 1, using an
independent raw Modbus TCP client (not the controller), cross-checked with Mbpoll.

## The controller was reading a voltage as power

The stored configuration had `active_power_address = 2` with `scale = 0.001`, and the
controller reported `grid_power_kw: 24.73`.

Address 2 is **not** active power. Measured:

| PDU address | Raw (S32) | ÷100 | What it is |
|---:|---:|---:|---|
| 1 | 24761 | 247.61 | phase voltage |
| **2** | 24762 | **247.62** | **phase voltage** |
| 3 | 24762 | 247.62 | phase voltage |
| 20 (`0x0014`) | 9,658,000 | 96,580 W | L1 active power |
| **57** | **29,106,000** | **291,060 W** | **total active power** |

So `24.73 kW` was really **247.3 volts** being fed into the power control loop. Three phases
at ~96.6 kW each ≈ 290 kW total, which corroborates address 57 independently.

Address 57 is the firmware's own default and was correct all along. The catalogue lists total
active power at `0x003A` (58 in base-1 display convention), which is PDU 57 — the usual
display-vs-PDU off-by-one this project has been bitten by before.

### Corrected and verified

```
active_power_address = 57
active_power_type    = INT32      word_order = ABCD
active_power_scale   = 0.00001    -> kW directly from raw/100 W
function             = 3          (FC04 also answers; FC03 is in use and proven)
```

`0.00001` is set explicitly rather than relying on the legacy fingerprint in
`effective_active_power_scale()`, which only rewrites `0.01` at address 57/58. No double-scaling.

Cross-checked against a direct read taken at the same moment:

```
direct 272.72 kW   controller 271.22 kW
direct 270.88 kW   controller 268.22 kW
direct 272.26 kW   controller 269.04 kW
```

Agreement is within live load fluctuation between the two reads. **The long-standing question
of whether the reported grid power was trustworthy is now closed for this meter.**

## RESOLVED: the source input is `0x2100` on slave 3

The site confirmed the correct register by energising the 220 VAC source-detection input while
watching it. Reproduced independently from the controller network:

| Slave | `0x2100` | `0x2101` | `0x2102` | 220 VAC input |
|---:|---:|---:|---:|---|
| 3 | **1** | 0 | 0 | applied |
| 3 | **0** | 0 | 0 | not applied |
| 1 | 0 | 0 | 0 | (no input wired) |

So `0` = grid, `1` = generator, exactly the semantics the specification assumed — only the
register and the slave were wrong. `0x2100` is the documented Lovato "OR of all digital inputs"
register, read with function code 3, so this is no longer a clone-specific guess.

The default in `source_detection_config.h` has been changed from `0x2160` to `0x2100`.

Both meters answer on the same daisy chain behind `192.168.100.200:502`:

| Slave | Total active power (PDU 57) | Source input |
|---:|---:|---|
| 1 | 291.30 kW | not wired |
| 3 | 288.70 kW | **wired, verified** |

**Which meter measures which circuit is still unknown to me.** Both read roughly the same power,
so I cannot infer from the readings whether one is the grid feed and the other a generator feed.
That has to be stated by someone who can see the installation before meter roles are assigned or
Mode B is used.

## Superseded: `0x2160` does not exist on these meters

`docs/PHASE1_SOURCE_DETECTION_SPEC.md` defaults the single-input source register to decimal
8544 (`0x2160`), recorded as clone-specific and established by physical observation. On this
unit it is **not readable**:

| Register | FC03 | FC04 |
|---|---|---|
| 8544 (`0x2160`), 1 register | **EXCEPTION 0x02** illegal data address | **EXCEPTION 0x02** |
| `0x2100` OR of all digital inputs | `0x0000` | `0x0000` |
| `0x2101` input 1 status | `0x0000` | `0x0000` |

A two-register read at `0x2160` did return data, but a single-register read — the natural way to
read a status word — is rejected. That inconsistency is itself a reason not to trust it.

The standard Lovato digital-input registers do respond. Both currently read zero, which per the
spec means "grid", but that cannot be distinguished from "input not wired" without toggling the
physical source-detection signal.

**Outstanding commissioning step, requires site access:** energise and de-energise the 220 VAC
source-detection input and record which register changes and to what value. Until then Mode A
must not be enabled on this meter. The spec's decision to make the register, function code and
address base configurable per site is what makes this recoverable — the default is simply wrong
for this unit.

Mode B (separate grid and generator meters) is unaffected and remains the preferred topology.

## Commissioned scene: Mode A on slave 3

Per site direction, only slave 3 is used and the grid/generator distinction comes from `0x2100`.

Meter:

```
EM500 slave 3   unit_id 3   function 3   active_power_address 57
data_type INT32  word_order ABCD  scale 0.00001 kW/raw
poll 1000 ms     timeout 1500 ms   role = grid
```

The role is `grid` because that is the input the control engine consumes. In Mode A this single
meter measures the point of common coupling — whichever source is currently carrying the load —
and `0x2100` states which source that is. Grid and generator are therefore distinguished by the
digital input, not by which meter is read.

Source detection:

```
mode              single_input
register          8448 (0x2100)   function 3   address_base 0
grid_value        0     generator_value 1
debounce_ms       3000            stale_timeout_ms 5000
```

**The two timing values are commissioning choices, not measurements.** They were set to three and
five polls of the 1000 ms meter interval. They are recorded here so they can be challenged rather
than mistaken for verified site data.

Live result with 220 VAC applied to the tariff input:

```
state              generator
candidate_state    generator
tariff             2
reason             resolved; automatic control remains disabled
evidence_fresh     true      transition_pending false      conflict false
single_input       raw_value 1, age 0 ms
grid_power_kw      323.44
control_enabled    false
```

Tariff 2 is selected for generator, matching the specification. Read reliability in steady state
is 100%: the 13 failures counted are all from startup before the network was ready, and the
counter stopped advancing once running (61, 69, 77 successes against a static 13).

### Still to verify on site

The **transition** has not been exercised. De-energising and re-energising the input should show
the state pass through `unknown` with `transition_pending` true for the 3000 ms debounce before
settling on the new source. Until that is observed, only the steady states are proven.

Mode A remains single-evidence by design: a stuck or miswired input cannot be detected by the
controller, and the API reports that limitation verbatim.

## Other confirmed facts

- Both FC03 and FC04 answer at the power registers; address 0 returns exception 0x02 on both.
- The meter had been off the network entirely earlier in the day — no ICMP, no ARP entry, all
  ports closed — which is why the controller reported it offline. That was a field condition,
  not a firmware fault, and the controller reported it correctly and held control fail-closed.
