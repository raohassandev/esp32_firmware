# Automatrix PV-DG Controller — Rev-A DIN-Rail Enclosure Specification

**Status:** mechanical implementation working specification; connector cutout coordinates remain linked to PCB placement until H2 PCB freeze.  
**Electrical parent:** `hardware/kicad/ELECTRICAL_DESIGN_FREEZE.md`  
**PCB target envelope:** <=145 mm x 95 mm where practical.

## 1. Enclosure concept

Rev-A uses a two-part industrial panel enclosure:

- injection-mouldable/3D-printable PC/ABS-style base;
- removable screw-retained lid;
- internal PCB standoffs;
- external 35 mm DIN-rail clip/foot on the base;
- field wiring exits on opposite long edges to keep relay contacts separated from SELV communications;
- RJ45 and USB-C edge access;
- no exposed live relay-contact copper when the lid is fitted.

Prototype casing must be printable without custom tooling. Production tooling is explicitly deferred until PCB fit, thermal and field wiring validation pass.

## 2. Working outer envelope

Parametric starting envelope:

- outer length: 158 mm;
- outer width: 108 mm;
- outer height: 48 mm;
- wall: 2.2 mm nominal;
- base floor: 2.5 mm nominal;
- lid wall/top: 2.0–2.2 mm;
- corner radius: 3 mm nominal.

These dimensions are intentionally slightly larger than the working PCB envelope to leave terminal wiring, creepage barriers and wall clearance. Final dimensions may shrink after PCB placement.

## 3. PCB mounting

Working PCB mounting pattern:

- PCB nominal: 145 x 95 mm maximum target;
- four M3 mounting holes, **5 mm inset from PCB corners**, matching KiCad coordinates `(5,5)`, `(140,5)`, `(5,90)`, `(140,90)`;
- enclosure standoff height: 5 mm;
- standoff OD: >=7 mm;
- screw pilot appropriate to M3 self-tapping prototype or heat-set insert production option;
- minimum 2 mm board-edge-to-wall mechanical clearance, increased near terminals/RJ45 as required.

Final hole coordinates must be exported from the frozen KiCad board and copied into the enclosure source; the enclosure is not allowed to invent different mounting coordinates.

## 4. Functional sides

### Side A — relay/contact side

Four externally labelled groups: RELAY 1..4, each **NC / COM / NO**.

Requirements:

- finger-safe terminal bodies preferred;
- lid/wall geometry must prevent casual access to PCB contact copper;
- relay-contact wiring must not share cable openings with USB, Ethernet, RS485 or HMI;
- internal insulating barrier/rib between relay-contact zone and low-voltage zone preferred;
- production enclosure should target UL94 V-0 PC/ABS or better if mains relay use is marketed.

### Side B — SELV/service side

Provision for:

- 12/24 VDC power input;
- RS485-A A/B/GND;
- RS485-B A/B/GND;
- HMI +5V/GND/TX/RX;
- optional RS232 HMI connector;
- optional DI1..DI4/COM.

### End C — network/service

Provision for:

- RJ45 Ethernet;
- USB-C service/programming;
- RESET/BOOT service access if not top-accessible;
- optional microSD access only after H2 confirms a collision-free user-accessible location.

### Top/lid

Provision for POWER, STATUS, RELAY 1..4 and optional DI indicators. microSD, if customer-accessible, must use a deliberate slot/retention geometry; otherwise it remains internal-service-only.

## 5. DIN rail

- 35 mm top-hat DIN rail concept (EN 60715 / common TS35 form factor);
- prototype may use a commercial screw-on DIN clip;
- base includes two M3/clearance mounting positions for the external DIN clip;
- custom snap geometry deferred until retention cycling is tested.

## 6. Environmental/mechanical target

Indoor electrical control-panel use; no outdoor/IP claim before independent validation. No fan. Keep ventilation away from possible mains contact copper. Board must be removable without desoldering field connectors. USB/RJ45 cable insertion must not flex the PCB excessively.

## 7. Thermal strategy

Switching regulators stay away from ESP32 antenna and relay contact zone. First prototype thermal test is required with all four relays energized, HMI auxiliary output loaded, Ethernet active and Wi-Fi active at 12 V and 24 V inputs.

## 8. Safety separation geometry

- casing preserves PCB high-voltage/SELV boundary;
- mounting hardware must not reduce electrical clearance;
- no metal DIN clip directly under exposed high-voltage/contact copper without adequate spacing;
- internal insulating rib follows relay/logic separation;
- production label states only complete-product contact ratings actually validated.

## 9. Prototype mechanical acceptance

Mechanical physical acceptance requires: frozen PCB outline/mounting, verified connector dimensions/3D, PCB STEP fit, real printed/machined enclosure fit, cable insertion/removal checks, DIN retention, and proof that relay/contact wiring cannot enter the SELV cable area unintentionally.

## 10. Source of truth

`hardware/mechanical/Automatrix_PVDG_RevA_enclosure.scad` is the parametric prototype source. Connector/cutout coordinates are copied from the H2-frozen KiCad/STEP model; no enclosure cutout is finalized from visual guessing.
