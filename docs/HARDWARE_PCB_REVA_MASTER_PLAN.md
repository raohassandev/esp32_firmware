# Automatrix ESP32 PV-DG Controller — Hardware / PCB Rev-A Master Plan

**Status:** Canonical working hardware plan  
**Revision:** Rev-A Plan v0.1  
**Date:** 2026-08-16  
**Repository:** `raohassandev/esp32_firmware`  
**Hardware branch:** `hardware/pcb-reva-plan`  
**Firmware baseline:** `feature/multibrand-inverter-profiles`

> This file is the canonical hardware planning document for the single-PCB controller. Update this document whenever a hardware requirement, interface, connector, GPIO, protection method, enclosure constraint, BOM target, or validation decision changes. Do not silently change production hardware outside this plan.

---

## 1. Product objective

Replace the experimental setup consisting of an ESP32 development board, PC/USB power, external ZLAN RS485-to-Ethernet converter, Wi-Fi router path and loose wiring with one low-cost industrial controller PCB.

The board must preserve the existing firmware's Modbus TCP capability while adding direct Modbus RTU hardware so the ZLAN converter is no longer required for local RS485 devices.

Primary priorities:

1. Low manufacturing cost.
2. Reliable industrial panel operation.
3. Two independent RS485 buses.
4. Wired Ethernet as a first-class network interface.
5. Serial touch-display connection.
6. Four physical relay outputs.
7. Optional digital inputs, RTC and microSD without requiring a different PCB.
8. USB programming/diagnostics retained.
9. DIN-rail friendly enclosure and connector arrangement.
10. Hardware designed for DFM, field service and future firmware expansion.

---

## 2. Scope freeze for Rev-A

### 2.1 Mandatory populated functions

| Block | Rev-A requirement |
|---|---|
| MCU | ESP32-S3-WROOM-1-N8, no PSRAM required by default |
| Wi-Fi / BLE | Provided by ESP32-S3 module; Wi-Fi retained for commissioning/fallback |
| Ethernet | 1 x 10/100 Ethernet RJ45 using W5500 or approved equivalent SPI Ethernet controller |
| RS485 | 2 x independent half-duplex RS485 ports, each protected and separately controlled |
| Relay outputs | 4 x electromechanical dry-contact relay outputs |
| HMI / touch display | 1 x serial UART display port with power and protected logic interface |
| USB | USB-C for firmware flashing, diagnostics and bench logic power |
| Field power | 12/24 VDC nominal industrial input |
| Status indication | Power, system/status and relay indication; Ethernet link/activity through RJ45 where supported |
| Service controls | BOOT and RESET buttons or service-access equivalents |

### 2.2 Optional functions planned on the same PCB

These must have PCB footprints and routing provisions but may be DNP (Do Not Populate) in the low-cost BOM.

| Block | Planned Rev-A provision |
|---|---|
| Digital inputs | 4 x optically isolated 12/24 VDC inputs |
| RTC | I2C real-time clock + 32.768 kHz crystal + coin-cell backup provision |
| microSD | 1 x microSD socket, SPI mode |
| HMI RS232 | Optional MAX3232-class transceiver footprint / selection path if an RS232 HMI variant is required |
| RS485 isolation | Quote as an alternate option only; base Rev-A remains non-isolated to control cost |

### 2.3 Explicitly outside base Rev-A

- PoE input.
- Cellular modem.
- CAN transceiver.
- Analog inputs/outputs.
- On-board touch LCD.
- High-current motor outputs.
- Custom injection mould tooling before PCB/field validation.

---

## 3. System architecture

