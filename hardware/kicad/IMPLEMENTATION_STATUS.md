# Rev-A KiCad Implementation Status

**Branch:** `hardware/kicad-reva-implementation`  
**Parent specification:** `hardware/pcb-reva-plan`  
**Canonical requirements:** `docs/HARDWARE_PCB_REVA_MASTER_PLAN.md`  
**KiCad project:** `hardware/kicad/Automatrix_PVDG_RevA.kicad_pro`  
**Status:** IMPLEMENTATION STARTED — schematic not yet frozen

## Design ownership rule

This branch contains the actual electrical/mechanical implementation. The master hardware plan remains the requirements authority. Any implementation discovery that changes cost, interface count, connector type, GPIO assignment, electrical rating, enclosure size or safety assumption must be reflected back into the master plan before Rev-A is frozen.

A sheet/block is not COMPLETE because it was drawn. It becomes complete only after electrical review, datasheet/reference-design review, ERC and the relevant bench/prototype validation gate.

## Rev-A frozen functional scope

### Mandatory populated

- ESP32-S3-WROOM-1-N8.
- 12/24 VDC field input and protected power conversion.
- 10/100 Ethernet using W5500-class SPI Ethernet controller and magnetics RJ45.
- Two independent protected half-duplex RS485 ports.
- Four SPDT/Form-C electromechanical dry-contact relay outputs, NC/COM/NO available externally.
- One independent serial touch-HMI UART port with protected 5 V auxiliary supply.
- USB-C native ESP32-S3 programming/service interface.
- BOOT, RESET, power/status and relay indications.

### Optional / DNP-capable on same PCB

- Four optically isolated 12/24 V digital inputs.
- RTC with backup cell provision.
- microSD.
- RS232 transceiver option for HMI variant.
- Isolated-RS485 variant is an alternate design/cost study, not base Rev-A.

## Schematic implementation order

The design will be built and reviewed in this order to prevent downstream PCB rework:

1. `PWR` — field input protection, 5 V field rail, 3.3 V logic rail, USB/field power OR-ing/back-feed control.
2. `MCU` — ESP32-S3 module, EN/BOOT, decoupling, native USB, antenna keep-out requirements and test points.
3. `ETH` — W5500, clock, SPI, reset/interrupt, magnetics RJ45, ESD and reference-design network.
4. `RS485A` — UART transceiver, DE/RE, boot-safe bias, TVS, selectable termination/bias, terminal and test points.
5. `RS485B` — independent copy with independent UART/control; no shared DE/RE.
6. `HMI` — dedicated UART, 3.3 V/5 V compatibility, 5 V protected auxiliary output and optional RS232 path.
7. `RELAY` — four coil drivers, flyback, OFF-at-reset hardware bias, status LEDs, contact terminals and HV/LV zoning.
8. `DI_OPT` — four optional isolated 12/24 V digital inputs.
9. `RTC_OPT` — optional RTC and backup source.
10. `SD_OPT` — optional SPI microSD.
11. `TEST` — manufacturing/service test points and programming/bring-up access.
12. Root sheet review, annotation, ERC, BOM review and GPIO reconciliation.

## Preliminary GPIO allocation under implementation

| Function | GPIO | Implementation state |
|---|---:|---|
| USB D- | 19 | Reserved / native USB |
| USB D+ | 20 | Reserved / native USB |
| RS485-1 TX | 43 | Reserved |
| RS485-1 RX | 44 | Reserved |
| RS485-1 DE/RE | 42 | Reserved |
| RS485-2 TX | 17 | Reserved |
| RS485-2 RX | 18 | Reserved |
| RS485-2 DE/RE | 16 | Reserved |
| HMI TX | 15 | Reserved |
| HMI RX | 14 | Reserved |
| W5500 SCLK | 12 | Reserved |
| W5500 MOSI | 11 | Reserved |
| W5500 MISO | 13 | Reserved |
| W5500 CS | 10 | Reserved |
| W5500 INT | 9 | Reserved |
| W5500 RESET | 8 | Reserved |
| Relay 1 | 4 | Reserved |
| Relay 2 | 5 | Reserved |
| Relay 3 | 6 | Reserved |
| Relay 4 | 7 | Reserved |
| DI1..DI4 optional | 1,2,47,48 | Reserved pending strapping review |
| RTC SDA/SCL optional | 38,39 | Reserved |
| SD SCLK/MOSI/MISO/CS optional | 40,41,37,36 | Reserved |
| Status LED | 35 | Reserved |
| BOOT | 0 | Boot strap; must be treated carefully |

No PCB routing may be finalized until this table passes ESP32-S3 strapping/internal-flash/resource review and schematic ERC.

## Electrical design gates before PCB placement

- [ ] Field-input voltage range and surge/TVS strategy calculated.
- [ ] 5 V rail sized for HMI plus four simultaneous relay coils with margin.
- [ ] 3.3 V rail peak-current and transient margin calculated for ESP32-S3 + W5500 + all populated logic.
- [ ] USB-only power behavior proven not to energize relay coils/HMI auxiliary output.
- [ ] ESP32 boot/strapping pins reviewed against every external load.
- [ ] W5500 schematic checked against manufacturer reference design.
- [ ] Both RS485 ports checked for failsafe/boot behavior and field ESD.
- [ ] HMI electrical level assumption documented; no raw 5 V signal may reach ESP32 GPIO.
- [ ] Relay contact ratings and PCB creepage/clearance basis documented before any 230 VAC claim.
- [ ] Optional circuits proven not to compromise mandatory core when DNP.
- [ ] Component lifecycle/availability and at least one alternate considered for production-critical parts.

## PCB gates

- Preferred first implementation: 4 layer.
- User wiring must terminate at board edges.
- Relay-contact zone must be physically segregated from SELV/logic/communications.
- ESP32 antenna keep-out must remain free of copper, relays, magnetics, enclosure metal and cable bundles as far as practical.
- Ethernet differential routing and return path must be kept away from relay switching loops and buck-converter hot loops.
- Test points must be reachable after assembly and preferably with enclosure top removed.

## Mechanical/casing gates

- DIN-rail mounting is the default enclosure concept.
- PCB outline is not frozen until terminal placement and relay safety zoning are credible.
- Enclosure must provide service access to power, Ethernet, 2x RS485, HMI, four relay Form-C terminals, USB-C and optional DI/microSD as applicable.
- First prototype casing should be 3D printed or machined/adapted; injection mould tooling is forbidden before PCB and field-fit validation.

## Current checkpoint

**H0 — implementation bootstrap: PASS**

Evidence now present on the implementation branch:

- Separate branch created from the canonical Rev-A planning branch.
- KiCad project file created.
- Root schematic created.
- Schematic block order and electrical/PCB/mechanical gates recorded.
- Existing GPIO proposal transferred into implementation control.

**H1 — schematic freeze: NOT STARTED/NOT PASSED.**

H1 will require all mandatory schematic blocks, exact component MPNs, final connector pinout, power calculations, final GPIO review and clean ERC before PCB placement is allowed to be called frozen.
