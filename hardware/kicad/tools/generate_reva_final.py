#!/usr/bin/env python3
"""Rev-A production generator wrapper.

This wrapper keeps two concepts separate:
1. manifest desired electrical nets are in physical pad-number order;
2. the project-local rectangular symbol geometry requires a reversed-per-side
   connection order before KiCad upgrade so the exported physical pins end up
   with the desired nets.

The exported netlist is independently audited in CI; neither mapping trusts the
other.
"""
from pathlib import Path
import generate_reva_schematic as g

ROOT = Path(__file__).resolve().parents[1]


def comp(ref):
    return next(c for c in g.COMPS if c["ref"] == ref)


def desired_pin_net_map(c):
    pins = g.DEFS[c["sym"]]["pins"]
    if len(pins) != len(c["nets"]):
        raise ValueError(f"{c['ref']}: physical pin/net length mismatch")
    return {str(pin[0]): net for pin, net in zip(pins, c["nets"])}


def set_desired_pin(ref, pin, net):
    c = comp(ref)
    pins = g.DEFS[c["sym"]]["pins"]
    for idx, p in enumerate(pins):
        if str(p[0]) == str(pin):
            c["nets"][idx] = net
            return
    raise ValueError(f"{ref}: physical pin {pin} not present")


def require(ref, pin, net):
    actual = desired_pin_net_map(comp(ref)).get(str(pin))
    if actual != net:
        raise ValueError(f"{ref} desired physical pin {pin}: expected {net!r}, got {actual!r}")


# ---------------------------------------------------------------------------
# Complete the USB-C symbol using the actual GCT USB4105 16-contact footprint
# pad names. SBU pads are explicitly unused; shell is chassis/shield.
# ---------------------------------------------------------------------------
g.DEFS["USB_C"]["pins"] = [
    ["A1", "GND_A1"], ["A4", "VBUS_A4"], ["A5", "CC1"], ["A6", "D+_A6"],
    ["A7", "D-_A7"], ["A8", "SBU1"], ["A9", "VBUS_A9"], ["A12", "GND_A12"],
    ["B1", "GND_B1"], ["B4", "VBUS_B4"], ["B5", "CC2"], ["B6", "D+_B6"],
    ["B7", "D-_B7"], ["B8", "SBU2"], ["B9", "VBUS_B9"], ["B12", "GND_B12"],
    ["SH", "SHIELD"],
]
comp("J_USB")["nets"] = [
    "GND", "USB_5V", "USB_CC1", "USB_D+", "USB_D-", None, "USB_5V", "GND",
    "GND", "USB_5V", "USB_CC2", "USB_D+", "USB_D-", None, "USB_5V", "GND",
    "CHASSIS",
]

# ESP32-S3 USB guidelines recommend reserving 22/33-ohm series resistors and
# shunt-cap footprints close to the MCU. Split the MCU-side nets so the series
# parts are electrically real rather than decorative footprints. Capacitors are
# DNP by default and can be tuned only if prototype SI/EMC testing requires it.
set_desired_pin("U1", "13", "USB_D-_MCU")
set_desired_pin("U1", "14", "USB_D+_MCU")
if not any(c["ref"] == "R_MCU_DM_SER" for c in g.COMPS):
    g.COMPS.extend([
        {"ref":"R_MCU_DM_SER","sym":"RES","value":"27R","footprint":"Resistor_SMD:R_0603_1608Metric","datasheet":"","dnp":False,"x":145,"y":135,"nets":["USB_D-_MCU","USB_D-"]},
        {"ref":"R_MCU_DP_SER","sym":"RES","value":"27R","footprint":"Resistor_SMD:R_0603_1608Metric","datasheet":"","dnp":False,"x":145,"y":145,"nets":["USB_D+_MCU","USB_D+"]},
        {"ref":"C_MCU_DM_USB","sym":"CAP","value":"22pF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":160,"y":135,"nets":["USB_D-_MCU","GND"]},
        {"ref":"C_MCU_DP_USB","sym":"CAP","value":"22pF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":160,"y":145,"nets":["USB_D+_MCU","GND"]},
    ])

