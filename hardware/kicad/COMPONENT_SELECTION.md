# Rev-A Major Component Selection

This file controls major-component choices for the KiCad implementation. `FROZEN` means the exact device/function may be used in the prototype schematic. It does not mean the complete circuit block has passed ERC, thermal review, EMC review, DRC or physical prototype validation.

| Block | Part / class | State | Design reason |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N8 | FROZEN | Existing firmware target; 8 MB flash fits the current partition plan; certified module reduces RF/layout risk and production complexity. |
| Ethernet controller | WIZnet W5500, 48-LQFP | FROZEN | Mandatory wired 10/100 Ethernet; SPI host interface and mature manufacturer reference designs. |
| Ethernet MagJack | CETUS J1B1211CCD or pin-compatible WIZnet-approved integrated-magnetics RJ45 | FROZEN FOR PROTOTYPE | Explicitly recommended by WIZnet and used in W5500 reference products. Any substitute requires transformer/center-tap/pinout review. |
| Ethernet crystal | 25 MHz crystal + WIZnet reference network | FROZEN FUNCTION / EXACT CRYSTAL OPEN | Manufacturer reference topology is fixed; exact crystal MPN/load capacitors remain dependent on selected CL/ESR/layout. |
| RS485 A | TI THVD1410D, SOIC-8 | FROZEN FOR REV-A PROTOTYPE | 3.3–5 V half-duplex transceiver, 500 kbps class, extended common-mode/failsafe and strong IEC ESD capability. |
| RS485 B | TI THVD1410D, SOIC-8 | FROZEN FOR REV-A PROTOTYPE | Same qualified circuit as A; independent UART and DE/RE. |
| RS485 field TVS | Semtech SM712.TCT, SOT-23 | FROZEN FOR PROTOTYPE | Purpose-built asymmetric RS485 protection retained in addition to transceiver ESD robustness. |
| Main 12/24 V -> 5 V converter | TI TPS54360B, HSOIC-8 PowerPAD | FROZEN FOR REV-A PROTOTYPE | 60 V input, 3.5 A continuous class; appropriate headroom for the declared 5 V load. |
| Main buck power train | 8.2 µH >=5 A, 2×2.2 µF/100 V input, 2×47 µF/10 V output, EVM-derived frequency/feedback/compensation network | FROZEN STARTING DESIGN | TPS54360EVM-182 reference is the controlled starting point; exact passive MPNs and worst-case thermal/DC-bias proof remain release gates. |
| 3.3 V logic rail | Diodes Inc. AP63203WU-7, fixed 3.3 V / 2 A synchronous buck | FROZEN FOR PROTOTYPE | Manufacturer documentation identifies AP63203 as the fixed 3.3 V member; 5 V-to-3.3 V conversion leaves useful current margin. |
| Relay | Hongfa HF3FF/005-1ZST, 5 V coil, 1 Form C / SPDT | FROZEN FOR PROTOTYPE | Compact Form-C relay; board/contact copper is independently limited to the declared 5 A target pending validation. |
| Relay driver | AO3400A-class N-MOSFET + SS14-class flyback + 100 Ω gate resistor + 10 kΩ gate pulldown | FROZEN FUNCTION / SOURCE-ALTERNATE | 10 kΩ pulldown strengthens OFF-at-reset/brownout behavior without material GPIO load. Exact MOSFET source remains to be frozen against 3.3 V RDS(on). |
| Relay terminal | 3-position 5.08 mm field terminal, NC/COM/NO | FROZEN MECHANICAL FAMILY | Exact vendor MPN/current/voltage approval/body height remains a pre-release mechanical/safety gate. |
| Power/RS485/DI terminals | 5.08 mm field terminal family | FROZEN MECHANICAL FAMILY | Common serviceable field pitch; exact 2/3/5-position vendor parts remain to be frozen. |
| USB-C | GCT USB4105 family, 16-pin USB2 device receptacle with through-hole shell stakes | FROZEN FAMILY / EXACT SUFFIX OPEN | Native ESP32-S3 USB; exact stake-length suffix must match 1.6 mm PCB/enclosure mechanics before manufacturing. |
| USB D+/D- ESD | TI TPD1E05U06DYAR, SOD-523, one per line | FROZEN FOR PROTOTYPE | 0.5 pF-class high-speed protector; physical pin 1 I/O, pin 2 GND; placed adjacent to connector. |
| HMI connector | JST B4B-XH-A / XH 4-position, +5V/GND/TX/RX | FROZEN FOR PROTOTYPE | Keyed low-voltage connector; HMI 5 V remains field-powered only. |
| HMI RX buffer | TI SN74LVC1G17DBVR, SOT-23-5, powered from 3.3 V | FROZEN FOR PROTOTYPE | Accepts input to 5.5 V and provides Ioff partial-power/back-drive protection, replacing the passive divider. |
| HMI RX ESD | TI TPD1E10B06DYAR, SOD-523 | FROZEN FOR PROTOTYPE | Bidirectional 5.5 V low-speed I/O protection at the HMI connector. |
| RS485/service diagnostic buffer | TI SN74LVC14APWR, TSSOP-14 | FROZEN FOR PROTOTYPE | Four gates sense UART TX/RX on the logic side; two gates drive system green/red status. No LED loads A/B. |
| RS485 TX activity LED | Würth 150080AS75000 amber, 0805 | FROZEN FOR PROTOTYPE | TX activity, two channels, driven through LVC14 with 680 Ω current resistor. |
| RS485 RX activity LED | Würth 150080VS75000 green, 0805 | FROZEN FOR PROTOTYPE | RX activity, two channels, driven through LVC14 with 680 Ω current resistor. |
| RUN/status LED | Würth 150080VS75000 green, 0805 | FROZEN FOR PROTOTYPE | GPIO35 via LVC14; green means RUN/healthy. |
| FAULT/WARNING LED | Würth 150080RS75000 red, 0805 | FROZEN FOR PROTOTYPE | GPIO21 via LVC14; red means FAULT; red+green means WARNING. |
| Optional RS232 HMI | MAX3232-class DNP transceiver path | FROZEN OPTIONAL FUNCTION | Same PCB can support RS232 variant without changing base interfaces. |
| Optional DI | LTV-817/PC817-class optocoupler x4 + resistor/reverse diode/pull-up | FROZEN OPTIONAL FUNCTION | Final CTR and 12/24 V threshold/dissipation verification remains required. |
| Optional RTC | PCF8563-class I2C RTC + 32.768 kHz crystal + backup-cell provision | FROZEN OPTIONAL FUNCTION | DNP in base BOM. |
| Optional microSD | SPI microSD socket + ESD/pull-up network | FROZEN OPTIONAL FUNCTION | DNP in base BOM. |