```text
                        12/24 VDC FIELD INPUT
                                 |
                    Fuse/PTC + reverse protection
                    TVS + filtering + DC/DC power
                                 |
                 +---------------+---------------+
                 |                               |
              5V FIELD                        3.3V LOGIC
          relays / HMI aux             ESP32 / W5500 / IO
                 |                               |
                 |                    +----------+----------+
                 |                    |    ESP32-S3-N8      |
                 |                    +--+---+---+---+---+---+
                 |                       |   |   |   |   |
                 |                       |   |   |   |   +-- Optional I2C RTC
                 |                       |   |   |   +------ Optional microSD
                 |                       |   |   +---------- UART HMI
                 |                       |   +-------------- UART -> RS485 #2
                 |                       +------------------ UART -> RS485 #1
                 |
        +--------+---------+
        | 4 x relay coils  |
        +------------------+

ESP32 SPI ----------------------> W5500 ----------------> Magnetics RJ45
ESP32 native USB ---------------> USB-C
ESP32 Wi-Fi --------------------> Commissioning / fallback network
```

### Communication intent

- **Ethernet:** preferred wired network path for normal Modbus TCP operation.
- **Wi-Fi:** retained for commissioning, fallback access and development; it must not be the only production communication path.
- **RS485 #1 / #2:** direct Modbus RTU buses for meters, generators, inverter/logger interfaces or other RS485 equipment.
- **HMI UART:** independent serial channel; must not share a transceiver or connector with either RS485 bus.

---

## 4. Electrical requirements

### 4.1 Main input power

**Target input:**

- Nominal: 12 VDC or 24 VDC.
- Design operating target: approximately 9–30 VDC.
- Reverse-polarity protection required.
- Input fuse or resettable PTC required.
- TVS surge suppression required.
- Bulk and high-frequency input filtering required.
- Input connector: removable/pluggable screw terminal preferred.

**Power architecture target:**

- Field DC/DC should have comfortable headroom above 24 V; 36–60 V rated regulator family preferred.
- 5 V field rail target: **minimum 3 A design capability** so relay coils and a serial HMI supply can coexist.
- 3.3 V logic rail target: **minimum 1.5 A design capability** for ESP32-S3, W5500 and optional peripherals.
- Prefer switching conversion for 3.3 V if thermal loss from a 5 V LDO would be excessive.
- Place rail test points for VIN, 5V_FIELD and 3V3.

### 4.2 USB-C power behavior

USB-C is mandatory for flashing and diagnostics.

Recommended behavior:

- ESP32 logic must be able to boot from USB during bench development without requiring the 12/24 V field supply.
- USB power and field-derived 5 V must not back-feed each other.
- Relay coils and the external HMI 5 V output should preferably require field power, so a laptop USB port is not expected to energize field loads.
- Use native ESP32-S3 USB D-/D+ on GPIO19/GPIO20.
- Add USB ESD protection and correct USB-C CC resistors.

### 4.3 ESP32 module

Base selection: **ESP32-S3-WROOM-1-N8**.

Reasons:

- Existing firmware targets ESP32-S3.
- Current partition map fits within 8 MB; firmware configuration will be aligned to the actual module before hardware release.
- Native USB allows removal of a separate CP2102/CH340 class USB-UART bridge.
- Three UART controllers allow two RS485 buses plus one independent serial HMI.
- Wi-Fi remains available without an external radio.

Hardware rules:

- Follow Espressif module land pattern and antenna keep-out exactly.
- Place module at a board edge with antenna facing the non-metallic enclosure wall.
- No copper, ground plane, metal fastener or high-current relay trace in the antenna keep-out.
- Keep strapping pins free from unsafe external biasing.
- Keep GPIO19/20 dedicated to native USB.

---

## 5. Ethernet interface

### 5.1 Base implementation

- Controller: W5500 or approved equivalent compatible with ESP-IDF integration plan.
- 10Base-T / 100Base-TX.
- RJ45 with integrated magnetics preferred to reduce assembly count.
- Link/activity LEDs preferred in the RJ45.
- Dedicated RESET and INT lines to ESP32.
- Ethernet ESD protection at the connector.
- Correct PHY analog filtering and reference components per W5500 reference design.
- Route Ethernet differential pairs as a matched pair and keep switching/relay noise away from this region.

