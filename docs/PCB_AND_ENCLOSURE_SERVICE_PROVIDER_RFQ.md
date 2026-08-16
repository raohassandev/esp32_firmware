# PCB + Enclosure Design / Manufacturing RFQ

**Project:** Automatrix ESP32 PV-DG Controller — Rev-A  
**Date:** 2026-08-16  
**Purpose:** Request quotation for complete schematic, PCB, prototype assembly, testing support and industrial enclosure/casing design.  
**Canonical technical plan:** `docs/HARDWARE_PCB_REVA_MASTER_PLAN.md`

---

## 1. What we need from the service provider

Please quote the following scopes **separately** so we can compare design cost, prototype cost and production cost:

1. Electronic schematic design/review.
2. PCB layout and DFM.
3. Complete editable source files.
4. Prototype PCB fabrication.
5. Prototype component procurement and assembly.
6. Prototype bring-up / electrical testing.
7. Production PCBA pricing at multiple quantities.
8. Industrial DIN-rail enclosure/casing mechanical design.
9. Prototype enclosure fabrication / 3D printing.
10. Optional custom injection-mould tooling and production enclosure pricing.

The design must be suitable for low-cost production but must not compromise basic industrial field reliability, protection, serviceability or electrical safety.

---

## 2. Existing firmware platform

The product firmware already runs on **ESP32-S3 using ESP-IDF** and currently performs Modbus TCP communication through a network/ZLAN test arrangement.

The new PCB will remove the external ZLAN dependency for local RS485 devices by adding two direct RS485 interfaces while retaining Modbus TCP through wired Ethernet.

The PCB design provider is **not required to rewrite the firmware** unless firmware work is separately quoted. However, the provider must supply all hardware information needed by the firmware team: GPIO map, power behavior, transceiver control polarity, LEDs, connector pinout and test procedure.

---

## 3. Mandatory hardware requirements

### 3.1 Processor

- ESP32-S3 module.
- Preferred part: ESP32-S3-WROOM-1-N8.
- 8 MB flash target.
- No PSRAM required in base version.
- Module antenna must follow Espressif keep-out and placement guidance.

### 3.2 Field power

- Nominal input: **12/24 VDC**.
- Target operating range: approximately **9–30 VDC**.
- Reverse-polarity protection.
- Input fuse or resettable PTC.
- TVS/surge protection.
- Filtering suitable for industrial panel use.
- Pluggable 2-pin input terminal.
- 5 V field rail designed for relay coils and HMI auxiliary power.
- 3.3 V rail for MCU/Ethernet/peripherals.
- Supplier must provide a power budget and regulator thermal calculation.

### 3.3 Ethernet — mandatory

- 1 x 10/100 Ethernet port.
- W5500 or approved equivalent compatible with ESP32-S3/ESP-IDF.
- RJ45 with integrated magnetics preferred.
- Link/activity LEDs preferred.
- ESD protection at RJ45.
- Correct reference-design magnetics, clock, filtering and differential-pair routing.
- PoE not required.

### 3.4 RS485 — mandatory two ports

**Quantity: 2 independent ports.**

Each port must include:

- Half-duplex RS485 transceiver.
- Separate ESP32 UART and DE/RE control.
- 3-pin pluggable terminal: A, B, GND.
- TVS protection.
- Selectable 120-ohm termination.
- Optional/selectable bias resistors.
- Safe receive/non-driving state at boot/reset.
- Test points.
- Industrial-temperature transceiver preferred.

Base quotation should use **non-isolated RS485** for cost control.

Please also provide a **separate alternate price** for galvanically isolated RS485 on both ports if practical.

### 3.5 Relay outputs — mandatory

**Quantity: 4.**

- Electromechanical dry-contact relays.
- Preferred SPDT / Form-C contacts.
- Each channel: NC, COM, NO externally available.
- Total relay terminals: 12.
- Minimum preferred contact target for quotation: **5 A at 250 VAC resistive / 5 A at 30 VDC resistive**, or better.
- Please quote a 10 A relay option if the cost/size difference is reasonable.
- 5 V coil preferred.
- Transistor/MOSFET/array coil drivers.
- Flyback protection.
- One LED per relay.
- Hardware pulldowns so all relays remain OFF during MCU boot/reset/failure.
- Relay contact section physically separated from low-voltage logic.
- Use appropriate creepage/clearance, slots and terminal ratings for the declared switching voltage.
- Provider must clearly state whether the finished PCB is being designed/claimed for 230 VAC relay switching; if yes, provide the safety-spacing basis used.

### 3.6 Serial touch display — mandatory

