# EM500 / Lovato DMG610-compatible register catalogue

Status: engineering specification. Measurement reads are permitted. Setup writes remain disabled until the exact clone behavior is physically qualified.

## 1. Device profile

The Rozwel EM-500 measurement map matches the Lovato DMG6 family closely, with the DMG610 being the nearest feature match because it is a three-phase + neutral meter with CT inputs and an integrated RS485 port.

The clone is not assumed to be byte-for-byte identical for every setup/status register. Every register is classified as one of:

- `EM500_VERIFIED`: present in the supplied EM-500 document and/or physically read.
- `LOVATO_COMPATIBLE`: present in the official DMG6 protocol and expected to work, but must be probed read-only.
- `CLONE_SPECIFIC`: physically observed on this meter but absent from the official DMG610 map.
- `UNVERIFIED_WRITE`: never write until snapshot, readback and rollback tests pass.

## 2. Addressing convention

The supplied EM-500 document and the live Modbus Poll test use direct PDU addresses. For example, PDU address `2` reads the EM-500 table entry `0x0002`.

The official Lovato manual describes table addresses that are decremented by one on the Modbus wire. Therefore the firmware must keep an explicit per-profile setting:

```text
address_base = 0  # EM500 direct-PDU behavior confirmed on the current meter
address_base = 1  # Lovato table-address convention when physically verified
wire_address = configured_address - address_base
```

No hidden or automatic one-based conversion is allowed.

## 3. Instantaneous electrical measurements

All values are high-word-first unless physical testing proves otherwise.

| PDU address | Words | Measurement | Decode | Engineering value |
|---:|---:|---|---|---|
| `0x0002` | 2 | L1-N voltage | U32 | raw / 100 V |
| `0x0004` | 2 | L2-N voltage | U32 | raw / 100 V |
| `0x0006` | 2 | L3-N voltage | U32 | raw / 100 V |
| `0x0008` | 2 | L1 current | U32 | raw / 10000 A |
| `0x000A` | 2 | L2 current | U32 | raw / 10000 A |
| `0x000C` | 2 | L3 current | U32 | raw / 10000 A |
| `0x000E` | 2 | L1-L2 voltage | U32 | raw / 100 V |
| `0x0010` | 2 | L2-L3 voltage | U32 | raw / 100 V |
| `0x0012` | 2 | L3-L1 voltage | U32 | raw / 100 V |
| `0x0014` | 2 | L1 active power | S32 | raw / 100 W |
| `0x0016` | 2 | L2 active power | S32 | raw / 100 W |
| `0x0018` | 2 | L3 active power | S32 | raw / 100 W |
| `0x001A` | 2 | L1 reactive power | S32 | raw / 100 var |
| `0x001C` | 2 | L2 reactive power | S32 | raw / 100 var |
| `0x001E` | 2 | L3 reactive power | S32 | raw / 100 var |
| `0x0020` | 2 | L1 apparent power | U32 | raw / 100 VA |
| `0x0022` | 2 | L2 apparent power | U32 | raw / 100 VA |
| `0x0024` | 2 | L3 apparent power | U32 | raw / 100 VA |
| `0x0026` | 2 | L1 power factor | S32 | raw / 10000 |
| `0x0028` | 2 | L2 power factor | S32 | raw / 10000 |
| `0x002A` | 2 | L3 power factor | S32 | raw / 10000 |
| `0x0032` | 2 | Frequency | U32 | raw / 1000 Hz |
| `0x0034` | 2 | Equivalent phase voltage | U32 | raw / 100 V |
| `0x0036` | 2 | Equivalent line voltage | U32 | raw / 100 V |
| `0x0038` | 2 | Equivalent / total current | U32 | raw / 10000 A |
| `0x003A` | 2 | Total active power | S32 | raw / 100 W |
| `0x003C` | 2 | Total reactive power | S32 | raw / 100 var |
| `0x003E` | 2 | Total apparent power | U32 | raw / 100 VA |
| `0x0040` | 2 | Total power factor | S32 | raw / 10000 |
| `0x0042` | 2 | Line-voltage asymmetry | U32 | raw / 100 % |
| `0x0044` | 2 | Phase-voltage asymmetry | U32 | raw / 100 % |
| `0x0046` | 2 | Current asymmetry | U32 | raw / 100 % |
| `0x0048` | 2 | Neutral current | U32 | raw / 10000 A |

## 4. Power quality