### 5.2 Not required

- PoE is not required in Rev-A.
- Ethernet switch functionality is not required.

---

## 6. RS485 interfaces — mandatory 2 ports

Each port is electrically independent and must have its own UART/DE control.

### 6.1 Electrical interface per port

- Half-duplex RS485/RS422-compatible transceiver.
- 3.3 V logic compatible.
- Industrial-temperature transceiver preferred.
- Base design non-isolated for cost.
- TVS protection at the field connector.
- 120 ohm termination selectable by jumper / DIP / 0-ohm option; not permanently fitted to the bus.
- Bias resistors provided as selectable/DNP footprints.
- Default DE/RE hardware state must keep the transceiver non-driving during ESP32 reset/boot.
- Connector: 3-pin pluggable terminal labelled `A`, `B`, `GND`.
- Add test points for TX, RX, DE, A and B.

### 6.2 Suggested transceiver class

- THVD14xx class for robust industrial builds, or a lower-cost MAX3485-compatible qualified part for the value BOM.
- Final MPN must be approved after price, availability and bench testing.

### 6.3 Firmware intent

Add a `modbus_rtu` transport without removing the existing `modbus_tcp` transport. Device configuration must be able to select TCP or RTU and the required RTU port/baud/parity/stop-bit settings.

---

## 7. Relay outputs — mandatory 4 channels

### 7.1 Output type

- **4 electromechanical dry-contact relays.**
- Preferred contact format: **SPDT / Form-C**, providing `NC`, `COM`, `NO` for each channel.
- Minimum preferred contact target for quotation: **5 A at 250 VAC resistive and 5 A at 30 VDC resistive**, or higher if the relay footprint/cost remains reasonable.
- Quote a 10 A relay alternate if available at small additional cost.
- Relays are intended for interlocks, contactor coils, alarms and general control; high-inrush loads must be separately reviewed.

### 7.2 Coil drive

- 5 V coil preferred for BOM availability.
- Low-side transistor array / MOSFET driver.
- Flyback protection on every coil.
- One indication LED per relay.
- Hardware and firmware default state must be **relay OFF** during reset, boot, brownout and firmware failure.
- Driver input pulldowns required so relays cannot momentarily energize during boot.

### 7.3 Contact-side safety/layout

- Relay contact copper and terminals must be physically separated from SELV logic/communication copper.
- Use generous creepage/clearance and isolation slots where practical.
- No routing of Ethernet, USB, ESP32 antenna, RS485 logic or HMI logic through the relay contact zone.
- If the board is claimed for mains switching, final relay, terminal, track spacing, creepage and clearance must be verified by the PCB engineer against the declared end-product safety category and applicable standards before production.
- Use appropriately rated pluggable screw terminals.

### 7.4 Relay connector count

Four relays x three contact points = **12 field terminals**:

`R1-NC, R1-COM, R1-NO ... R4-NC, R4-COM, R4-NO`.

---

## 8. Serial touch-display interface — mandatory

The display model is not yet hardware-frozen, therefore Rev-A must avoid locking the board to only one vendor where possible.

### 8.1 Base TTL UART port

- Dedicated ESP32 UART, independent from both RS485 ports.
- Signals: `+5V_HMI`, `GND`, `TX`, `RX`.
- Optional fifth pin for shield/NC/service signal.
- 5 V HMI auxiliary output target: up to **1 A**, field-power dependent and protected by fuse/PTC/current limiting.
- ESP32 input must be protected from a 5 V HMI TX signal; use a proper level-shifting or approved divider/buffer solution.
- ESP32 TX to HMI must meet the selected display's VIH requirement.
- Add ESD protection if the cable exits the enclosure.
- Use a locking or pluggable connector suitable for panel wiring.

### 8.2 Optional RS232 variant

Provide an optional MAX3232-class footprint or selectable routing path so the same PCB can support an RS232 touch HMI variant without redesign. DNP this section in the base TTL BOM.