- One dedicated UART serial HMI port.
- Base connection: TTL UART.
- Signals: +5 V, GND, TX, RX; optional fifth shield/service pin allowed.
- HMI 5 V auxiliary supply target: up to 1 A, protected and available from field power.
- ESP32 RX must be protected from a possible 5 V display TX level.
- Proper logic-level compatibility required.
- External connector must be locking/pluggable and serviceable.
- ESD protection if cable exits enclosure.

Please include an **optional DNP MAX3232/RS232 interface provision** or alternate price so the same PCB can support an RS232 HMI if needed later.

### 3.7 USB service/programming — mandatory

- USB Type-C.
- Use ESP32-S3 native USB; separate USB-UART bridge is not required unless provider gives a strong reason.
- USB ESD protection.
- Correct USB-C CC resistors.
- Board logic should be able to boot/program from USB for bench service.
- USB and field-power rails must not back-feed each other.
- Prefer relay/HMI field power to remain disabled when only laptop USB power is present.

### 3.8 Buttons / indicators

- RESET button.
- BOOT button.
- Power LED.
- System/status LED.
- Relay LEDs x4.
- Ethernet link/activity LEDs where supported by RJ45.

---

## 4. Optional hardware — same PCB, DNP allowed

Please include footprints/routing for the following features and quote their populated cost separately.

### 4.1 Four digital inputs

- Quantity: 4.
- 12/24 VDC field inputs.
- Optically isolated preferred.
- Input reverse/transient protection.
- RC filtering/debounce support.
- LED indication preferred.
- 5-pin terminal arrangement: DI1, DI2, DI3, DI4, COM.

### 4.2 RTC

- Low-cost I2C RTC such as PCF8563-class or approved equivalent.
- 32.768 kHz crystal as required.
- Coin-cell backup provision.
- Battery accessible after opening enclosure.

### 4.3 microSD

- microSD socket.
- SPI connection.
- ESD protection if externally accessible.
- Card detect optional.

### 4.4 Optional RS232 HMI section

- MAX3232-class interface or equivalent.
- DNP by default.
- Clear selection method between TTL and RS232 variant.

---

## 5. Preferred PCB construction

Please quote both options:

### Option A — preferred

- **4-layer PCB**.
- Continuous ground plane.
- Suitable power plane/routing.
- Controlled Ethernet routing.
- Standard industrial FR-4.
- 1.6 mm nominal thickness unless enclosure dictates otherwise.
- 1 oz copper minimum unless power/relay calculations require more.

### Option B — cost-down alternate

- 2-layer PCB.
- Only recommend if provider can maintain reliable ESP32 RF, Ethernet signal integrity, power integrity and relay-noise performance.

Provider must identify any technical compromise associated with the 2-layer option.

---

## 6. PCB size / mechanical target

Initial target PCB envelope: **not more than approximately 145 mm x 95 mm if practical**.

This is not a hard mechanical freeze. Final board size should be optimized around:

- 4 relay terminal groups.
- 2 RS485 connectors.
- 1 HMI connector.
- Power input.
- Optional DI terminal.
- RJ45.
- USB-C.
- DIN-rail casing.

All user-wired connectors should be near enclosure edges and easily accessible.

---

## 7. PCB functional zoning

Provider must physically separate these zones:

1. Relay contact / potential mains zone.
2. Relay coil + field power zone.
3. ESP32 low-voltage logic zone.
4. Ethernet/RJ45 zone.
5. RS485/HMI communication zone.
6. Optional DI/RTC/SD zone.

Requirements:

- Keep ESP32 antenna away from relays, inductors, Ethernet magnetics and metal casing parts.
- No high-voltage relay-contact trace through low-voltage communication/MCU areas.
- Keep Ethernet differential routing away from relay switching and power inductors.
- Add isolation slots where useful in relay contact zone.
- Preserve access to all service/test points.

---

## 8. Preliminary GPIO map

Provider may propose pin changes only where electrically necessary and must return an updated pin table before schematic freeze.

| Function | GPIO |
|---|---:|
| USB D- | 19 |
| USB D+ | 20 |
| RS485-1 TX | 43 |
| RS485-1 RX | 44 |
| RS485-1 DE/RE | 42 |
| RS485-2 TX | 17 |
| RS485-2 RX | 18 |
| RS485-2 DE/RE | 16 |
| HMI TX | 15 |
| HMI RX | 14 |
| W5500 SCLK | 12 |
| W5500 MOSI | 11 |
| W5500 MISO | 13 |
| W5500 CS | 10 |
| W5500 INT | 9 |
| W5500 RESET | 8 |
| Relay 1–4 | 4, 5, 6, 7 |
| Optional DI1–DI4 | 1, 2, 47, 48 |
| Optional RTC SDA/SCL | 38, 39 |
| Optional SD SCLK/MOSI/MISO/CS | 40, 41, 37, 36 |
| Status LED | 35 |
| BOOT | 0 |