# W5500 MagJack uses LINK/ACT LEDs only. Speed and duplex indicator pins are
# deliberately no-connect in base Rev-A instead of creating isolated nets.
u2 = comp("U2")
u2["nets"][23] = None  # physical pin 24 SPDLED
u2["nets"][25] = None  # physical pin 26 DUPLED

# ---------------------------------------------------------------------------
# Complete optional RS232 HMI provision. DNP in base BOM.
# MAX3232 charge-pump pins may not float merely because the option is DNP.
# ---------------------------------------------------------------------------
u7 = comp("U7")
u7["nets"] = [
    "RS232_C1P", "RS232_VPLUS", "RS232_C1M", "RS232_C2P",
    "RS232_C2M", "RS232_VMINUS", None, None, None, None,
    "HMI_TX", "HMI_RX", "HMI_RS232_RX", "HMI_RS232_TX", "GND", "3V3",
]

if not any(c["ref"] == "J_RS232" for c in g.COMPS):
    g.COMPS.extend([
        {"ref":"C_RS2321","sym":"CAP","value":"0.1uF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":345,"y":195,"nets":["RS232_C1P","RS232_C1M"]},
        {"ref":"C_RS2322","sym":"CAP","value":"0.1uF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":345,"y":205,"nets":["RS232_C2P","RS232_C2M"]},
        {"ref":"C_RS2323","sym":"CAP","value":"0.1uF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":345,"y":215,"nets":["RS232_VPLUS","GND"]},
        {"ref":"C_RS2324","sym":"CAP","value":"0.1uF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":345,"y":225,"nets":["RS232_VMINUS","GND"]},
        {"ref":"C_RS2325","sym":"CAP","value":"0.1uF DNP","footprint":"Capacitor_SMD:C_0603_1608Metric","datasheet":"","dnp":True,"x":345,"y":235,"nets":["3V3","GND"]},
        {"ref":"J_RS232","sym":"CONN3","value":"RS232 GND/TX/RX DNP","footprint":"Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical","datasheet":"","dnp":True,"x":365,"y":215,"nets":["GND","HMI_RS232_TX","HMI_RS232_RX"]},
    ])