---

## 9. Optional 4 x digital inputs

### 9.1 Electrical target

- Quantity: 4.
- Intended field levels: 12/24 VDC.
- Optical isolation preferred.
- Reverse polarity / negative transient protection.
- Input RC filtering for industrial noise.
- Per-channel indication LED preferred.
- Common terminal arrangement suitable for field wiring.
- Input circuitry and terminal block may be DNP in Lite BOM while retaining the PCB footprint.

### 9.2 Firmware behavior

When populated, firmware must expose debounced logical DI states to control logic and diagnostics. Inputs must default to inactive if the optional hardware is not present.

---

## 10. Optional RTC

- I2C RTC footprint.
- PCF8563-class low-cost RTC or approved equivalent.
- 32.768 kHz crystal if required by selected device.
- Coin-cell backup provision.
- Battery holder must be accessible after opening the enclosure.
- RTC interrupt/alarm line is optional but reserve a GPIO if layout permits.
- RTC section may be DNP in the Lite BOM.

---

## 11. Optional microSD

- microSD socket.
- SPI mode preferred for low pin count and simple firmware.
- Use a separate SPI controller/bus from Ethernet where practical; otherwise use independent chip-select and verify bus coexistence.
- ESD protection at socket if externally accessible.
- Card-detect optional.
- microSD section may be DNP in Lite BOM.

---

## 12. Preliminary GPIO allocation

This table is a **schematic-start allocation**, not an immutable final pinout. The PCB engineer may move non-special pins after ERC/layout review, but native USB, boot/strapping constraints and documented interface ownership must be preserved.

| Function | ESP32-S3 GPIO | Notes |
|---|---:|---|
| USB D- | 19 | Native USB dedicated |
| USB D+ | 20 | Native USB dedicated |
| RS485-1 TX | 43 | UART0 TX; DE keeps bus silent during boot logs |
| RS485-1 RX | 44 | UART0 RX |
| RS485-1 DE/RE | 42 | Pulldown = receive/non-driving default |
| RS485-2 TX | 17 | UART1 |
| RS485-2 RX | 18 | UART1 |
| RS485-2 DE/RE | 16 | Pulldown = receive/non-driving default |
| HMI TX | 15 | UART2 |
| HMI RX | 14 | UART2 |
| W5500 SCLK | 12 | SPI2 candidate |
| W5500 MOSI | 11 | SPI2 candidate |
| W5500 MISO | 13 | SPI2 candidate |
| W5500 CS | 10 | Dedicated chip select |
| W5500 INT | 9 | Interrupt |
| W5500 RESET | 8 | Hardware reset |
| Relay 1 | 4 | Driver input with pulldown |
| Relay 2 | 5 | Driver input with pulldown |
| Relay 3 | 6 | Driver input with pulldown |
| Relay 4 | 7 | Driver input with pulldown |
| DI1 | 1 | Optional |
| DI2 | 2 | Optional |
| DI3 | 47 | Optional |
| DI4 | 48 | Optional |
| RTC SDA | 38 | Optional I2C |
| RTC SCL | 39 | Optional I2C |
| SD SCLK | 40 | Optional SPI3 candidate |
| SD MOSI | 41 | Optional SPI3 candidate |
| SD MISO | 37 | Optional; valid for N8/non-octal plan |
| SD CS | 36 | Optional; valid for N8/non-octal plan |
| Status LED | 35 | Move if layout requires |
| BOOT | 0 | Button only; strapping pin |

**Reserved / caution pins:** GPIO0, GPIO3, GPIO45 and GPIO46 are strapping pins. GPIO19/20 are reserved for USB. GPIO26–32 are not to be used. GPIO33–37 must be rechecked if the module variant ever changes to octal flash/PSRAM.

---

## 13. PCB construction and layout requirements

### 13.1 Layer count

**Preferred production/prototype:** 4-layer PCB.