Do not use GPIO26–32. Recheck GPIO33–37 if a future module with octal flash/PSRAM is proposed. Strapping pins must not be loaded in a way that causes boot problems.

---

## 9. Schematic deliverables required

Quotation must include delivery of:

- Complete editable KiCad project preferred.
- If another EDA tool is proposed, state it in quotation; editable sources remain mandatory.
- Hierarchical/block-organized schematic.
- PDF schematic.
- Exact part numbers for all components.
- Manufacturer name.
- Distributor/LCSC/JLCPCB part number where available.
- Electrical ratings.
- Approved alternates for supply-risk items.
- Power budget.
- Relay-contact safety notes.
- Connector pinout table.
- Final GPIO allocation.

No black-box or locked proprietary design files will be accepted as the only source deliverable.

---

## 10. PCB manufacturing deliverables required

- Editable PCB source.
- Gerber files.
- NC drill files.
- BOM CSV/XLSX.
- Pick-and-place / CPL file.
- Assembly drawings.
- Fabrication drawing.
- PCB stack-up information.
- 3D STEP model of assembled PCB.
- DRC report.
- DFM review/report.
- Netlist/schematic-to-PCB consistency confirmation.
- Test-point drawing.

---

## 11. Prototype quantity to quote

Please quote assembled prototype quantities separately:

- **5 pcs**.
- **10 pcs**.

For each quantity show:

- PCB fabrication.
- Components.
- SMT assembly.
- THT/manual assembly.
- Stencil/setup charges.
- Testing charges.
- Shipping estimate if available.
- Lead time.

---

## 12. Production PCBA quantities to quote

Please provide unit pricing for:

- 50 pcs.
- 100 pcs.
- 500 pcs.
- 1,000 pcs if supported.

For each quantity, separate:

- Bare PCB.
- Component BOM.
- Assembly.
- Programming/testing.
- Packaging.

Please identify components that dominate BOM cost and suggest qualified cost-down alternatives separately instead of silently substituting parts.

---

## 13. Prototype / factory test requirements

Provider should support or design for the following tests:

1. Visual AOI/manual inspection.
2. Power-off short/resistance check.
3. 24 V input power test.
4. 5 V rail measurement.
5. 3.3 V rail measurement.
6. ESP32 USB programming/boot test.
7. Ethernet link test.
8. Modbus TCP connectivity test using test firmware/golden device.
9. RS485-1 TX/RX test.
10. RS485-2 TX/RX test.
11. Simultaneous RS485 traffic test.
12. Relay 1–4 coil activation and contact continuity test.
13. HMI UART TX/RX test.
14. Optional DI test if populated.
15. Optional RTC read/write and backup test if populated.
16. Optional microSD read/write test if populated.
17. Minimum 24-hour powered soak test on engineering samples.

Please quote any test-jig development separately.

---

## 14. Enclosure / casing design requirements

### 14.1 Form factor

Preferred: **industrial 35 mm DIN-rail enclosure**.

Initial maximum target envelope: approximately **160 mm x 110 mm x 50 mm**, subject to final PCB layout and connector study.

### 14.2 Quote two enclosure approaches

#### Approach 1 — low-cost / low-NRE

- Existing off-the-shelf DIN-rail enclosure.
- Custom machining/cut-outs.
- Custom front label/printing.
- PCB fitted to stock enclosure.

#### Approach 2 — custom production enclosure

- Custom PC/ABS enclosure design.
- Prototype 3D print.
- Injection mould DFM.
- Tooling quote.
- Per-unit moulded enclosure pricing.

Do not assume injection mould tooling will be approved before PCB prototypes and field tests are successful.

### 14.3 Mechanical access requirements

The casing must provide accessible openings for:

- Ethernet RJ45.
- USB-C service port.
- RS485 port 1.
- RS485 port 2.
- HMI serial connector.
- 12/24 V power input.
- 4 relay NC/COM/NO groups.
- Optional DI connector.
- Optional microSD access if populated.
- BOOT/RESET service access where practical.
- Status LEDs / light pipes.

### 14.4 Casing layout requirements

