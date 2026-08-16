# PCB Service Provider Handoff Readiness — Rev-A

**Project:** Automatrix PV-DG / ESP32 controller Rev-A  
**Implementation branch:** `hardware/kicad-reva-implementation`  
**Requirements branch:** `hardware/pcb-reva-plan`  
**Purpose:** This file is the release gate for material sent to a PCB/PCBA/enclosure service provider.

## Rule

No quotation/manufacturing package may be called **FINAL** until every item under H1-H3 is PASS. Prototype electrical tests under H4 remain post-fabrication acceptance and must not be confused with pre-fabrication design proof.

## H1 — Electrical schematic freeze

- [x] ESP32-S3-WROOM-1-N8 core defined.
- [x] W5500 Ethernet section defined.
- [x] 2 independent RS485 ports defined.
- [x] Dedicated serial touch-display interface defined.
- [x] Four electromechanical Form-C relay outputs defined.
- [x] 12/24 V field input, 5 V field rail and 3.3 V logic rail defined.
- [x] USB-C native programming/service interface defined.
- [x] Optional 4 DI, RTC, microSD and RS232-HMI provisions defined.
- [x] Canonical annotation generated.
- [x] KiCad 10.0.5 native parse PASS.
- [x] ERC = 0 errors / 0 warnings.
- [x] Exported physical pin/net audit PASS.
- [x] BOM and schematic PDF export PASS.

**H1 status: PASS**

## H2 — PCB implementation freeze

- [ ] All 149 schematic components have resolved footprints.
- [ ] Board outline and mounting holes frozen.
- [ ] All field connectors have verified mechanical placement.
- [ ] RJ45 and USB-C pad-inside-board proof PASS.
- [ ] Relay/contact high-voltage zone separated from logic/communications.
- [ ] ESP32 antenna keep-out protected.
- [ ] Ethernet clock/magnetics/W5500 placement reviewed.
- [ ] USB D+/D- routing reviewed.
- [ ] Two RS485 transceiver/TVS/termination areas reviewed.
- [ ] Buck hot-loop and current paths reviewed.
- [ ] Relay contact and coil current paths reviewed.
- [ ] Ground/power plane strategy implemented.
- [ ] Routing complete; no unrouted mandatory nets.
- [ ] KiCad DRC = 0 release-blocking violations.
- [ ] Schematic/PCB parity PASS.
- [ ] STEP export PASS.

**H2 status: IN PROGRESS**

## H3 — Provider manufacturing package

The handoff ZIP/directory must contain exactly controlled outputs from the same release commit:

- [ ] Native KiCad project (`.kicad_pro`, `.kicad_sch`, `.kicad_pcb`).
- [ ] Schematic PDF.
- [ ] Final BOM with manufacturer part numbers, package, quantity and DNP/optional status.
- [ ] CPL / pick-and-place file.
- [ ] Gerber set.
- [ ] NC drill files.
- [ ] IPC/netlist or equivalent connectivity export if requested by provider.
- [ ] PCB fabrication drawing / stack-up notes.
- [ ] Assembly drawing, top and bottom.
- [ ] STEP 3D model.
- [ ] Connector pinout table.
- [ ] Test-point / programming instructions.
- [ ] Required PCB finish, copper weight, thickness and soldermask notes.
- [ ] Relay-contact creepage/clearance design basis documented.
- [ ] Optional/DNP population variants documented.
- [ ] `PCB_AND_ENCLOSURE_SERVICE_PROVIDER_RFQ.md` included.
- [ ] This readiness checklist included with release commit SHA.

**H3 status: NOT PASSED until H2 is clean**

## Enclosure / casing package

- [ ] PCB STEP model is final.
- [ ] Maximum component heights extracted.
- [ ] DIN-rail enclosure target dimensions frozen.
- [ ] Cutouts for 12/24 V power, Ethernet, RS485-A, RS485-B, HMI, USB-C and relay terminals frozen.
- [ ] Optional DI / microSD access decision frozen.
- [ ] Relay/contact wiring kept away from communication/user-service areas.
- [ ] ESP32 antenna area kept clear of metal enclosure features where applicable.
- [ ] Prototype enclosure method specified: 3D-print / CNC / modified off-the-shelf DIN housing.
- [ ] Injection-mould tooling explicitly prohibited before prototype fit and field validation.

## H4 — Post-fabrication prototype acceptance

Provider fabrication completion is not product validation. Prototype boards must pass:

- [ ] 12 V and 24 V input startup.
- [ ] Input reverse-polarity protection test.
- [ ] USB-only programming/boot behavior.
- [ ] No unsafe USB ↔ field-supply backfeed.
- [ ] 3.3 V and 5 V rail measurements under expected load.
- [ ] Ethernet link + sustained Modbus TCP traffic.
- [ ] RS485-A communication test.
- [ ] RS485-B communication test.
- [ ] Simultaneous dual-RS485 test.
- [ ] Serial touch display communication test.
- [ ] Relay 1..4 ON/OFF and boot/reset default-OFF test.
- [ ] Relay contact continuity NO/NC/COM verification.
- [ ] Optional DI/RTC/microSD tests for populated full variant.
- [ ] Thermal soak / enclosure temperature check.
- [ ] Firmware recovery/programming access verified with enclosure fitted.

## Provider quotation structure requested

Ask the service provider for separate prices for:

1. Schematic/PCB engineering review only.
2. Prototype bare PCBs: 5 and 10 pcs.
3. Prototype assembled PCBA: 5 and 10 pcs.
4. Production PCBA: 50 / 100 / 500 / 1000 pcs.
5. Full mandatory population.
6. Optional full population (4 DI + RTC + microSD + RS232 option where required).
7. Components, SMT assembly and THT assembly separately.
8. Electrical fixture / programming / functional test cost separately.
9. 4-layer base quote and any justified 2-layer cost-down alternative separately.
10. DIN-rail enclosure prototype and production enclosure separately.
11. 3D-printed/CNC enclosure prototype and injection-mould tooling as separate alternatives.
12. Shipping, taxes/duties and lead time separately.

## Release discipline

Every package sent externally must be named with revision and commit, for example:

`Automatrix_PVDG_RevA_PROVIDER_RFQ_<short-commit>.zip`

If PCB, BOM, casing or connector geometry changes after a quotation, increment the package revision and resend the complete controlled package rather than individual loose files.