Suggested stack intent:

1. Top: components + critical signals.
2. Inner 1: continuous GND plane.
3. Inner 2: power + slower signals as allowed.
4. Bottom: remaining signals / ground stitching.

Reason: ESP32 RF, W5500 Ethernet, switching regulators and relay noise are much easier to control with a continuous ground reference.

**Cost-down alternate:** ask the manufacturer to quote a 2-layer version, but do not release a 2-layer production board until Ethernet/RF/EMC performance is proven.

### 13.2 Board envelope

Initial target only: **PCB <= 145 mm x 95 mm** if practical.

Final dimensions depend on:

- 12 relay contact terminals.
- Two RS485 terminals.
- Power input.
- Optional DI terminals.
- RJ45 and USB access.
- DIN-rail enclosure selection.

Prefer all pluggable terminals at board edges.

### 13.3 Functional zoning

Physically divide the PCB into:

1. Relay contact / potentially high-voltage field zone.
2. 12/24 V input and relay coil power zone.
3. ESP32 / low-voltage digital zone.
4. Ethernet/RJ45 zone.
5. RS485 and HMI field-communication zone.
6. Optional RTC/SD/DI zone.

Keep the ESP32 antenna as far as practical from relays, RJ45 magnetics, switching inductors and metal enclosure features.

### 13.4 Protection

Minimum protection provisions:

- Power input TVS + reverse polarity + fuse/PTC.
- USB ESD.
- Ethernet connector ESD / reference-design protection.
- RS485 TVS per port.
- HMI cable ESD if external.
- Relay coil flyback.
- DI reverse/transient protection if populated.

### 13.5 DFM

- Prefer JLCPCB/LCSC basic/extended parts where reasonable, without sacrificing field reliability.
- Avoid unnecessary BGA/QFN parts when an assembly-friendly alternative is available.
- Prefer 0603/0805 passives for serviceability unless density forces smaller parts.
- Keep component variants controlled in the BOM.
- All terminal footprints must match actual procurement MPNs, not generic drawings.
- Provide accurate 3D models for enclosure interference checking.

---

## 14. Connector plan

| Connector | Minimum pins | Suggested labelling |
|---|---:|---|
| Power | 2 | `VIN+`, `VIN-` |
| RS485 #1 | 3 | `A1`, `B1`, `GND` |
| RS485 #2 | 3 | `A2`, `B2`, `GND` |
| HMI | 4 or 5 | `+5V`, `GND`, `TX`, `RX`, optional `SH/NC` |
| Relay 1 | 3 | `NC1`, `COM1`, `NO1` |
| Relay 2 | 3 | `NC2`, `COM2`, `NO2` |
| Relay 3 | 3 | `NC3`, `COM3`, `NO3` |
| Relay 4 | 3 | `NC4`, `COM4`, `NO4` |
| Optional DI | 5 | `DI1..DI4`, `COM` |
| Ethernet | RJ45 | `ETHERNET` |
| USB | USB-C | `USB / SERVICE` |
| Optional SD | microSD | externally or internally accessible per enclosure decision |

Pluggable terminal pitch should be selected after current/voltage requirements and enclosure wall thickness are confirmed. Relay terminals must be separately rated for the declared switched voltage/current.

---

## 15. Factory test points and production test

Provide accessible test points or jig pads for:

- VIN.
- 5V_FIELD.
- 3V3.
- GND.
- EN/RESET.
- BOOT.
- USB D+/D- only if required for debugging.
- W5500 SPI CS/CLK/MOSI/MISO or dedicated test pads as practical.
- RS485-1 TX/RX/DE/A/B.
- RS485-2 TX/RX/DE/A/B.
- HMI TX/RX.
- Relay driver inputs.
- Optional DI logic outputs.
- I2C SDA/SCL.

Every assembled board should support a production test sequence:

1. Visual inspection and power-off short test.
2. Power from 24 V.
3. Verify 5 V and 3.3 V rails.
4. Verify USB enumeration and firmware flash.
5. Verify ESP32 boot and serial diagnostics.
6. Verify Ethernet link and TCP communication.
7. Verify RS485-1 transmit/receive loopback or golden-device Modbus test.
8. Verify RS485-2 transmit/receive loopback or golden-device Modbus test.
9. Toggle all four relays and verify contacts.
10. Verify HMI UART TX/RX.
11. If fitted, verify all DIs.
12. If fitted, verify RTC read/write/backup.
13. If fitted, verify microSD read/write.
14. Store board serial number / test result.

---

## 16. Firmware work required for Rev-A hardware

Hardware release and firmware release are related but must remain independently testable.

### 16.1 Ethernet

- Add W5500 Ethernet network initialization.
- Integrate Ethernet into ESP-IDF network stack.
- Prefer Ethernet for production traffic when link is available.
- Retain Wi-Fi commissioning/fallback path.
- Existing socket-based Modbus TCP client should continue to operate through the selected IP interface.

### 16.2 Direct Modbus RTU

- Add `modbus_rtu` transport.
- Two selectable RS485 ports.
- Config fields: transport, RS485 port, baud, parity, stop bits, timeout/retry settings.
- Do not remove or break existing Modbus TCP transport.

### 16.3 Relay manager

- Four relay channels.
- Safe OFF default at boot.
- Explicit ownership by control/safety logic.
- Manual diagnostic test mode.
- Fail-safe reset behavior.

### 16.4 HMI UART

- Dedicated UART driver/service.
- Display protocol to be finalized after display model selection.
- Hardware abstraction must allow the controller to operate without the HMI attached.

### 16.5 Optional features

- DI service only enabled when configured/populated.
- RTC service must fall back to network/system time if RTC absent.
- SD logging service must be optional and fail safely if no card is present.

---

## 17. Hardware validation gates

### Gate H0 — requirements freeze

- Mandatory/optional feature list approved.
- Relay contact requirements approved.
- HMI electrical interface approved.
- Input voltage range approved.
- Enclosure concept approved.

### Gate H1 — schematic freeze

- Full schematic reviewed.
- GPIO map confirmed against module variant.
- Power budget calculated.
- Relay safety spacing reviewed.
- Ethernet reference circuit reviewed.
- ERC clean or documented exceptions only.

### Gate H2 — PCB freeze

- DRC clean.
- 3D enclosure fit checked.
- Antenna keep-out confirmed.
- Ethernet routing checked.
- High-voltage/SELV zoning checked.
- Test-point access confirmed.
- Gerber/BOM/CPL cross-check completed.

### Gate H3 — prototype bring-up

Build 5–10 Rev-A prototypes.

Required evidence:

- Power/thermal measurements.
- USB flashing.
- Ethernet Modbus TCP.
- Both RS485 ports under simultaneous traffic.
- HMI UART.
- Four relay operations/contact verification.
- 24-hour soak test.
- Optional functions if populated.

### Gate H4 — pre-production

- Correct all Rev-A findings.
- Basic ESD/EMI pre-compliance checks.
- Field test in a real electrical panel.
- Freeze Rev-B or production Rev-A1.
- Lock approved BOM alternates.
- Finalize production enclosure/tooling.

---

## 18. BOM variants

The PCB remains common; population changes create variants.

### Variant L — Core / low-cost

Populated:

- ESP32-S3-N8.
- Ethernet.
- 2 x RS485.
- 4 x relays.
- HMI TTL UART.
- USB-C.
- 12/24 V power.

DNP:

- DI section.
- RTC.
- microSD.
- optional RS232 transceiver.

### Variant S — Standard

Core + 4 isolated digital inputs.

### Variant F — Full

Standard + RTC + microSD + optional HMI interface option as required.

### Cost target

