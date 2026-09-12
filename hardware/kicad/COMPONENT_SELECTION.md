# Rev-A Major Component Selection

This file controls major-component choices for the KiCad implementation. `FROZEN` means the exact device/function may be used in the prototype schematic. It does not mean the assembled product has passed thermal, EMC, safety-compliance or field validation.

| Block | Part / class | State | Design reason |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N8 | FROZEN | Existing firmware target; 8 MB flash fits the current partition plan; certified module reduces RF/layout risk and production complexity. |
| Ethernet controller | WIZnet W5500, 48-LQFP | FROZEN | Mandatory wired 10/100 Ethernet; SPI host interface and mature manufacturer reference designs. |
| Ethernet MagJack | CETUS J1B1211CCD | FROZEN FOR PROTOTYPE | Exact 10/100 BASE-TX 1CT:1CT integrated-magnetics RJ45. Supplier drawing and WIZnet reference usage are consistent with the implemented pinout/LED mapping. |
| Ethernet crystal | Abracon ABM8-25.000MHZ-D2Y-T, 25 MHz, 3225 | FROZEN FOR PROTOTYPE | Active 25 MHz 4-pad crystal matching the locked footprint; W5500 reference 18 pF shunt-cap topology is retained. |
| RS485 A | TI THVD1410D, SOIC-8 | FROZEN FOR REV-A PROTOTYPE | 3.3–5 V half-duplex transceiver, extended common-mode/failsafe and strong IEC ESD capability. |
| RS485 B | TI THVD1410D, SOIC-8 | FROZEN FOR REV-A PROTOTYPE | Same qualified circuit as A; independent UART and DE/RE. |
| RS485 field TVS | Semtech SM712.TCT, SOT-23 | FROZEN FOR PROTOTYPE | Purpose-built asymmetric RS485 protection retained in addition to transceiver ESD robustness. |
| Main 12/24 V -> 5 V converter | TI TPS54360B, HSOIC-8 PowerPAD | FROZEN FOR REV-A PROTOTYPE | 60 V input, 3.5 A continuous class; appropriate headroom for the declared 5 V load. |
| Main buck inductor | Würth Elektronik 744393465082, WE-XHMI 6060, 8.2 µH | FROZEN FOR PROTOTYPE | Exact footprint-compatible active part; current rating substantially exceeds the planned converter current. Physical H4 thermal soak still verifies enclosure temperature. |
| Main buck input capacitors | 2 × Murata GRM32ER72A225KA35L, 2.2 µF 100 V X7R, 1210 | FROZEN FOR PROTOTYPE | Exact 1210/100 V source matching the starting design. DC-bias behavior remains part of prototype rail/ripple validation. |
| Main buck output capacitors | 2 × Murata GRM32ER61A476KE20L, 47 µF 10 V X5R, 1210 | FROZEN FOR PROTOTYPE | Exact 1210 output capacitor used in TI power designs; physical ripple/temperature remains an H4 measurement. |
| 3.3 V logic rail | Diodes Inc. AP63203WU-7, fixed 3.3 V / 2 A synchronous buck | FROZEN FOR PROTOTYPE | 5 V-to-3.3 V conversion leaves useful current margin. |
| Reverse-polarity MOSFET | Diodes Inc. DMP6023LSS | FROZEN FOR PROTOTYPE | Exact P-channel input-path device already implemented in SO-8; field input validation still verifies reverse-polarity behavior. |
| Relay | Hongfa HF3FF/005-1ZST, 5 V coil, 1 Form C / SPDT | FROZEN FOR PROTOTYPE | Exact active relay target. The implemented KiCad JQC-3FF Form-C footprint pad geometry matches the HF3FF manufacturer PCB layout. |
| Relay driver | Alpha & Omega AO3400A, SOT-23 + SS14-class flyback + 100 Ω gate resistor + 10 kΩ gate pulldown | FROZEN FOR PROTOTYPE | AO3400A is in full production and has specified RDS(on) at 2.5 V and 4.5 V gate drive; 10 kΩ pulldown strengthens OFF-at-reset/brownout behavior. |
| Relay terminal | Phoenix Contact MKDS 3/3-5.08 BK, order 1712193 | FROZEN FOR PROTOTYPE | Exact relay-contact terminal; stock KiCad footprint locked. Board copper remains independently limited to the 5 A target. |
| Field power terminal | Same Sky TB007-508-02BE | FROZEN FOR PROTOTYPE | Exact 2-position horizontal 5.08 mm part matching the locked TB007 footprint. |
| RS485 terminals | Same Sky TB007-508-03BE | FROZEN FOR PROTOTYPE | Exact 3-position horizontal 5.08 mm parts for A/B/GND, matching the locked TB007 footprint. |
| Optional DI terminal | Same Sky TB007-508-05BE | FROZEN OPTIONAL / DNP BASE | Exact 5-position horizontal 5.08 mm part matching the locked optional footprint. |
| USB-C | GCT USB4105-GF-A-120 | FROZEN FOR PROTOTYPE | Exact USB 2.0 Type-C receptacle from the locked USB4105 family; 1.20 mm stake option selected for Rev-A. Provider DFM still confirms soldering/enclosure fit on the 1.6 mm PCB. |
| USB D+/D- ESD | TI TPD1E05U06DYAR, SOD-523, one per line | FROZEN FOR PROTOTYPE | 0.5 pF-class high-speed protector; physical pin 1 I/O, pin 2 GND; placed adjacent to connector. |
| HMI connector | JST B4B-XH-A / XH 4-position, +5V/GND/TX/RX | FROZEN FOR PROTOTYPE | Keyed low-voltage connector; HMI 5 V remains field-powered only. |
| HMI RX buffer | TI SN74LVC1G17DBVR, SOT-23-5, powered from 3.3 V | FROZEN FOR PROTOTYPE | Accepts input to 5.5 V and provides Ioff partial-power/back-drive protection, replacing the earlier passive divider concept. |
| HMI RX ESD | TI TPD1E10B06DYAR, SOD-523 | FROZEN FOR PROTOTYPE | Bidirectional 5.5 V low-speed I/O protection at the HMI connector. |
| RS485/service diagnostic buffer | TI SN74LVC14APWR, TSSOP-14 | FROZEN FOR PROTOTYPE | Four gates sense UART TX/RX on the logic side; two gates drive system green/red status. No LED loads A/B. |
| RS485 TX activity LED | Würth 150080AS75000 amber, 0805 | FROZEN FOR PROTOTYPE | TX activity, two channels, driven through LVC14 with 680 Ω current resistor. |
| RS485 RX activity LED | Würth 150080VS75000 green, 0805 | FROZEN FOR PROTOTYPE | RX activity, two channels, driven through LVC14 with 680 Ω current resistor. |
| RUN/status LED | Würth 150080VS75000 green, 0805 | FROZEN FOR PROTOTYPE | GPIO35 via LVC14; green means RUN/healthy. |
| FAULT/WARNING LED | Würth 150080RS75000 red, 0805 | FROZEN FOR PROTOTYPE | GPIO21 via LVC14; red means FAULT; red+green means WARNING. |
| Optional RS232 HMI | MAX3232-class DNP transceiver path | FROZEN OPTIONAL FUNCTION | Same PCB can support RS232 variant without changing base interfaces. |
| Optional DI | LTV-817/PC817-class optocoupler x4 + resistor/reverse diode/pull-up | FROZEN OPTIONAL FUNCTION | Final CTR and 12/24 V threshold/dissipation verification remains required if populated. |
| Optional RTC | PCF8563-class I2C RTC + 32.768 kHz crystal + backup-cell provision | FROZEN OPTIONAL FUNCTION | DNP in base BOM. |
| Optional microSD | SPI microSD socket + ESD/pull-up network | FROZEN OPTIONAL FUNCTION | DNP in base BOM. |

