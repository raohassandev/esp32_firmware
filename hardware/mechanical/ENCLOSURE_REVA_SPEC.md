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
- four M3 mounting holes, nominal 4 mm inset from PCB corners;
- enclosure standoff height: 5 mm;
- standoff OD: >=7 mm;
- screw pilot appropriate to M3 self-tapping prototype or heat-set insert production option;
- minimum 2 mm board-edge-to-wall mechanical clearance, increased near terminals/RJ45 as required.

Final hole coordinates must be exported from the frozen KiCad board and copied into the enclosure source; the enclosure is not allowed to invent different mounting coordinates.

## 4. Functional sides

### Side A — relay/contact side

Four groups, each externally labelled:

- RELAY 1: NC / COM / NO
- RELAY 2: NC / COM / NO
- RELAY 3: NC / COM / NO
- RELAY 4: NC / COM / NO

Requirements:

- finger-safe terminal bodies preferred;
- lid/wall geometry must prevent casual access to PCB contact copper;
- relay-contact wiring must not share the same cable opening as USB, Ethernet, RS485 or HMI;
- internal printed barrier/rib between relay-contact zone and low-voltage zone is preferred;
- enclosure material for production should target UL94 V-0 PC/ABS or better if mains relay use is marketed.

### Side B — SELV/service side

Provision for:

- 12/24 VDC power input;
- RS485-A A/B/GND;
- RS485-B A/B/GND;
- HMI +5V/GND/TX/RX;
- optional DI1..DI4/COM.

### End C — network/service

Provision for:

- RJ45 Ethernet;
- USB-C service/programming;
- RESET/BOOT service access if not top-accessible.

### Top/lid

Provision for labels/indicators:

- POWER;
- STATUS;
- RELAY 1..4;
- optional DI indication if populated.

microSD, if fitted for customer access, must use a deliberate slot and retention geometry; otherwise it remains internal-service-only.

## 5. DIN rail

- rail standard: 35 mm top-hat DIN rail concept (EN 60715 / common TS35 form factor);
- prototype may use a commercially available screw-on DIN clip to avoid wasting design time on a fragile printed snap clip;
- enclosure base includes two M3/clearance mounting positions for the external DIN clip;
- production custom snap geometry may replace the commercial clip only after load/retention cycling is tested.

## 6. Environmental/mechanical target

Rev-A is intended for an indoor electrical control panel, not outdoor direct exposure.

Design targets:

- operating ventilation strategy compatible with ESP32/W5500 and buck-converter heat;
- no fan;
- avoid ventilation directly above possible mains contact copper;
- connector labels remain readable after installation;
- board removable without desoldering field connectors;
- enclosure opens with ordinary service tools;
- USB/RJ45 connectors withstand normal cable insertion without flexing the PCB excessively.

No IP rating is claimed until a production enclosure is independently tested.

## 7. Thermal strategy

- switching regulators placed away from ESP32 antenna and relay contact zone;
- base/lid may include protected side vents over logic/power region only if thermal test shows they are useful;
- target no component operation outside manufacturer temperature rating at maximum declared input/load and representative panel ambient;
- first prototype must be thermally checked with all four relays energized, HMI output loaded, Ethernet active and Wi-Fi active.

## 8. Safety separation geometry

- casing must preserve the PCB high-voltage/SELV boundary instead of bridging it with conductive hardware;
- mounting screws/standoffs must not reduce required electrical clearance;
- no metal DIN clip directly underneath exposed high-voltage/contact copper without adequate board/enclosure spacing;
- an internal insulating rib may track the relay/logic PCB separation line;
- production label must state contact ratings actually validated for the complete product, not merely the relay manufacturer's maximum component rating.

## 9. Prototype mechanical acceptance

Mechanical H3 cannot pass until:

1. KiCad PCB outline and mounting holes are frozen.
2. Connector 3D models/actual dimensions are checked.
3. STEP export of populated PCB fits the enclosure CAD without collision.
4. 3D-printed or machined prototype accepts the real assembled board.
5. All field plugs can be inserted/removed with enclosure fitted.
6. RJ45 and USB-C cables fit without interference.
7. DIN clip holds the populated enclosure securely.
8. Relay/contact wiring cannot enter the SELV cable area unintentionally.

## 10. Source of truth rule

`hardware/mechanical/Automatrix_PVDG_RevA_enclosure.scad` is the parametric prototype source. Once H2 PCB placement freezes connector coordinates, those coordinates are copied from KiCad/STEP into the SCAD parameters and the mechanical revision is incremented. No enclosure cutout should be finalized from visual guessing.
