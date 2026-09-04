# Rev-A Electrical Design Freeze Specification

**Branch:** `hardware/kicad-reva-implementation`  
**Status:** H1 SCHEMATIC FREEZE PASS; H2 routed-PCB release validation in progress.  
**Purpose:** component-level electrical definition used to generate, audit and manufacture the Rev-A prototype.

## 1. Power architecture

### 1.1 Field input

Nominal source: 12/24 VDC industrial panel supply. Design target: approximately 9–30 VDC normal operating range.

Implemented chain:

`VIN+ terminal -> input protection -> DMP6023LSS reverse-polarity MOSFET -> 33 V-class TVS -> input filtering -> TPS54360B -> 5V_FIELD`

`VIN- terminal -> GND_FIELD / logic ground` in base non-isolated Rev-A.

Rules:
- 60 V-rated silicon in the main buck path.
- No raw field VIN may reach ESP32, W5500, SD, RTC or HMI UART logic.
- Test points: protected VIN, 5V_FIELD, 3V3, GND.
- Layout minimizes the TPS54360B hot loop and provides PowerPAD thermal vias.

### 1.2 5 V rail

Main converter: TPS54360B, 5.0 V output.

Frozen prototype power train:
- switching frequency target: 600 kHz;
- L1: Würth Elektronik 744393465082, 8.2 µH WE-XHMI 6060;
- CIN1/CIN2: Murata GRM32ER72A225KA35L, 2.2 µF 100 V X7R 1210;
- COUT1/COUT2: Murata GRM32ER61A476KE20L, 47 µF 10 V X5R 1210;
- BOOT capacitor: 0.1 µF;
- feedback/compensation network follows the controlled TI TPS54360B starting design rather than being tuned during PCB layout.

The 5 V field rail powers:
- four relay coils;
- touch-HMI auxiliary 5 V output;
- the field-side source feeding the logic ORing path.

Physical H4 validation remains responsible for measured ripple, regulator/inductor temperature and effective capacitor performance under representative 12/24 V load.

### 1.3 USB / field power separation

`5V_FIELD` and USB VBUS are ORed only into the `5V_LOGIC_IN` node.

- USB-only power: ESP32 + W5500 + logic may boot for programming/service.
- USB-only power must NOT energize relay coils.
- USB-only power must NOT energize HMI 5 V auxiliary output.
- Field power may power all sections.
- USB VBUS and field 5 V must never back-feed each other.

### 1.4 3.3 V rail

Converter: AP63203WU-7 fixed 3.3 V, 2 A class.

`5V_LOGIC_IN -> AP63203 -> 3V3`

Loads: ESP32-S3, W5500, both THVD1410 transceivers, diagnostic/status logic, LEDs, optional RTC, optional SD and optocoupler output pull-ups.

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

TPS54360B 3.5 A class therefore provides useful prototype headroom. The HMI supply is field-side only and protected so USB service power cannot energize it.

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
- module antenna at the logic-edge region with manufacturer keep-out honored on all copper layers and enclosure metal kept away.

No use of module-reserved flash pins. GPIO allocation is controlled by `IMPLEMENTATION_STATUS.md` and the generated physical-pin manifest.

## 4. Ethernet

Controller: W5500 48-LQFP at 3.3 V.

Mandatory connections:
- EXRES1 -> 12.4 kΩ 1% -> AGND;
- TOCAP -> 4.7 µF -> AGND, short trace;
- 1V2O -> 10 nF -> AGND;
- VBG left floating;
- RSVD pin 23 tied to GND as specified;
- crystal: Abracon ABM8-25.000MHZ-D2Y-T, 25 MHz, 3225 4-pad;
- WIZnet reference 18 pF shunt-cap oscillator topology retained;
- SPI: SCLK GPIO12, MOSI GPIO11, MISO GPIO13, CS GPIO10;
- INTn GPIO9;
- RSTn GPIO8, active-low, boot-safe pull-up;
- PHY mode default: all-capable auto-negotiation;
- integrated-transformer RJ45: CETUS J1B1211CCD;
- Ethernet ESD at connector side;
- RJ45 LEDs used for link/activity according to the exact J1B1211CCD pinout.