## Manufacturer/reference checkpoints

- Espressif ESP32-S3-WROOM-1 datasheet/hardware guidance controls module pinout, strapping, decoupling and antenna keepout.
- WIZnet W5500 datasheet/reference schematic controls EXRES1, TOCAP, 1V2O, clock, PHY/magnetics and MDI layout.
- TI THVD1410 documentation controls transceiver pinout, supply, failsafe and bus limits.
- Semtech SM712 documentation controls the RS485 TVS electrical limits and footprint.
- TI TPS54360B datasheet and TPS54360EVM-182 control the main 5 V starting network/layout; exact thermal margin remains prototype/release evidence.
- Diodes Inc. AP63203 documentation controls the fixed 3.3 V rail and layout.
- Hongfa HF3FF drawing controls relay pin numbering, body and coil/contact limits.
- TI SN74LVC1G17 documentation controls the HMI partial-power buffer; DBV physical pins are 1 NC, 2 A, 3 GND, 4 Y, 5 VCC.
- TI TPD1E05U06 controls USB ESD placement/pinout; DYA pin 1 is I/O and pin 2 is GND.
- TI TPD1E10B06 controls HMI RX ESD protection.

## Cost-down rule

Prototype Rev-A prioritizes a defensible industrial electrical design. A cheaper substitute cannot be silently introduced: voltage ratings, pinout, timing, ESD, temperature range, lifecycle, footprint, contact/coil data and test evidence must be compared first.

## Remaining exact-part/mechanical gates

1. Freeze USB4105 exact suffix/stake length against final 1.6 mm PCB and enclosure opening.
2. Verify J1B1211CCD exact supplier drawing, magnetics/center taps, LED pins and footprint.
3. Verify HF3FF custom footprint from Hongfa drawing.
4. Freeze exact 2P/3P/5P 5.08 mm terminal MPNs/body heights and safety ratings.
5. Freeze exact TPS54360 input protection/reverse-polarity MOSFET/inductor/capacitor MPNs after thermal/DC-bias review.
6. Freeze exact W5500 25 MHz crystal and load capacitors.
7. Freeze exact AO3400A-source MOSFET with guaranteed RDS(on) at a 3.3 V-compatible gate drive.
