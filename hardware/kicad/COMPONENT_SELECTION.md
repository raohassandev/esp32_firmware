# Rev-A Major Component Selection

This file controls major-component choices for the KiCad implementation. `FROZEN` means the exact device/function may be used in the schematic. It does not mean the complete circuit block has passed ERC, thermal review or prototype validation.

| Block | Part / class | State | Design reason |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N8 | FROZEN | Existing firmware target; 8 MB flash fits current partition plan; module reduces RF/layout risk and production complexity. |
| Ethernet controller | WIZnet W5500, 48-LQFP | FROZEN | 10/100 PHY + MAC/TCP-IP controller, SPI host interface, mature reference designs; mandatory wired Ethernet. |
| RS485 A | TI THVD1410D, SOIC-8 | PROVISIONAL-FROZEN FOR REV-A PROTOTYPE | 3 V to 5.5 V supply, 500 kbps, industrial temperature range, integrated failsafe and strong IEC ESD/common-mode behavior. SOIC-8 also keeps a serviceable prototype footprint. |
| RS485 B | TI THVD1410D, SOIC-8 | PROVISIONAL-FROZEN FOR REV-A PROTOTYPE | Same circuit and qualification target as RS485 A; electrically independent UART/DE control. |
| Main 12/24 V -> 5 V converter | TI TPS54360B, HSOIC-8 PowerPAD | PROVISIONAL-FROZEN FOR REV-A PROTOTYPE | 4.5-60 V input and 3.5 A continuous class gives suitable headroom for a protected 24 V industrial input and the HMI + relay field rail. Exact surrounding network must be calculated from the datasheet/reference design. |
| 3.3 V logic rail | 5 V -> 3.3 V buck, >=1.0 A continuous target | OPEN | Exact device will be selected after ESP32 + W5500 + optional logic peak-current budget. Avoid a hot linear regulator unless dissipation proves acceptable. |
| RJ45 | Integrated-magnetics 10/100 RJ45 | OPEN | Exact footprint must be frozen with mechanical/casing dimensions; reference network must match W5500 design. |
| Relay | 5 V coil, SPDT/Form-C, PCB relay; >=5 A at 250 VAC resistive target; 10 A preferred if size/cost works | OPEN | Exact relay controls footprint, creepage, terminal pitch, enclosure width and coil current; must be frozen before relay sheet/PCB outline freeze. |
| Relay driver | Four independent low-side MOSFET drivers + flyback protection | PROVISIONAL | Preferred over Darlington array to reduce coil voltage loss and dissipation; exact MOSFET/diode selected with relay coil. |
| RS485 TVS | RS485-specific bidirectional TVS / SM712-class | PROVISIONAL | External terminal protection retained even though the selected transceiver has strong ESD robustness; exact device to be chosen against bus capacitance/surge target. |
| USB-C | USB 2.0 Type-C receptacle, USB-device/service only | OPEN | Exact connector set by enclosure access and assembly preference. CC resistors and ESD are mandatory. |
| HMI connector | Locking/pluggable +5V/GND/TX/RX | OPEN | Exact connector set by display cable and enclosure; electrical interface remains dedicated UART. |
| Optional DI | 4x optically isolated 12/24 V inputs | OPEN | Optocoupler/input-current network to be selected after 12/24 V threshold/noise target is calculated. |
| Optional RTC | PCF8563-class low-cost RTC | OPEN | Final device/backup cell arrangement to be selected after board-space review. |
| Optional microSD | SPI microSD connector | OPEN | Final connector chosen after enclosure accessibility decision. |

## Datasheet/reference checkpoints already established

- ESP32-S3-WROOM-1: use current Espressif module datasheet and hardware design guidance for module pins, strapping and antenna keep-out.
- W5500: use current WIZnet W5500 datasheet and WIZnet reference schematic/hardware resources for PHY/magnetics/clock/decoupling.
- THVD1410: use TI THVD14xx datasheet; base device is non-isolated and does not replace board-level field protection decisions.
- TPS54360B: use TI datasheet/EVM/reference design. The power block is not considered complete until the 5 V load budget, switching frequency, inductor, compensation, capacitors, UVLO and thermal/layout calculations are recorded.

## Cost-down rule

Prototype Rev-A prioritizes a defensible industrial electrical design. After bench validation, major devices may be cost-down reviewed. A cheaper substitute cannot be silently dropped into production: voltage ratings, pinout, timing, ESD, temperature range, lifecycle, footprint and test evidence must be compared first.

## Immediate next freezes

1. Compute worst-case 5 V and 3.3 V load budgets.
2. Freeze exact relay and relay terminal family because they drive board width and casing.
3. Freeze RJ45/magnetics footprint.
4. Freeze USB-C and field terminal families.
5. Draw and review the power + MCU sheets before Ethernet/RS485 placement work begins.