The CETUS supplier drawing and WIZnet reference usage are the mechanical/electrical authority for the MagJack. MDI routing is controlled by the repository signal-integrity gate: short, layer-controlled, limited-via paths referenced to uninterrupted ground and isolated from relay contacts/buck switch copper.

## 5. RS485 ports A and B

Each port is electrically independent.

Part: THVD1410D, 3.3 V.

Per-port topology:
- ESP UART TX -> DI;
- RO -> ESP UART RX;
- /RE and DE tied together to one ESP control GPIO;
- 10 kΩ pulldown on DE//RE node so reset state = driver disabled + receiver enabled;
- 0.1 µF decoupling at VCC;
- A/B -> SM712.TCT TVS -> exact Same Sky TB007-508-03BE A/B/GND terminal;
- selectable 120 Ω termination across A/B;
- optional DNP bus bias: A pull-up to 3V3 and B pull-down to GND, value selected during bench bus testing;
- A/B/GND test points.

GPIOs:
- RS485-A TX/RX/DE: 43/44/42;
- RS485-B TX/RX/DE: 17/18/16.

## 6. Serial touch HMI

Dedicated UART, not shared with RS485.

Connector: JST B4B-XH-A:
1. +5V_HMI (from 5V_FIELD only)
2. GND
3. Controller TX -> HMI RX
4. HMI TX -> Controller RX input network

GPIOs: HMI TX=15, HMI RX=14.

Implemented logic protection:
- ESP TX 3.3 V is the controller-to-HMI logic output;
- HMI TX enters `HMI_RX_IN` through connector-side protection;
- TI TPD1E10B06DYAR protects the connector-side RX signal;
- TI SN74LVC1G17DBVR, powered from 3.3 V, buffers HMI RX into the ESP32 and provides Ioff partial-power/back-drive protection;
- the earlier passive 10 kΩ/20 kΩ divider concept is superseded and is not part of Rev-A;
- optional DNP MAX3232 path remains for an RS232 display variant.

## 7. Relay outputs

Four identical mandatory Form-C channels.

Relay: Hongfa HF3FF/005-1ZST, 5 V coil, SPDT. The implemented stock KiCad `Relay_SPDT_Hongfa_JQC-3FF_0XX-1Z` pad geometry was cross-checked against the manufacturer Form-C PCB layout.

Per channel:
- +5V_FIELD -> relay coil -> Alpha & Omega AO3400A low-side N-MOSFET drain;
- MOSFET source -> GND;
- ESP GPIO -> 100 Ω gate resistor -> gate;
- 10 kΩ gate pulldown -> GND;
- SS14-class flyback diode across coil, cathode at +5V_FIELD;
- indicator LED + resistor controlled from the switched coil node;
- external contact terminal: Phoenix Contact MKDS 3/3-5.08 BK 1712193, NC / COM / NO.

GPIOs: relay 1..4 = GPIO4, GPIO5, GPIO6, GPIO7.

Safety/layout rules:
- all relays OFF during boot/reset/fault unless firmware later intentionally energizes them;
- contact copper kept in a dedicated field/contact zone;
- no contact trace passes under/through MCU, USB, Ethernet or RS485 logic;
- design target >=6 mm copper clearance from possible mains contact nets to SELV logic, with isolation slots where geometry benefits;
- base board traces/terminals are designed around a 5 A continuous resistive contact-path target; relay markings do not increase the PCB-system rating;
- any 230 VAC use remains a product safety/compliance responsibility requiring final spacing/enclosure/standard review.

## 8. Field connectors

