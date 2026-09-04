# Rev-A PCB Placement and Routing Rules

**Gate:** these rules are ready for H2 implementation, but board creation/routing must not be accepted until H1 schematic/exported-pin validation passes.

## 1. Board and stack

- Working outline: 145 mm x 95 mm maximum target.
- Preferred 4-layer FR-4, 1.6 mm, 1 oz copper.
- L1: components/signals/power hot loops.
- L2: continuous ground reference plane except deliberate safety isolation voids.
- L3: power distribution + low-speed signals.
- L4: low-speed signals + ground fill.
- No copper or components in ESP32 antenna keep-out on any copper layer except as explicitly permitted by Espressif module guidance.

## 2. Mechanical zoning

Coordinate convention: board origin at lower-left, X along 145 mm edge, Y along 95 mm edge.

Working zones:

- `RELAY_CONTACT`: Y=0..25 mm, full width; relay contact terminals on Y=0 edge.
- `RELAY_COIL_POWER`: Y=25..40 mm; relay bodies/coil drivers and field 5 V.
- `LOGIC`: Y=40..78 mm; ESP32 and low-voltage logic.
- `SELV_CONNECTORS`: Y=78..95 mm; power/RS485/HMI/DI terminals as practical.
- `NETWORK_END`: right-hand X edge for RJ45.
- `USB_SERVICE`: right/lower-safe edge adjacent logic only, never opening into relay-contact zone.

The actual relay/SELV creepage boundary overrides these planning coordinates.

## 3. Mandatory connector edge order

### Relay/contact long edge

From left to right:

1. Relay 1 NC / COM / NO
2. Relay 2 NC / COM / NO
3. Relay 3 NC / COM / NO
4. Relay 4 NC / COM / NO

Keep at least one terminal-body pitch of visual/mechanical separation between channels where envelope permits. Silkscreen must repeat `NC COM NO` at every channel.

### SELV/service edge

Preferred left-to-right grouping:

1. 12/24 VDC input `VIN+ VIN-`
2. RS485-A `A B GND`
3. RS485-B `A B GND`
4. HMI `+5V GND TX RX`
5. optional DI `DI1 DI2 DI3 DI4 COM`

### Network/service end

- RJ45 centered in its own edge opening.
- USB-C separated enough that both RJ45 and USB cables can be inserted simultaneously.
- BOOT/RESET accessible with enclosure open or through service holes.

## 4. Relay/contact routing

- Contact nets are treated as a separate potential-high-voltage domain even if a particular installation only switches low voltage.
- Minimum design target: 6.0 mm copper-to-SELV clearance; use larger clearance where board geometry permits.
- Add routed isolation slots between contact zone and SELV/coil/logic where they materially improve creepage.
- No ground plane beneath relay contact terminals/traces within the high-voltage boundary.
- No logic/power vias inside the relay contact region.
- Contact traces sized for the declared 5 A continuous resistive board target; use broad copper/pours and verify thermal rise instead of relying on thin default tracks.
- NC/COM/NO traces should be direct from relay contact pads to corresponding terminal pads.
- Relay body placement must respect its manufacturer mechanical drawing and pin identification.

## 5. Field power / TPS54360B

Placement order around U5:

1. input ceramic capacitors immediately at VIN/GND pins;
2. catch diode / switch-node network per TI reference;
3. inductor adjacent to SW node;
4. output capacitors adjacent to inductor/return;
5. feedback/compensation components quiet and close to FB/COMP;
6. exposed PowerPAD connected to ground copper with thermal via array.

Rules:

- minimize high-di/dt input and switch loops;
- keep SW copper compact and away from ESP32 antenna, Ethernet MDI and crystal;
- separate power ground return from sensitive analog/PHY areas until a low-impedance common ground region;
- no Ethernet or USB traces cross the buck hot-loop region.

## 6. 3.3 V AP63203

- input/output ceramics and inductor follow Diodes reference placement tightly;
- short switching loop;
- located between 5V_LOGIC_IN source and logic distribution, not beside ESP32 antenna;
- feed ESP32 and W5500 through broad 3V3 distribution with local bulk/decoupling.

## 7. ESP32-S3

- module antenna at PCB edge with antenna end facing outside board/enclosure interior where practical;
- manufacturer keep-out on all layers beneath/beyond antenna;
- no relay, MagJack, inductor, metal DIN clip, battery, microSD socket or large cable bundle inside antenna keep-out;
- EN/BOOT circuitry close to module pins;
- native USB D+/D- leave module as a matched differential pair toward USB-C without stubs;
- provide accessible EN/GPIO0/3V3/GND test pads.