# Desired physical-pad assertions. This is intentionally independent from the
# connection-order compensation used by g.connections().
def validate_desired_pinout():
    expected_u1 = {
        "1":"GND","2":"3V3","3":"ESP_EN","4":"RELAY1_CTL","5":"RELAY2_CTL",
        "6":"RELAY3_CTL","7":"RELAY4_CTL","8":"HMI_TX","9":"RS485B_DE",
        "10":"RS485B_TX","11":"RS485B_RX","12":"ETH_RST","13":"USB_D-_MCU","14":"USB_D+_MCU",
        "17":"ETH_INT","18":"ETH_CS","19":"ETH_MOSI","20":"ETH_SCLK","21":"ETH_MISO",
        "22":"HMI_RX","24":"DI3_LOGIC","25":"DI4_LOGIC","27":"ESP_BOOT",
        "28":"STATUS_LED_CTL","29":"SD_CS","30":"SD_MISO","31":"RTC_SDA","32":"RTC_SCL",
        "33":"SD_SCLK","34":"SD_MOSI","35":"RS485A_DE","36":"RS485A_RX","37":"RS485A_TX",
        "38":"DI2_LOGIC","39":"DI1_LOGIC","40":"GND","41":"GND",
    }
    for pin, net in expected_u1.items(): require("U1", pin, net)

    expected_u2 = {
        "1":"ETH_TXN","2":"ETH_TXP","3":"GND","4":"3V3","5":"ETH_RXN","6":"ETH_RXP",
        "8":"3V3","9":"GND","10":"ETH_EXRES","11":"3V3","14":"GND","15":"3V3",
        "16":"GND","17":"3V3","19":"GND","20":"ETH_TOCAP","21":"3V3","22":"ETH_1V2",
        "23":"GND","25":"ETH_LED_LINK","27":"ETH_LED_ACT","28":"3V3","29":"GND",
        "30":"ETH_XI","31":"ETH_XO","32":"ETH_CS","33":"ETH_SCLK","34":"ETH_MISO",
        "35":"ETH_MOSI","36":"ETH_INT","37":"ETH_RST","43":"ETH_PMODE2",
        "44":"ETH_PMODE1","45":"ETH_PMODE0","48":"GND",
    }
    for pin, net in expected_u2.items(): require("U2", pin, net)
    if desired_pin_net_map(u2)["24"] is not None or desired_pin_net_map(u2)["26"] is not None:
        raise ValueError("W5500 SPDLED/DUPLED must remain NC in base Rev-A")

    for ref, pfx in (("U3","RS485A"),("U4","RS485B")):
        for pin, net in {"1":f"{pfx}_RX","2":f"{pfx}_DE","3":f"{pfx}_DE","4":f"{pfx}_TX","5":"GND","6":f"{pfx}_A","7":f"{pfx}_B","8":"3V3"}.items():
            require(ref,pin,net)

    for pin, net in {"1":"5V_HMI","2":"GND","3":"HMI_TX_OUT","4":"HMI_RX_IN"}.items(): require("J_HMI",pin,net)
    for pin, net in {
        "A1":"GND","A4":"USB_5V","A5":"USB_CC1","A6":"USB_D+","A7":"USB_D-","A9":"USB_5V","A12":"GND",
        "B1":"GND","B4":"USB_5V","B5":"USB_CC2","B6":"USB_D+","B7":"USB_D-","B9":"USB_5V","B12":"GND","SH":"CHASSIS",
    }.items(): require("J_USB",pin,net)

    for n in range(1,5):
        for pin, net in {"A1":"5V_FIELD","A2":f"RELAY{n}_COIL","11":f"RLY{n}_COM","14":f"RLY{n}_NO","12":f"RLY{n}_NC"}.items():
            require(f"K{n}",pin,net)
        for pin, net in {"1":f"RELAY{n}_GATE","2":"GND","3":f"RELAY{n}_COIL"}.items():
            require(f"Q{n}",pin,net)

    for pin, net in {"1":"RS232_C1P","2":"RS232_VPLUS","3":"RS232_C1M","4":"RS232_C2P","5":"RS232_C2M","6":"RS232_VMINUS","11":"HMI_TX","12":"HMI_RX","13":"HMI_RS232_RX","14":"HMI_RS232_TX","15":"GND","16":"3V3"}.items():
        require("U7",pin,net)
    for pin, net in {"1":"GND","2":"HMI_RS232_TX","3":"HMI_RS232_RX"}.items(): require("J_RS232",pin,net)

    print("desired physical pin manifest: PASS")


g.validate_critical_pinout = validate_desired_pinout


def write_fp_lib_table():
    libs = sorted({c["footprint"].split(":",1)[0] for c in g.COMPS if ":" in c["footprint"] and not c["footprint"].startswith("Automatrix:")})
    lines = ["(fp_lib_table", "  (version 7)"]
    for lib in libs:
        lines.append(f'  (lib (name "{lib}")(type "KiCad")(uri "${{KICAD10_FOOTPRINT_DIR}}/{lib}.pretty")(options "")(descr ""))')
    lines.append(")")
    (ROOT / "fp-lib-table").write_text("\n".join(lines)+"\n", encoding="utf-8")


def main():
    g.main()
    write_fp_lib_table()
    print("Rev-A final generator additions: USB-C physical pads, ESP32 USB SI parts, RS232 option, project footprint table")


if __name__ == "__main__":
    main()
