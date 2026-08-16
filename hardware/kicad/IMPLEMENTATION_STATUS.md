# Rev-A KiCad Implementation Status

**Branch:** `hardware/kicad-reva-implementation`  
**Parent specification:** `hardware/pcb-reva-plan`  
**Canonical requirements:** `docs/HARDWARE_PCB_REVA_MASTER_PLAN.md`  
**KiCad project:** `hardware/kicad/Automatrix_PVDG_RevA.kicad_pro`  
**Status:** H1 SCHEMATIC FROZEN — H2 PCB IMPLEMENTATION IN PROGRESS

## Design ownership rule

This branch contains the actual electrical/mechanical implementation. The master hardware plan remains the requirements authority. Any implementation discovery that changes cost, interface count, connector type, GPIO assignment, electrical rating, enclosure size or safety assumption must be reflected back into the master plan before Rev-A is released.

A sheet/block is not COMPLETE because it was drawn. It becomes complete only after electrical review, datasheet/reference-design review, native KiCad validation and the relevant bench/prototype validation gate.

## Rev-A frozen functional scope

### Mandatory populated

- ESP32-S3-WROOM-1-N8.
- 12/24 VDC field input and protected power conversion.
- 10/100 Ethernet using W5500 SPI Ethernet controller and integrated-magnetics RJ45.
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

## Schematic implementation blocks

All Rev-A electrical blocks are now represented in the native KiCad schematic:

1. `PWR` — protected field input, 5 V field rail, 3.3 V logic rail, USB/field power OR-ing/back-feed control.
2. `MCU` — ESP32-S3 module, EN/BOOT, decoupling, native USB and service nodes.
3. `ETH` — W5500, clock, SPI, reset/interrupt, integrated magnetics RJ45 and PHY support network.
4. `RS485A` — independent UART transceiver, DE/RE control, TVS, termination/bias provision and terminal.
5. `RS485B` — independent second RS485 interface.
6. `HMI` — dedicated TTL UART, 5 V auxiliary supply/protection and DNP RS232 path.
7. `RELAY` — four Form-C relays, MOSFET drivers, flyback protection, OFF-at-reset bias, indicators and NC/COM/NO terminals.
8. `DI_OPT` — four optional isolated 12/24 V digital inputs.
9. `RTC_OPT` — optional RTC and backup source.
10. `SD_OPT` — optional SPI microSD.
11. Service/test/control support.

## Frozen GPIO allocation

| Function | GPIO | H1 status |
|---|---:|---|
| USB D- | 19 | Verified physical module pin/net |
| USB D+ | 20 | Verified physical module pin/net |
| RS485-1 TX | 43 | Verified |
| RS485-1 RX | 44 | Verified |
| RS485-1 DE/RE | 42 | Verified |
| RS485-2 TX | 17 | Verified |
| RS485-2 RX | 18 | Verified |
| RS485-2 DE/RE | 16 | Verified |
| HMI TX | 15 | Verified |
| HMI RX | 14 | Verified |
| W5500 SCLK | 12 | Verified |
| W5500 MOSI | 11 | Verified |
| W5500 MISO | 13 | Verified |
| W5500 CS | 10 | Verified |
| W5500 INT | 9 | Verified |
| W5500 RESET | 8 | Verified |
| Relay 1 | 4 | Verified |
| Relay 2 | 5 | Verified |
| Relay 3 | 6 | Verified |
| Relay 4 | 7 | Verified |
| Optional DI1..DI4 | 1,2,47,48 | Verified |
| Optional RTC SDA/SCL | 38,39 | Verified |
| Optional SD SCLK/MOSI/MISO/CS | 40,41,37,36 | Verified |
| Status LED | 35 | Verified |
| BOOT | 0 | Dedicated boot strap; no field load |

A second independent checker audits these physical ESP32/W5500/RS485/USB/HMI/relay pins from the KiCad-exported netlist rather than trusting the generator alone.

## H1 electrical design gates