Frozen Rev-A field connector MPNs:
- field power: Same Sky TB007-508-02BE, 2-position 5.08 mm;
- RS485 A/B: Same Sky TB007-508-03BE, 3-position 5.08 mm;
- optional DI: Same Sky TB007-508-05BE, 5-position 5.08 mm, DNP in base build;
- relay contacts: Phoenix Contact MKDS 3/3-5.08 BK 1712193;
- HMI: JST B4B-XH-A;
- USB service: GCT USB4105-GF-A-120.

The provider DFM review must verify exact body/stake/enclosure clearances against the STEP export before fabrication release.

## 9. Optional isolated digital inputs

Four channels, DNP-capable in Lite/Base BOM.

Per-channel starting topology:
- field input terminal DIx referenced to DI_COM;
- 3.3 kΩ, >=0.25 W series resistor;
- optocoupler LED (LTV-817/PC817-class);
- reverse diode across optocoupler LED;
- transistor collector -> ESP GPIO with 10 kΩ pull-up to 3V3;
- emitter -> logic GND;
- optional 100 nF debounce capacitor at logic node.

GPIOs: DI1..DI4 = GPIO1, GPIO2, GPIO47, GPIO48.

If populated, final threshold/CTR/dissipation must be verified on 12 V and 24 V bench supplies during H4.

## 10. Optional RTC

PCF8563-class I2C RTC, 32.768 kHz crystal, backup cell provision, DNP-capable.

GPIO38 SDA, GPIO39 SCL. Pull-ups to 3V3. Battery accessible with enclosure opened.

## 11. Optional microSD

SPI microSD, DNP-capable.

GPIO40 SCLK, GPIO41 MOSI, GPIO37 MISO, GPIO36 CS. Local decoupling and user-accessible ESD are implemented according to the optional path.

## 12. Manufacturing/service test points

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

## 13. PCB stack-up and zones

Rev-A prototype: 4 layers, 1.6 mm FR-4, 1 oz outer copper unless the fabrication/thermal review explicitly requests otherwise.

Stack:
- L1: components + signals + local power;
- L2 / `In1.Cu`: near-continuous dedicated GND reference plane;
- L3 / `In2.Cu`: controlled low-speed/power routing;
- L4: low-speed signals / ground fill.

Zones:
1. relay contact edge;
2. relay coils + field power;
3. logic/ESP32;
4. Ethernet/MagJack edge;
5. RS485/HMI edge;
6. optional DI/RTC/SD.

The routed release must pass native KiCad DRC, 0-unconnected audit and repository signal-integrity geometry validation before H2 is marked complete.

## 14. Mechanical envelope

PCB target: 145 mm x 95 mm within the controlled Rev-A enclosure contract.

Enclosure target: approximately 158 x 108 x 48 mm, TS35 DIN-rail mounting, with field/contact and SELV/service connector zoning. The mechanical contract and STEP model are validated by CI before provider handoff; production tooling remains blocked until physical fit/thermal validation.

## 15. H1 acceptance — PASS

H1 schematic freeze requirements are now satisfied digitally:
- mandatory component-level circuits present in native KiCad;
- optional DNP circuits present and marked;
- GPIO and connector physical-pin audit passes;
- power-budget and controlled-component manifests committed;
- KiCad parses project and schematic;
- ERC = 0 unwaived violations;
- deterministic netlist/BOM/PDF exports available.

H1 does not claim physical rail, EMC, thermal or field-interface performance.

## 16. H2 / H3 release boundary

H2 requires one persisted routed native PCB with:
- KiCad DRC = 0;
- unconnected = 0;
- L2 ground strategy PASS;
- signal-integrity geometry PASS;
- STEP export PASS;
- enclosure/mechanical contract PASS.

H3 then requires manufacturing Gerbers/drill/CPL/assembly outputs and the controlled provider ZIP tied to one release commit SHA.

Only H4 is intentionally physical: current-limited first power, reverse-polarity/backfeed tests, rail/ripple/thermal measurements, Ethernet, both RS485 ports, HMI, four relays, optional populated blocks and enclosure fit/soak validation.