Previous planning indicated the communication-only core could be kept near the low-teens USD component range at 100-piece scale. Four relays and their terminals/drivers will increase that target. Use **approximately USD 12–15 / PKR 3.4k–4.2k component BOM as a design target for the mandatory populated board at 100-piece scale**, excluding PCB fabrication, assembly, shipping, tax, enclosure and design/NRE. This is a planning target, not a supplier quote.

Full optional population should be costed separately so optional features do not block the low-cost product.

---

## 19. Enclosure / casing master plan

### 19.1 Product form

Preferred enclosure style: **DIN-rail industrial controller enclosure** for 35 mm DIN rail.

Two development paths must be quoted:

1. **Low-NRE path:** off-the-shelf DIN-rail PC/ABS enclosure with custom cut-outs/labels.
2. **Custom path:** custom 3D enclosure followed by injection-mould tooling only after Rev-A/Rev-B PCB dimensions are stable.

Prototype casing should be 3D printed or CNC/modified stock enclosure; do not purchase injection mould tooling before PCB and connector locations are proven.

### 19.2 Initial mechanical envelope

Target overall enclosure envelope: approximately **160 mm x 110 mm x 50 mm maximum**, subject to PCB placement study.

Target PCB: <=145 mm x 95 mm if practical.

These dimensions are targets, not frozen drawings.

### 19.3 Enclosure requirements

- DIN-rail 35 mm mounting.
- Flame-retardant PC/ABS preferred for production.
- Four PCB mounting points or equivalent secure retention.
- Service access to USB-C and BOOT/RESET without removing field wiring.
- RJ45 accessible from outside.
- Both RS485 terminals externally accessible.
- HMI serial connector externally accessible.
- Relay terminal groups clearly separated and labelled.
- Power input clearly separated and labelled.
- Optional DI terminals accessible when populated.
- microSD location accessible without exposing mains relay contacts if possible.
- LED light pipes/windows for power/status/relay indication if LEDs are not directly visible.
- Ventilation near regulator/relay area if thermal testing requires it.
- No metal or internal obstruction in the ESP32 antenna keep-out region.
- Cable entry/terminal geometry must permit normal panel wiring and screwdriver access.
- Relay contact area should have physical/finger-safety consideration if mains switching is allowed.
- Space for product label, QR code, serial number, model, input rating and terminal legend.

### 19.4 Suggested connector zoning on enclosure

**Communication side/front:**

- Ethernet RJ45.
- USB-C service.
- RS485-1.
- RS485-2.
- HMI serial.
- status indicators.

**Field output side:**

- Relay 1–4 NC/COM/NO terminals.
- 12/24 V input.
- optional DI terminals.

This reduces accidental mixing of Ethernet/USB wiring with switched relay wiring.

### 19.5 Mechanical deliverables

- STEP model of enclosure assembly.
- STEP model of PCB installed in enclosure.
- STL for prototype printing.
- 2D dimensioned drawing/PDF.
- DXF for panel/cut-outs if required.
- Exploded assembly drawing.
- DIN clip design or selected off-the-shelf clip part number.
- Screw/insert/fastener BOM.
- Label/printing artwork and terminal legend.
- Injection mould DFM package only after tooling approval.

---

## 20. Change-control rule

Any of the following requires this file's revision and change log to be updated before hardware is considered frozen:

- Connector type or pinout.
- GPIO assignment.
- Relay count/type/contact rating.
- Input power range.
- RS485 isolation decision.
- Ethernet controller or RJ45 type.
- HMI electrical interface.
- Optional DI/RTC/SD population strategy.
- PCB dimensions/mounting holes.
- Enclosure dimensions/cut-outs.
- BOM substitutions affecting electrical behavior.

### Revision history

| Revision | Date | Change |
|---|---|---|
| v0.1 | 2026-08-16 | Initial canonical plan: 2 x RS485, Ethernet, 4 relay outputs, serial HMI, optional 4DI/RTC/SD, DIN-rail enclosure and firmware integration plan. |