- [x] Field-input voltage/protection architecture documented.
- [x] 5 V rail load budget recorded for HMI + four relays + downstream logic.
- [x] 3.3 V rail load budget recorded for ESP32-S3 + W5500 + communication/optional logic.
- [x] USB and field-power rails are separate in the schematic with no intended relay/HMI energization from USB-only power.
- [x] ESP32 boot/strapping/resource allocation reviewed and independently exported-net checked.
- [x] W5500 physical pin map and required support network reviewed against manufacturer data.
- [x] Both RS485 ports have independent UART/control and boot-safe driver-disable intent.
- [x] HMI input protection prevents intentional raw 5 V drive into ESP32 RX.
- [x] Relay contact rating/PCB spacing basis documented; complete product is **not** claimed certified for mains merely from relay component rating.
- [x] Optional circuits are DNP-capable and do not form mandatory core dependencies.
- [x] Controlled component list/BOM target and cost-down substitution rule committed.
- [x] Native KiCad 10.0.5 schematic upgrade/parse PASS.
- [x] Native KiCad ERC: **0 errors, 0 warnings**.
- [x] Exported physical pin/net audit PASS.
- [x] Canonical annotation: 149 unique component references PASS.
- [x] BOM/netlist/schematic-PDF evidence generated by CI.

Physical behavior is deliberately not claimed by H1. USB back-feed, regulator thermal performance, RS485 electrical margins, relay switching/contact temperature and HMI loading remain H4 prototype tests.

## PCB gates

- Preferred implementation: 4 layer, 1.6 mm FR-4.
- Working board envelope: <=145 x 95 mm if the electrically safe layout fits.
- User wiring terminates at accessible board edges.
- Relay-contact zone is physically segregated from SELV/logic/communications.
- ESP32 antenna keep-out must remain free of inappropriate copper/metal/components.
- Ethernet differential routing and return path stay away from relay switching loops and buck hot loops.
- Test/service access remains available after assembly.
- `hardware/kicad/PCB_PLACEMENT_ROUTING_RULES.md` is the H2 layout rule authority.

## Mechanical/casing gates

- DIN-rail mounting is the default enclosure concept.
- PCB outline/mounting holes become the source for final casing coordinates after H2.
- Enclosure provides service access to power, Ethernet, 2x RS485, HMI, four relay Form-C terminals, USB-C and optional DI/microSD as applicable.
- Parametric prototype casing source exists under `hardware/mechanical/`.
- First physical casing is 3D printed/adapted; injection-mould tooling remains forbidden before PCB/field-fit validation.

## Current checkpoints

### H0 — implementation bootstrap: PASS

- Separate implementation branch created from canonical planning branch.
- KiCad project, controlled component decisions and validation pipeline established.

### H1 — schematic freeze: PASS

Authoritative KiCad 10.0.5 evidence:

- deterministic schematic generator: PASS;
- desired physical-pin manifest: PASS;
- native schematic upgrade: PASS;
- ERC: **0 errors / 0 warnings**;
- exported physical-pin/net audit: PASS;
- canonical annotation: PASS;
- design-control invariant check: PASS;
- generated BOM, netlist and schematic PDF: PASS;
- validated native schematic persisted to the branch.

### H2 — PCB placement/routing: IN PROGRESS

- 4-layer PCB generator added.
- exact schematic pad-to-footprint net assignment is fail-closed.
- industrial functional placement engine added.
- first placement iteration exposed density around the W5500 support cluster; functional-zone packing is being corrected rather than increasing board size without evidence.
- H2 cannot pass until placement, routing, schematic parity, DRC, STEP/manufacturing outputs and safety-layout review are all clean.

### H3 — enclosure mechanical freeze: IN PROGRESS / BLOCKED BY H2

- enclosure specification and parametric OpenSCAD prototype source exist.
- final connector cutouts and mounting coordinates wait for H2 PCB freeze.

### H4 — fabricated prototype / bench validation: NOT STARTED

Requires real assembled boards and physical evidence. No physical-production-ready claim may be made before H4 passes.
