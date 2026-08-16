# Rev-A Electrical Design Freeze Working Specification

**Branch:** `hardware/kicad-reva-implementation`  
**Status:** working design; H1 not passed until native KiCad ERC + review are clean.  
**Purpose:** component-level electrical definition used to draw the Rev-A schematic and later route the PCB.

## 1. Power architecture

### 1.1 Field input

Nominal source: 12/24 VDC industrial panel supply. Design target: approximately 9–30 VDC normal operating range.

Required chain:

`VIN+ terminal -> resettable/replaceable input protection -> reverse-polarity MOSFET -> 33 V-class TVS -> input LC/bulk filtering -> TPS54360B -> 5V_FIELD`

`VIN- terminal -> GND_FIELD / logic ground` in base non-isolated Rev-A.

Rules:
- 60 V-rated silicon in the main buck path.
- No raw field VIN may reach ESP32, W5500, SD, RTC or HMI UART logic.
- Test points: protected VIN, 5V_FIELD, 3V3, GND.
- Layout must minimize the TPS54360B hot loop and provide thermal vias under PowerPAD.

### 1.2 5 V rail

Main converter: TPS54360B, 5.0 V output.

Starting reference values derived from the TI 5 V / 3.5 A EVM:
- switching frequency target: 600 kHz;
- L: 8.2 µH, >=5 A saturation/current rating;
- CIN: 2 x 2.2 µF, 100 V X7R close to VIN/GND plus board-level bulk provision;
- COUT: 2 x 47 µF, 10 V X5R/X7R plus 0.1 µF ceramic;
- BOOT capacitor: 0.1 µF;
- feedback divider starting target around the TI EVM 5 V values;
- external compensation network to follow TI EVM/datasheet, not guessed during PCB layout.

The 5 V field rail powers:
- four relay coils;
- touch-HMI auxiliary 5 V output;
- the field-side source feeding the logic ORing path.

### 1.3 USB / field power separation

`5V_FIELD` and USB VBUS must be diode/ideal-diode ORed only into a `5V_LOGIC_IN` node.

- USB-only power: ESP32 + W5500 + logic may boot for programming/service.
- USB-only power must NOT energize relay coils.
- USB-only power must NOT energize HMI 5 V auxiliary output.
- Field power may power all sections.
- USB VBUS and field 5 V must never back-feed each other.

### 1.4 3.3 V rail

Converter: AP63203WU-7 fixed 3.3 V, 2 A class.

`5V_LOGIC_IN -> AP63203 -> 3V3`

Loads: ESP32-S3, W5500, both THVD1410 transceivers, LEDs, optional RTC, optional SD and optocoupler output pull-ups.

Design target: >=1.2 A credible simultaneous logic allowance with >=25% headroom; device class is 2 A.

## 2. Power budget

### 2.1 5 V loads

| Load | Design allowance |
|---|---:|
| HMI auxiliary output | 1.00 A |
| 4 x HF3FF 5 V relay coils | ~0.29 A nominal total |
| 3.3 V converter input equivalent | ~0.90 A worst-case planning allowance |
| LEDs / margin / losses | ~0.30 A |
| **Planned 5 V simultaneous demand** | **~2.49 A** |

TPS54360B 3.5 A class therefore provides useful prototype headroom. HMI connector must be fused/current-limited or protected so an external HMI short cannot collapse the whole controller without protection.

### 2.2 3.3 V loads

Planning allowances (not claimed measured consumption):
- ESP32-S3 radio/CPU transient allowance: 0.70 A;
- W5500: 0.20 A design allowance;
- 2 x RS485 + LEDs/RTC/SD/logic: 0.30 A;
- total planning rail: 1.20 A.

AP63203 2 A class leaves margin for transient current and future firmware use.

## 3. ESP32-S3 core

Part: ESP32-S3-WROOM-1-N8.

Mandatory support:
- EN pull-up and RC reset network per Espressif guidance;
- BOOT button on GPIO0 with safe pull-up behavior;
- native USB D-/D+ on GPIO19/GPIO20;
- USB ESD close to receptacle;
- 5.1 kΩ Rd on USB-C CC1/CC2;
- local 3.3 V decoupling/bulk close to module supply;
- module antenna over board edge where practical with manufacturer keep-out honored on all copper layers and enclosure metal avoided.

No use of module-reserved flash pins. GPIO allocation is controlled by `IMPLEMENTATION_STATUS.md`.

## 4. Ethernet

Controller: W5500 48-LQFP at 3.3 V.

Mandatory datasheet connections:
- EXRES1 pin -> 12.4 kΩ 1% -> AGND;
- TOCAP -> 4.7 µF -> AGND, short trace;
- 1V2O -> 10 nF -> AGND;
- VBG left floating;
- RSVD pin 23 tied to GND as specified;
- 25 MHz crystal network using manufacturer topology;
- SPI: SCLK GPIO12, MOSI GPIO11, MISO GPIO13, CS GPIO10;
- INTn GPIO9;
- RSTn GPIO8, active-low, boot-safe pull-up;
- PHY mode default: all-capable auto-negotiation;
- manufacturer-approved integrated-transformer RJ45, prototype target J1B1211CCD;
- Ethernet ESD at connector side;
- RJ45 LEDs used for link/activity where pinout supports it.

MDI pair routing is differential, short, length-balanced within practical 10/100 limits, referenced to uninterrupted ground, and kept away from relay contacts and buck switch node.

## 5. RS485 ports A and B

Each port is electrically independent.

Part: THVD1410D, 3.3 V.