## 8. W5500 / Ethernet

- W5500, 25 MHz crystal and MagJack form one compact region at network edge.
- crystal immediately adjacent XI/XO, short symmetric traces, grounded keep-clear around oscillator per reference guidance.
- EXRES1 12.4 kΩ, TOCAP 4.7 µF and 1V2O 10 nF at their pins.
- MDI TX/RX pairs routed as controlled differential pairs according to the selected fabricator stackup; no stubs and minimal layer transitions.
- pair spacing/width calculated from actual 4-layer stackup before fabrication.
- keep MDI pairs away from relay contacts and switching converters.
- connector-side ESD placed at RJ45 before long board routing.
- chassis/shield strategy remains separate from logic ground unless the final EMC network intentionally couples them.

## 9. USB-C

- use exact GCT USB4105 family footprint or approved mechanically identical part.
- duplicate A/B USB2 D+ pads connect together at the receptacle footprint per USB-C device implementation; likewise D-.
- VBUS and GND duplicated pads tied with low impedance.
- CC1/CC2 each have their own 5.1 kΩ Rd to ground.
- low-capacitance ESD adjacent connector.
- D+/D- 90 Ω differential target according to final stackup, no long stubs.
- shell/chassis pads use the controlled chassis strategy.

## 10. RS485 A/B

- ports physically distinct and labels unambiguous.
- transceiver close to field terminal; TVS closer to connector than transceiver.
- selectable termination resistor physically close across A/B near transceiver/connector end.
- DNP bias resistors grouped and labelled per port.
- A/B route as a balanced pair, equal environment, away from buck/relay switching nodes.
- ground terminal has a robust path to board ground in base non-isolated Rev-A.
- DE/RE pulldown located close to transceiver control pin.

## 11. HMI

- 5V_HMI field-power trace sized for 1 A declared auxiliary output.
- protect HMI 5 V branch separately with resettable fuse/load protection.
- HMI TX/RX logic routing away from relay/contact region.
- HMI RX divider/ESD close to connector/ESP input path.
- optional MAX3232 section DNP and placed so its charge-pump capacitors are compact; optional RS232 connector must not confuse TTL HMI connector labeling.

## 12. Optional DI

- field-side optocoupler LED/resistor/reverse-diode components grouped adjacent DI terminal.
- maintain isolation slot/clearance around optocoupler barrier; do not pour logic copper through intended isolation gap.
- logic-side pull-up/debounce components placed after optocoupler barrier.
- DNP entire DI block must not create dangling required core nets.

## 13. Optional RTC / SD

- RTC crystal extremely close to RTC oscillator pins; no fast signals under/through its oscillator loop.
- battery away from ESP antenna.
- microSD at enclosure-accessible edge only if customer-accessible variant is intended; otherwise internal service location.
- SD SPI traces short, grouped and series-resistor provision added if edge rates require it.

## 14. Net classes — starting values

Final widths are recalculated against the selected PCB stackup/copper and current/thermal targets.

- `DEFAULT_SIGNAL`: >=0.20 mm.
- `GPIO_UART_SPI`: >=0.20 mm.
- `3V3_POWER`: >=0.50 mm trunk / pours preferred.
- `5V_LOGIC`: >=0.75 mm trunk.
- `5V_HMI_1A`: >=1.0 mm or pour.
- `VIN_FIELD`: >=1.0 mm, increase according to input current and copper.
- `RELAY_COIL`: >=0.50 mm.
- `RELAY_CONTACT_5A`: broad copper/pour; target >=2.5 mm where geometry permits, verified by thermal/current calculation.
- `USB_DPDM`: controlled 90 Ω differential.
- `ETH_MDI`: controlled 100 Ω differential.
- `RS485_PAIR`: >=0.25 mm, coupled routing with practical field-bus spacing.

## 15. DRC and release gates

H2 cannot pass unless:

- exact schematic-to-board parity passes;
- zero unwaived DRC violations;
- footprint library links resolve;
- relay/contact clearances satisfy explicit HV/SELV rule set;
- ESP32 antenna keep-out is represented in PCB constraints/courtyard/keepout;
- differential pairs follow final fabricator stackup;
- all mandatory connectors lie on accessible board edges;
- all optional DNP footprints preserve mandatory operation when empty;
- 3D STEP inspection shows no connector/body collisions;
- board outline/mount holes are copied into enclosure source after freeze;
- Gerber/drill/BOM/CPL outputs can be generated reproducibly.
