# Rev-A KiCad Implementation Status

**Branch:** `hardware/kicad-reva-implementation`  
**Parent specification:** `hardware/pcb-reva-plan`  
**Canonical requirements:** `docs/HARDWARE_PCB_REVA_MASTER_PLAN.md`  
**Provider release gate:** `docs/PCB_PROVIDER_HANDOFF_READINESS.md`  
**KiCad project:** `hardware/kicad/Automatrix_PVDG_RevA.kicad_pro`  
**Status:** H1 SCHEMATIC FREEZE PASS — H2 PCB IMPLEMENTATION IN PROGRESS

## Design ownership rule

This branch contains the actual electrical/mechanical implementation. The master hardware plan remains the requirements authority. Any implementation discovery that changes cost, interface count, connector type, GPIO assignment, electrical rating, enclosure size or safety assumption must be reflected back into the master plan before Rev-A is frozen.

A sheet/block is not COMPLETE because it was drawn. It becomes complete only after electrical review, datasheet/reference-design review, ERC and the relevant bench/prototype validation gate. External provider material is not FINAL until `docs/PCB_PROVIDER_HANDOFF_READINESS.md` passes its pre-fabrication release gates.

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

## H1 evidence — PASS

- Native KiCad 10.0.5 schematic generated and parsed.
- ERC: 0 errors / 0 warnings.
- Exported physical pin/net audit PASS with 495 connected pins indexed.
- Canonical annotation PASS with 149 unique references.
- Design-control invariants PASS.
- BOM, netlist and schematic PDF exports PASS.

Physical tests such as USB/field backfeed, relay switching, thermal behavior and field RS485 are deliberately deferred to the post-fabrication prototype gate; H1 is a digital electrical-design freeze, not bench validation.

## H2 current work

H2 is implementing the 4-layer PCB from the validated schematic/netlist.

Completed foundation:

- All schematic components are fed into deterministic footprint/net generation.
- Industrial functional zones are defined for power, MCU, Ethernet, dual RS485, HMI, relays and optional blocks.
- Placement validation uses real KiCad courtyard/pad geometry rather than silkscreen/reference text.
- Edge connectors are required to keep every solder/drilled pad inside the fabricated PCB.
- RJ45 and USB-C edge positions are now auto-fitted from their actual KiCad footprints rather than hard-coded assumptions.

Current H2 gate:

1. obtain clean deterministic component placement,
2. review field connector/relay/antenna geometry,
3. route power and safety-critical paths,
4. route Ethernet/USB and communications,
5. finish remaining signals/planes,
6. achieve clean KiCad DRC and schematic parity,
7. export STEP and manufacturing package,
8. freeze enclosure cutouts from the final STEP model.

**H2 status: IN PROGRESS — no production/manufacturing-ready claim yet.**

## Release / provider rule

The PCB service provider will receive a single controlled package only after H2 passes. It will include the native KiCad sources, PDF schematic, BOM, CPL, Gerbers, drill files, drawings, STEP, pinouts, RFQ and provider-readiness checklist, all tied to one release commit SHA.