Per-port topology:
- ESP UART TX -> DI;
- RO -> ESP UART RX;
- /RE and DE tied together to one ESP control GPIO;
- 10 kΩ pulldown on DE//RE node so reset state = driver disabled + receiver enabled;
- 0.1 µF decoupling at VCC;
- A/B -> SM712-class TVS -> 3-pin A/B/GND field terminal;
- selectable 120 Ω termination across A/B;
- optional DNP bus bias: A pull-up to 3V3 and B pull-down to GND, value selected during bench bus testing;
- A/B/GND test points.

GPIOs:
- RS485-A TX/RX/DE: 43/44/42;
- RS485-B TX/RX/DE: 17/18/16.

## 6. Serial touch HMI

Dedicated UART, not shared with RS485.

Connector: 4-pin keyed locking low-voltage header:
1. +5V_HMI (from 5V_FIELD only, protected)
2. GND
3. Controller TX -> HMI RX
4. HMI TX -> Controller RX

GPIOs: HMI TX=15, HMI RX=14.

Logic protection:
- ESP TX 3.3 V passes through small series resistor to HMI RX;
- HMI TX enters a 5 V-to-3.3 V divider/protection network before ESP RX;
- initial divider: 10 kΩ series/high side + 20 kΩ low side;
- ESD protection if connector cable exits the enclosure;
- optional DNP MAX3232 path for an RS232 display variant.

## 7. Relay outputs

Four identical mandatory Form-C channels.

Relay: Hongfa HF3FF/005-1ZST, 5 V coil, SPDT.

Per channel:
- +5V_FIELD -> relay coil -> AO3400A-class low-side N-MOSFET drain;
- MOSFET source -> GND;
- ESP GPIO -> 100 Ω gate resistor -> gate;
- 100 kΩ gate pulldown -> GND;
- SS14-class flyback diode across coil, cathode at +5V_FIELD;
- indicator LED + resistor controlled from the switched coil node;
- external contact terminal: NC / COM / NO.

GPIOs: relay 1..4 = GPIO4, GPIO5, GPIO6, GPIO7.

Safety/layout rules:
- all relays OFF during boot/reset/fault unless firmware later intentionally energizes them;
- contact copper kept in a dedicated field/contact zone;
- no contact trace passes under/through MCU, USB, Ethernet or RS485 logic;
- minimum design target: >=6 mm copper clearance from possible mains contact nets to SELV logic, with isolation slots used where geometry benefits;
- base board traces/terminals are designed around a 5 A continuous resistive contact path target; relay 10 A marking alone must never be interpreted as a 10 A certified PCB-system rating;
- any 230 VAC use remains a product safety/compliance responsibility and requires final spacing/enclosure review.

## 8. Optional isolated digital inputs

Four channels, DNP-capable in Lite BOM.

Per channel starting topology:
- field input terminal DIx referenced to DI_COM;
- 3.3 kΩ, >=0.25 W series resistor;
- optocoupler LED (LTV-817/PC817-class);
- reverse diode across optocoupler LED;
- transistor collector -> ESP GPIO with 10 kΩ pull-up to 3V3;
- emitter -> logic GND;
- optional 100 nF debounce capacitor at logic node.

GPIOs: DI1..DI4 = GPIO1, GPIO2, GPIO47, GPIO48.

Final threshold/CTR must be verified on 12 V and 24 V bench supplies before production.

## 9. Optional RTC

PCF8563-class I2C RTC, 32.768 kHz crystal, backup cell through diode-OR/power-selection network, DNP-capable.

GPIO38 SDA, GPIO39 SCL. Pull-ups to 3V3. Battery accessible with enclosure opened.

## 10. Optional microSD

SPI microSD, DNP-capable.

GPIO40 SCLK, GPIO41 MOSI, GPIO37 MISO, GPIO36 CS. Local 0.1 µF + bulk decoupling, ESD if user-accessible, pull-ups per card/SPI requirements.

## 11. Manufacturing/service test points

At minimum:
- protected VIN;
- 5V_FIELD;
- 5V_LOGIC_IN;
- 3V3;
- GND;
- ESP EN and GPIO0;
- USB D+/D- small pads;
- W5500 CS/INT/RST;
- RS485 A: TX/RX/DE/A/B;
- RS485 B: TX/RX/DE/A/B;
- relay gate/coil switched node for all four channels.

## 12. PCB stack-up and zones

Preferred Rev-A prototype: 4 layers, 1.6 mm FR-4, 1 oz outer copper unless relay/contact current analysis requests 2 oz.

Proposed stack:
- L1: components + signals + local power;
- L2: near-continuous GND plane;
- L3: power distribution / low-speed signals;
- L4: low-speed signals / ground fill.

Zones:
1. high-voltage/dry-contact relay contact edge;
2. relay coils + field power;
3. logic/ESP32;
4. Ethernet/MagJack edge;
5. RS485/HMI edge;
6. optional DI/RTC/SD.

## 13. Preliminary mechanical envelope

PCB working envelope: 145 mm x 95 mm maximum target; may shrink after placement.

Connector placement intent:
- relay NC/COM/NO terminal groups along one long edge;
- power + RS485 + HMI + optional DI along the opposite/adjacent service edge;
- RJ45 on enclosure edge with direct cable access;
- USB-C accessible without removing PCB;
- ESP32 antenna at a board/enclosure edge away from relay and metal DIN clip.

DIN-rail enclosure target remains approximately <=160 x 110 x 50 mm until final 3D placement is frozen.

## 14. H1 acceptance

H1 schematic freeze may pass only when:
- all mandatory component-level circuits are present in native KiCad;
- optional DNP circuits are present and clearly marked;
- final GPIO and connector pinout audit passes;
- power-budget document and controlled component list are committed;
- Mac KiCad parses project and schematic;
- ERC has zero unwaived violations;
- generated netlist/BOM/PDF artifacts are available;
- each ERC exclusion, if any, is individually justified in the repo.
