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

- [x] All schematic components have resolved footprints under the controlled release generator.
- [x] Board outline and mounting holes frozen by the release generator/mechanical contract.
- [x] Field-connector mechanical placement is covered by the controlled placement/mechanical gates.
- [x] RJ45 and USB-C board-edge/pad placement contract PASS.
- [x] Relay/contact zone separation from logic/communications contract PASS.
- [x] ESP32 antenna keep-out contract PASS.
- [x] Ethernet clock/magnetics/W5500 placement and critical-route geometry gate PASS.
- [x] USB D+/D- critical-route geometry gate PASS.
- [x] Two RS485 transceiver/TVS/termination area contracts PASS.
- [x] Buck hot-loop/current-path contract PASS.
- [x] Relay contact and coil-current path contract PASS.
- [x] L2 ground/reference-plane strategy gate PASS.
- [x] Routing complete; `UNCONNECTED=0`.
- [x] KiCad DRC = 0 release-blocking violations.
- [x] Schematic/PCB electrical contract/parity audits PASS.
- [x] STEP export PASS.

**H2 status: PASS**

### H2 authoritative evidence

- Successful KiCad Rev-A validation run: `33797012638` on source `0330eed27eda84fb08ecf3cb49345719229d1f01`.
- The successful workflow persisted the routed native checkpoint as commit `324e0db1600c2fd883d83f923a0c442669b237f0` with `H2_ROUTING_COMPLETE`.
- H2 proof tokens enforced by the workflow: `ERC=0`, `DRC=0`, `UNCONNECTED=0`, `L2_GND=PASS`, `SIGNAL_INTEGRITY_GEOMETRY=PASS`, `STEP=0`.
- Engineering-evidence artifact: `kicad-reva-validation`, artifact id `9909977211`, digest `sha256:246830e56b8a17be3a0057186e7e30102c5e5dd371fb3bf4e64c2279502e8ea7`.

## H3 — Provider manufacturing package

The controlled H2 workflow has generated and validated the automated provider package contract. The following generated outputs are proven present by the release workflow/package builder:

- [x] Native KiCad project (`.kicad_pro`, `.kicad_sch`, `.kicad_pcb`).
- [x] Schematic PDF.
- [x] Final generated BOM/manufacturing BOM.
- [x] CPL / pick-and-place file.
- [x] Gerber set.
- [x] NC drill files.
- [x] IPC-D-356 connectivity export.
- [x] Assembly drawing, top and bottom.
- [x] STEP 3D model.
- [x] Final DRC/ERC reports and board statistics.
- [x] Controlled hardware-interface contract.
- [x] PCB/enclosure RFQ document included.
- [x] Provider package contents manifest included.
- [x] This readiness checklist included.
- [x] Mechanical handoff JSON and enclosure inputs included.
- [x] Embedded `RELEASE_COMMIT.txt` and SHA256 manifest generated from one release checkout.

**H3 automated package status: PASS**

### H3 authoritative evidence

- Provider-package build completed in run `33797012638` after H2 gates.
- Provider artifact: `Automatrix-PVDG-RevA-provider-package`, artifact id `9909976209`.
- Provider artifact digest: `sha256:869bc723cd05f106aab850aa3de65bb4b46d600b77bc08e91dbedcaef41bd496`.
- Package builder fails closed if H2 proof, native sources, schematic/BOM/STEP, Gerber/drill/CPL, assembly drawings, IPC-D-356, final DRC/ERC, board statistics, mechanical handoff or controlled provider documents are missing.

H3 PASS means a controlled quotation/prototype-manufacturing package exists. It does **not** claim that a fabricated prototype or final production enclosure has passed H4.

## Enclosure / casing package

- [x] PCB STEP model generated.
- [x] Board outline/mounting/connector mechanical handoff generated and validated.
- [x] Enclosure specification and SCAD source included in the provider package.
- [x] Relay/contact safety-zone and antenna/mechanical constraints are part of the controlled design/handoff contract.
- [ ] Physical prototype enclosure fit/cutout validation.
- [ ] Production enclosure tooling approval after prototype validation.

Injection-mould tooling remains prohibited before prototype fit and field validation.

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

**H4 status: PHYSICAL PROTOTYPE REQUIRED**

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