- Relay contact terminals physically separated from communication connectors.
- Finger-safety considered if relay contacts are used for mains voltage.
- Screwdriver access to terminal screws.
- Cable bend space suitable for panel wiring.
- Ventilation only where required by thermal test.
- ESP32 antenna region must not be blocked by metal parts or copper foil/shields.
- Provide internal standoffs or slots for secure PCB mounting.
- DIN-rail clip must tolerate normal panel installation/removal.
- Space for product label, serial number, QR code, input rating and terminal legend.

### 14.5 Preferred production material

- Flame-retardant PC/ABS preferred for final production.
- Provider to recommend exact grade and flame rating based on final electrical/safety claim.
- Prototype material may be SLA/SLS/FDM/CNC as appropriate.

---

## 15. Enclosure deliverables required

- Editable mechanical CAD source.
- STEP assembly.
- STEP enclosure parts.
- STL files for prototype printing.
- 2D dimensioned PDF drawings.
- DXF cut-out drawings where applicable.
- Exploded assembly view.
- PCB-to-enclosure interference check.
- Fastener/insert/DIN-clip BOM.
- Front label / terminal legend artwork.
- Injection mould DFM report if custom moulding is quoted.

---

## 16. Enclosure quantities to quote

For stock/3D-printed enclosure path:

- 1 engineering sample.
- 5 pcs.
- 10 pcs.
- 100 pcs.

For injection-mould path:

- Tooling/NRE separately.
- 100 pcs.
- 500 pcs.
- 1,000 pcs.

Clearly separate tooling cost from unit cost.

---

## 17. Quotation format requested

Please return a line-item quotation containing at least:

| Item | Required price |
|---|---|
| Schematic engineering | Fixed NRE |
| PCB layout/DFM | Fixed NRE |
| Engineering revisions included | Number of revisions |
| 5-pc prototype PCBA | Total + per unit |
| 10-pc prototype PCBA | Total + per unit |
| 50-pc PCBA | Per unit |
| 100-pc PCBA | Per unit |
| 500-pc PCBA | Per unit |
| Optional 4DI population | Extra per unit |
| Optional RTC population | Extra per unit |
| Optional microSD population | Extra per unit |
| Optional RS232 HMI | Extra per unit |
| Optional isolated RS485 x2 | Extra per unit |
| Enclosure mechanical design | Fixed NRE |
| 3D-printed enclosure prototype | Per unit |
| Stock DIN enclosure modification | Per unit |
| Injection mould DFM | NRE |
| Injection mould tooling | Tool cost |
| Moulded casing | Per unit by quantity |
| Test jig | NRE |
| Production programming/test | Per unit |
| Lead time | Design / prototype / production |

---

## 18. Commercial / ownership requirements

Please state:

- Number of schematic/layout revisions included.
- Design lead time.
- Prototype lead time.
- Production lead time.
- Payment milestones.
- Warranty/rework terms for prototypes.
- Minimum order quantities.
- Component procurement source.
- Whether parts can be customer-supplied.
- Whether JLCPCB/LCSC sourcing is supported.

**All paid design outputs must be handed over in editable form.** The customer must receive the complete schematic, PCB, libraries used for custom symbols/footprints, Gerbers, BOM, CPL, mechanical CAD and enclosure drawings required to manufacture the product with another supplier in the future.

---

## 19. Supplier response questions

Please answer these with the quotation:

1. Do you recommend 4-layer or 2-layer for this ESP32 + W5500 + relays design, and why?
2. What is your proposed exact ESP32-S3 module MPN?
3. What W5500/RJ45 solution do you recommend?
4. What exact RS485 transceiver do you recommend for the low-cost version?
5. What is the extra cost for isolated RS485?
6. What relay MPN/contact rating do you propose?
7. Is the relay layout suitable for 230 VAC switching? If yes, what design/safety basis are you using?
8. What DC/DC regulators do you propose and what is the calculated thermal margin at 24 V input?
9. Can the HMI port deliver 5 V / 1 A continuously?
10. How will USB and field power be prevented from back-feeding each other?
11. What PCB dimensions do you estimate after placement?
12. Can you supply an off-the-shelf DIN-rail enclosure before custom tooling?
13. What editable design files will be delivered at project completion?
14. What prototype and production tests are included?
15. Which components are expected to have the highest supply-chain risk or cost?

---

## 20. Acceptance of design work

The electronic design phase will not be considered complete until:

- Schematic is reviewed and approved.
- GPIO map is frozen.
- Power budget is documented.
- PCB ERC/DRC is clean or exceptions documented.
- 3D enclosure fit is checked.
- Gerber/BOM/CPL match the approved source.
- All source files are delivered.

The prototype phase will not be considered complete until mandatory interfaces have been demonstrated on physical hardware: Ethernet, both RS485 ports, USB, HMI UART and all four relays.
