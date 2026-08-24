# Rev-A KiCad Implementation Status

**Branch:** `hardware/kicad-reva-implementation`  
**Parent specification:** `hardware/pcb-reva-plan`  
**Canonical requirements:** `docs/HARDWARE_PCB_REVA_MASTER_PLAN.md`  
**Provider release gate:** `docs/PCB_PROVIDER_HANDOFF_READINESS.md`  
**KiCad project:** `hardware/kicad/Automatrix_PVDG_RevA.kicad_pro`  
**Status:** H1 SCHEMATIC FREEZE PASS — H2 RELEASE WORKFLOW PENDING

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
- Exported physical pin/net audit PASS.
- Canonical annotation PASS.
- Design-control invariants PASS.
- BOM, netlist and schematic PDF exports PASS.

Physical tests such as USB/field backfeed, relay switching, thermal behavior and field RS485 are deliberately deferred to the post-fabrication prototype gate; H1 is a digital electrical-design freeze, not bench validation.

## H2 release candidate evidence

PR validation run `32522110531` on head `36381f09b0fa09f9d08d70b3016a37922d36d2e3` produced a clean digital PCB candidate:

- 190 canonical schematic references and 593 connected physical pins audited.
- Deterministic 4-layer placement PASS with 194 footprints and 144 schematic nets.
- Freerouting plus controlled critical-net restoration completed the board.
- Post-route audit: 0 unconnected items, 1754 traces and 278 vias.
- KiCad 10.0.5 final DRC: 0 violations, 0 unconnected pads and 0 footprint errors.
- L2 solid-ground strategy and signal-integrity geometry gate PASS.
- Ethernet critical routes and native USB route geometry passed the repository gate.

This proves the routing candidate digitally, but H2 is not released until the branch push workflow persists the routed native PCB, emits `H2_ROUTING_COMPLETE`, exports STEP, validates the enclosure contract, and builds the controlled provider package from one release SHA.

## H2 release gate

1. persist the clean routed native PCB on this branch,
2. emit the H2 completion marker tied to the release commit,
3. export STEP successfully,
4. validate the enclosure/mechanical handoff,
5. export manufacturing outputs,
6. build and retain the controlled provider package.

**H2 status: RELEASE WORKFLOW PENDING — no production/manufacturing-ready claim until the push release workflow passes.**

## Release / provider rule

The PCB service provider will receive a single controlled package only after H2 passes. It will include the native KiCad sources, PDF schematic, BOM, CPL, Gerbers, drill files, drawings, STEP, pinouts, RFQ and provider-readiness checklist, all tied to one release commit SHA.