| PDU address | Words | Measurement | Decode |
|---:|---:|---|---|
| `0x0054` | 2 | L1 voltage THD | U32 / 100 % |
| `0x0056` | 2 | L2 voltage THD | U32 / 100 % |
| `0x0058` | 2 | L3 voltage THD | U32 / 100 % |
| `0x005A` | 2 | L1 current THD | U32 / 100 % |
| `0x005C` | 2 | L2 current THD | U32 / 100 % |
| `0x005E` | 2 | L3 current THD | U32 / 100 % |
| `0x0060` | 2 | L1-L2 voltage THD | U32 / 100 % |
| `0x0062` | 2 | L2-L3 voltage THD | U32 / 100 % |
| `0x0064` | 2 | L3-L1 voltage THD | U32 / 100 % |

Individual harmonic blocks are optional diagnostics and must not be polled in the fast control loop.

## 5. Historical measurement blocks

The instantaneous layout is repeated with these base offsets:

| Block | Base |
|---|---:|
| Instantaneous | `0x0000` |
| Maximum / HI | `0x0400` |
| Minimum / LO | `0x0600` |
| Average | `0x0800` |
| Maximum demand | `0x0A00` |

A profile may request only supported fields. Unsupported clone addresses must return `null`, never numeric zero.

## 6. Energy registers

All energy values are four 16-bit words, decoded as unsigned 64-bit and divided by 100.

### Total and partial

| Address | Measurement |
|---:|---|
| `0x1B20` | Total imported active energy, kWh |
| `0x1B24` | Total exported active energy, kWh |
| `0x1B28` | Total imported reactive energy, kvarh |
| `0x1B2C` | Total exported reactive energy, kvarh |
| `0x1B30` | Total apparent energy, kVAh |
| `0x1B34` | Partial imported active energy, kWh |
| `0x1B38` | Partial exported active energy, kWh |
| `0x1B3C` | Partial imported reactive energy, kvarh |
| `0x1B40` | Partial exported reactive energy, kvarh |
| `0x1B44` | Partial apparent energy, kVAh |

### Tariff totals

| Address | Measurement |
|---:|---|
| `0x1B48` | Imported active energy tariff 1 |
| `0x1B4C` | Exported active energy tariff 1 |
| `0x1B50` | Imported reactive energy tariff 1 |
| `0x1B54` | Exported reactive energy tariff 1 |
| `0x1B58` | Apparent energy tariff 1 |
| `0x1B5C` | Imported active energy tariff 2 |
| `0x1B60` | Exported active energy tariff 2 |
| `0x1B64` | Imported reactive energy tariff 2 |
| `0x1B68` | Exported reactive energy tariff 2 |
| `0x1B6C` | Apparent energy tariff 2 |

### Per-phase blocks

- L1 starts at `0x1E20`.
- L2 starts at `0x1E48`.
- L3 starts at `0x1E70`.

Each block contains imported/exported active, imported/exported reactive, apparent and partial counters.

## 7. Counters and state

| Address | Meaning | Classification |
|---:|---|---|
| `0x1E00` | Total hour counter | EM500_VERIFIED |
| `0x1E02` onward | Partial hour counters | EM500_VERIFIED |
| `0x2100` | OR of all digital inputs | LOVATO_COMPATIBLE |
| `0x2101`...`0x2108` | Input 1...8 status | LOVATO_COMPATIBLE |
| `0x2110` | OR of outputs | LOVATO_COMPATIBLE |
| `0x2111`...`0x2118` | Output 1...8 status | LOVATO_COMPATIBLE |
| `0x2120` | OR of alarms | LOVATO_COMPATIBLE |
| `0x2121`...`0x2128` | Alarm 1...8 status | LOVATO_COMPATIBLE |
| `0x2130` | OR of boolean logic channels | LOVATO_COMPATIBLE |
| `0x2140` | OR of all limits | EM500_VERIFIED |
| `0x2141` onward | Individual limit states | EM500_VERIFIED / probe exact count |
| `0x4F00` onward | Remote boolean states | EM500_VERIFIED / probe exact count |

### Clone-specific source input at decimal 8544

Decimal `8544` is hexadecimal `0x2160`. On the installed meter, the user has physically observed:

```text
0 = no 220 VAC on the source-detection terminals
1 = 220 VAC present on the source-detection terminals
```

`0x2160` is not listed as an input-status register in the official DMG610 map. It is therefore classified as `CLONE_SPECIFIC` and named:

```text
clone_source_input_raw
```

The firmware must make these configurable per site:

```text
source_status_register = 0x2160
source_status_function = 3 or 4, physically verified
source_status_address_base = 0 or 1
source_status_generator_value = 1
source_status_grid_value = 0
source_status_debounce_ms
source_status_stale_timeout_ms
```

It must never be the only evidence used to command PV. Grid and generator voltage/power evidence must agree with the input state.

## 8. Setup parameters

### M01 — General metering setup

| Code | Address | Setting | Range / representation |
|---|---:|---|---|
| P01.01 | `0x5000` | CT primary | 1...10000 A |
| P01.02 | `0x5002` | CT secondary | clone enum; normally 1 A or 5 A |
| P01.03 | `0x5004` | Rated voltage | 2 words |
| P01.04 | `0x5006` | Use VT/PT | 0/1 |
| P01.05 | `0x5008` | VT/PT primary | 2 words |
| P01.06 | `0x500A` | VT/PT secondary | 50...500 V |
| P01.07 | `0x500C` | Wiring system | enum 0...5 |

### Other setup menus

| Menu | Base | Purpose |
|---|---:|---|
| M02 | `0x5080` | Language, display and default page |
| M03 | `0x5100` | Password enable and levels |
| M04 | `0x5180` | Integration mode/times for power, current, voltage and frequency |
| M05 | `0x5200` | Total/partial hour counters |
| M06 | `0x5280` | Trend graph |
| M07 | `0x5300 + (n-1)*0x80` | Communication address, baud, format, stop bits, protocol, IP and port |
| M08 | `0x5400 + (n-1)*0x80` | Limit source, upper/lower thresholds, delays, normal state and latch |
| M09 | `0x5800 + (n-1)*0x80` | Alarm source, channel, latch, priority and text |
| M10 | `0x5C00 + (n-1)*0x80` | Counters, scaling, description and units |
| M11 | `0x5E00 + (n-1)*0x80` | Energy pulse source, unit and duration |
| M12 | `0x6080 + (n-1)*0x80` | Boolean logic operands/operators |
| M13 | `0x6480 + (n-1)*0x80` | Digital input function, normal state, ON delay and OFF delay |
| M14 | `0x6880 + (n-1)*0x80` | Output function, channel and idle state |
| M15 | `0x6C80 + (n-1)*0x80` | User pages and displayed measurements |
| M16 | `0x6E80 + (n-1)*0x40` | Analog input type, scaling, description and units |
| M17 | `0x7080 + (n-1)*0x40` | Analog output type, source and scaling |
| M18 | `0x6B40` onward | Power-quality enable and voltage/frequency/THD/asymmetry/dip/swell thresholds |

### Tariff control

The supplied EM-500 document lists `0x4200` as the tariff-selection command with values 1 or 2. This is `UNVERIFIED_WRITE`.

Tariff options in the web application must be:

- Fixed tariff 1.
- Fixed tariff 2.
- External input controlled.
- Read-only / meter-managed.

The application must not issue `0x4200` until a service user confirms the exact meter model and a readback test passes.

## 9. Setup-write transaction

A normal operator may read setup values but may not write them.

A service/admin write must use this sequence:

1. Confirm control is disabled and inverter commands are inhibited.
2. Read and store the complete current parameter snapshot.
3. Validate model, firmware identity, unit ID, function code and address base.
4. Validate each requested value against a model-specific range.
5. Show an exact before/after change set.
6. Require a second explicit confirmation.
7. Write only the requested setup register(s).
8. Read back every changed register.
9. Save to meter flash only after all readbacks match.
10. Reconnect after the expected meter reboot.
11. Re-read the complete snapshot and compare.
12. Restore the snapshot when any verification step fails.

The following commands are never ordinary settings:

- Reset energy.
- Reset maximum/minimum values.
- Reset demand.
- Restore defaults.
- Backup/restore meter parameters.
- Wiring test.
- Reboot.

They require a separate maintenance workflow and audit log.

## 10. Fast/slow polling split

Fast control loop:

- Source digital input.
- Grid/generator total active power.
- Voltage and frequency validity.
- Meter freshness and communication status.

Slow telemetry loop:

- Phase voltage/current/power/PF.
- Reactive/apparent power.
- THD/asymmetry.
- Energy counters.
- Maximum/minimum/average/demand.
- Setup values.

Energy and setup registers must never delay the fast control loop.