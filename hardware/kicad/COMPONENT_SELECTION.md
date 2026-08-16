# Rev-A Major Component Selection

This file controls major-component choices for the KiCad implementation. `FROZEN` means the exact device/function may be used in the prototype schematic. It does not mean the complete circuit block has passed ERC, thermal review, EMC review, DRC or physical prototype validation.

| Block | Part / class | State | Design reason |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N8 | FROZEN | Existing firmware target; 8 MB flash fits the current partition plan; certified module reduces RF/layout risk and production complexity. |
| Ethernet controller | WIZnet W5500, 48-LQFP | FROZEN | Mandatory wired 10/100 Ethernet; SPI host interface and mature manufacturer reference designs. |
| Ethernet MagJack | CETUS J1B1211CCD or pin-compatible WIZnet-approved integrated-magnetics RJ45 | FROZEN FOR PROTOTYPE | Explicitly recommended by WIZnet and used in W5500 reference products. Any substitute requires transformer/center-tap/pinout review. |
| Ethernet crystal | 25 MHz crystal + WIZnet reference network (1 MΩ, 0 Ω provision, 18 pF load caps starting values) | FROZEN FOR PROTOTYPE | Matches W5500 manufacturer reference topology; final load caps remain crystal-CL/layout dependent. |
| RS485 A | TI THVD1410D, SOIC-8 | FROZEN FOR REV-A PROTOTYPE | 3.3–5 V half-duplex transceiver, 500 kbps class, extended common-mode/failsafe and strong IEC ESD capability. |
| RS485 B | TI THVD1410D, SOIC-8 | FROZEN FOR REV-A PROTOTYPE | Same qualified circuit as A; independent UART and DE/RE. |
| RS485 field TVS | SM712-class low-capacitance RS485 TVS | FROZEN FUNCTION / SOURCE-ALTERNATE | Board-level terminal protection retained in addition to transceiver ESD robustness. |
| Main 12/24 V -> 5 V converter | TI TPS54360B, HSOIC-8 PowerPAD | FROZEN FOR REV-A PROTOTYPE | 60 V input, 3.5 A continuous class; suitable headroom for 24 V industrial input and HMI + four relays + downstream logic. |
| Main buck power train | 8.2 µH >=5 A inductor, 2×2.2 µF/100 V input ceramics, 2×47 µF/10 V output ceramics, 0.1 µF BOOT; EVM-derived starting network | FROZEN STARTING DESIGN | Based on TI TPS54360EVM-182 5 V/3.5 A reference. Compensation/UVLO/thermal values are separately controlled and must pass calculation/review. |
| 3.3 V logic rail | Diodes Inc. AP63203WU-7, fixed 3.3 V / 2 A synchronous buck | FROZEN FOR PROTOTYPE | 5 V-to-3.3 V conversion with ample MCU/Ethernet margin, better efficiency than linear regulation and low BOM count. |
| Relay | Hongfa HF3FF/005-1ZST, 5 V coil, 1 Form C / SPDT | FROZEN FOR PROTOTYPE | 10 A-class NO contact, Form-C dry contact, compact THT relay. PCB/contact traces are not automatically rated to the relay maximum; board derating is controlled separately. |
| Relay driver | AO3400A-class N-MOSFET per relay + SS14-class flyback diode + gate pulldown | FROZEN FUNCTION / SOURCE-ALTERNATE | Independent low-side drivers minimize coil voltage loss; pulldown guarantees OFF-at-reset intent. |
| Relay terminal | 3-position 5.08 mm pitch field terminal, NC/COM/NO per relay | FROZEN MECHANICAL FAMILY | Keeps channel wiring obvious and permits wide mains/contact spacing. Exact vendor can change only within the locked footprint/rating family. |
| Power/RS485/DI terminals | 5.08 mm field terminal family | FROZEN MECHANICAL FAMILY | Common serviceable field pitch; exact 2/3/5-position variants selected per block. |
| USB-C | 16-pin USB 2.0 Type-C receptacle, device-only, through-hole shell preferred | FROZEN FUNCTION / FOOTPRINT TO VERIFY ON MAC | Native ESP32-S3 USB; no USB-UART bridge. CC1/CC2 5.1 kΩ Rd and USB ESD are mandatory. |
| HMI connector | JST B4B-XH-A / XH 4-position, +5V/GND/TX/RX | FROZEN FOR PROTOTYPE | Locking keyed low-voltage connector, sufficient for 5 V auxiliary current and UART service. |
| HMI RX protection | 5 V-tolerant input divider/protection: 10 kΩ upper + 20 kΩ lower starting values, series protection provision | FROZEN FUNCTION | Prevents raw 5 V HMI TX from reaching ESP32 GPIO while accepting common 5 V TTL UART displays. |
| Optional RS232 HMI | MAX3232-class DNP transceiver path | FROZEN OPTIONAL FUNCTION | Same PCB can support RS232 HMI variant without changing base BOM. |
| Optional DI | LTV-817/PC817-class optocoupler x4, 3.3 kΩ/0.25 W input resistor starting value, reverse diode, 10 kΩ logic pull-up | FROZEN OPTIONAL FUNCTION | Gives usable current at both 12 V and 24 V while isolating field inputs from MCU logic. Exact CTR grade to be verified before procurement. |
| Optional RTC | PCF8563-class I2C RTC + 32.768 kHz crystal + backup-cell ORing provision | FROZEN OPTIONAL FUNCTION | Low-cost field clock; DNP in Lite BOM. |
| Optional microSD | SPI microSD socket + ESD/pull-up network | FROZEN OPTIONAL FUNCTION | Logging expansion; DNP in Lite BOM. |

## Manufacturer/reference checkpoints

- ESP32-S3-WROOM-1: current Espressif module datasheet and hardware-design guidance control module pins, strapping, decoupling and antenna keep-out.
- W5500: current WIZnet datasheet/reference schematic controls pin use, EXRES1=12.4 kΩ 1%, TOCAP=4.7 µF, 1V2O=10 nF, 25 MHz clock topology, PHY/magnetics and MDI layout.
- THVD1410: TI THVD14xx datasheet controls transceiver pinout, supply, failsafe and bus limits; base Rev-A remains non-isolated.
- TPS54360B: TI datasheet and TPS54360EVM-182 are the starting reference for the 5 V/3.5 A rail. The EVM uses 600 kHz, 8.2 µH, 2×2.2 µF/100 V input ceramics, 2×47 µF/10 V output ceramics and the documented compensation/divider network.
- AP63203: Diodes Inc. datasheet/reference design controls the 3.3 V buck power train and layout.
- HF3FF/005-1ZST: Hongfa datasheet controls relay coil/contact ratings and mechanical pattern.

## Cost-down rule

Prototype Rev-A prioritizes a defensible industrial electrical design. After bench validation, major devices may be cost-down reviewed. A cheaper substitute cannot be silently dropped into production: voltage ratings, pinout, timing, ESD, temperature range, lifecycle, footprint, contact/coil data and test evidence must be compared first.

## Remaining footprint/mechanical checks before H1 freeze

1. Verify exact USB-C footprint against the chosen stocked connector on the Mac KiCad library / supplier CAD.
2. Verify J1B1211CCD mechanical footprint and LED pinout against supplier CAD before board placement.
3. Verify HF3FF footprint from Hongfa drawing / KiCad custom footprint before PCB routing.
4. Freeze the exact 5.08 mm terminal body height/orientation after DIN-rail enclosure fit study.
5. Complete power calculations and schematic/ERC before H1 can pass.