## Manufacturer/reference checkpoints

- Espressif ESP32-S3-WROOM-1 datasheet/hardware guidance controls module pinout, strapping, decoupling and antenna keepout.
- WIZnet W5500 datasheet/reference schematic controls EXRES1, TOCAP, 1V2O, clock, PHY/magnetics and MDI layout.
- CETUS J1B1211CCD supplier drawing controls exact magnetics, center taps, LED pins and mechanical outline; the WIZnet reference family uses the same part.
- TI THVD1410 documentation controls transceiver pinout, supply, failsafe and bus limits.
- Semtech SM712 documentation controls the RS485 TVS electrical limits and footprint.
- TI TPS54360B documentation controls the main 5 V network/layout; exact inductor and principal input/output ceramic MPNs are now frozen above, while assembled thermal/ripple margin remains physical evidence.
- Diodes Inc. AP63203 documentation controls the fixed 3.3 V rail and layout.
- Hongfa HF3FF drawing controls relay pin numbering, body and coil/contact limits; its Form-C PCB layout was cross-checked against the implemented JQC-3FF pad geometry.
- Same Sky TB007-508 documentation controls the 2/3/5-position field-terminal mechanical family and ratings.
- GCT USB4105 documentation controls USB-C shell/stake geometry; `USB4105-GF-A-120` is the Rev-A exact prototype MPN.
- TI SN74LVC1G17 documentation controls the HMI partial-power buffer; DBV physical pins are 1 NC, 2 A, 3 GND, 4 Y, 5 VCC.
- TI TPD1E05U06 controls USB ESD placement/pinout; DYA pin 1 is I/O and pin 2 is GND.
- TI TPD1E10B06 controls HMI RX ESD protection.
- Alpha & Omega AO3400A documentation controls relay-driver limits and SOT-23 footprint.

## Cost-down rule

Prototype Rev-A prioritizes a defensible industrial electrical design. A cheaper substitute cannot be silently introduced: voltage ratings, pinout, timing, ESD, temperature range, lifecycle, footprint, contact/coil data and test evidence must be compared first.

## Remaining provider / physical gates

The major populated Rev-A MPN freeze is closed remotely. Remaining gates do not justify changing the schematic without new evidence:

1. PCB/PCBA provider DFM confirms USB-C shell/stake solderability, RJ45/relay body clearances and enclosure openings against the exported STEP model.
2. Physical H4 tests verify TPS54360B rail ripple, DC-bias/thermal margin, reverse-polarity behavior and enclosure temperature at representative 12/24 V loading.
3. Physical H4 tests verify Ethernet, both RS485 ports, HMI and all four relays under simultaneous operation.
4. Optional DNP blocks require exact optional-part qualification only if a full-population build is ordered.
