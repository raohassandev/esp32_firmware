#!/usr/bin/env python3
"""Independently prove critical physical pin-to-net mappings from KiCad export."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
NETLIST = ROOT / "Automatrix_PVDG_RevA.net"
REF_MAP = json.loads((ROOT / "REFERENCE_MAP.json").read_text(encoding="utf-8"))
text = NETLIST.read_text(encoding="utf-8")
lines = text.splitlines()

pin_to_net = {}
i = 0
while i < len(lines):
    if re.match(r"\s*\(net\s*$", lines[i]):
        depth = lines[i].count("(") - lines[i].count(")")
        block = [lines[i]]; i += 1
        while i < len(lines) and depth > 0:
            block.append(lines[i]); depth += lines[i].count("(") - lines[i].count(")"); i += 1
        b = "\n".join(block)
        m = re.search(r'\(name\s+"([^"]+)"\)', b)
        if not m: continue
        net = m.group(1).lstrip("/")
        for nm in re.finditer(r'\(node\s+\(ref\s+"([^"]+)"\)\s+\(pin\s+"([^"]+)"\)', b, re.S):
            pin_to_net[(nm.group(1), nm.group(2))] = net
        continue
    i += 1


def canonical(ref): return REF_MAP.get(ref, ref)

def check(ref, expected):
    cref = canonical(ref)
    for pin, net in expected.items():
        actual = pin_to_net.get((cref, str(pin)))
        if actual != net:
            raise SystemExit(f"EXPORTED NETLIST FAIL: {ref}->{cref} pin {pin}: expected {net!r}, got {actual!r}")

def check_nc(ref,pin):
    cref=canonical(ref); actual=pin_to_net.get((cref,str(pin)))
    if actual is not None and not actual.startswith("unconnected-("):
        raise SystemExit(f"EXPORTED NETLIST FAIL: {ref}->{cref} pin {pin} must be NC but is on {actual}")

check("U1", {
    "1":"GND","2":"3V3","3":"ESP_EN","4":"RELAY1_CTL","5":"RELAY2_CTL","6":"RELAY3_CTL","7":"RELAY4_CTL",
    "8":"HMI_TX","9":"RS485B_DE","10":"RS485B_TX","11":"RS485B_RX","12":"ETH_RST","13":"USB_D-_MCU","14":"USB_D+_MCU",
    "17":"ETH_INT","18":"ETH_CS","19":"ETH_MOSI","20":"ETH_SCLK","21":"ETH_MISO","22":"HMI_RX","23":"STATUS_ALERT_CTL",
    "24":"DI3_LOGIC","25":"DI4_LOGIC","27":"ESP_BOOT","28":"STATUS_LED_CTL","29":"SD_CS","30":"SD_MISO",
    "31":"RTC_SDA","32":"RTC_SCL","33":"SD_SCLK","34":"SD_MOSI","35":"RS485A_DE","36":"RS485A_RX","37":"RS485A_TX",
    "38":"DI2_LOGIC","39":"DI1_LOGIC","40":"GND","41":"GND",
})
check("R_MCU_DM_SER", {"1":"USB_D-_MCU","2":"USB_D-"})
check("R_MCU_DP_SER", {"1":"USB_D+_MCU","2":"USB_D+"})
check("C_MCU_DM_USB", {"1":"USB_D-_MCU","2":"GND"})
check("C_MCU_DP_USB", {"1":"USB_D+_MCU","2":"GND"})
check("D_USB_DN", {"1":"USB_D-","2":"GND"})
check("D_USB_DP", {"1":"USB_D+","2":"GND"})

check("U2", {
    "1":"ETH_TXN","2":"ETH_TXP","3":"GND","4":"3V3A","5":"ETH_RXN","6":"ETH_RXP","8":"3V3A","9":"GND",
    "10":"ETH_EXRES","11":"3V3A","14":"GND","15":"3V3A","16":"GND","17":"3V3A","19":"GND","20":"ETH_TOCAP",
    "21":"3V3A","22":"ETH_1V2","23":"GND","25":"ETH_LED_LINK","27":"ETH_LED_ACT","28":"3V3","29":"GND",
    "30":"ETH_XI","31":"ETH_XO","32":"ETH_CS","33":"ETH_SCLK","34":"ETH_MISO","35":"ETH_MOSI","36":"ETH_INT",
    "37":"ETH_RST","43":"ETH_PMODE2","44":"ETH_PMODE1","45":"ETH_PMODE0","48":"GND",
})
check("J_ETH", {
    "1":"ETH_TXP_MAG","2":"ETH_TCT","3":"ETH_TXN_MAG","4":"ETH_RXP_MAG","5":"ETH_RCT","6":"ETH_RXN_MAG",
    "8":"CHASSIS","9":"ETH_LED_LINK_K","10":"3V3","11":"ETH_LED_ACT_K","12":"3V3","13":"CHASSIS",
})
check_nc("J_ETH","7")

# WIZnet reference-network continuity checks.
check("FB_ETH", {"1":"3V3","2":"3V3A"})
for suffix,chip,mag in (
    ("TXP","ETH_TXP","ETH_TXP_MAG"),("TXN","ETH_TXN","ETH_TXN_MAG"),
    ("RXP","ETH_RXP","ETH_RXP_MAG"),("RXN","ETH_RXN","ETH_RXN_MAG"),
):
    check(f"R_ETH_{suffix}_DAMP", {"1":chip,"2":mag})
    check(f"R_ETH_{suffix}_BIAS", {"1":"3V3A","2":mag})
check("R_ETH_TCT", {"1":"3V3A","2":"ETH_TCT"})
check("C_ETH_TCT", {"1":"ETH_TCT","2":"GND"})
check("C_ETH_RCT", {"1":"ETH_RCT","2":"GND"})
check("R_ETH_LINK_LED", {"1":"ETH_LED_LINK","2":"ETH_LED_LINK_K"})
check("R_ETH_ACT_LED", {"1":"ETH_LED_ACT","2":"ETH_LED_ACT_K"})
check("C_ETH_CHASSIS", {"1":"GND","2":"CHASSIS"})

for ref, pfx in (("U3","RS485A"),("U4","RS485B")):
    check(ref, {"1":f"{pfx}_RX","2":f"{pfx}_DE","3":f"{pfx}_DE","4":f"{pfx}_TX","5":"GND","6":f"{pfx}_A","7":f"{pfx}_B","8":"3V3"})

check("J_HMI", {"1":"5V_HMI","2":"GND","3":"HMI_TX_OUT","4":"HMI_RX_IN"})
check("U_HMIBUF", {"2":"HMI_RX_IN","3":"GND","4":"HMI_RX","5":"3V3"})
check_nc("U_HMIBUF","1")
check("C_HMIBUF", {"1":"3V3","2":"GND"})
check("D_HMI_RX_ESD", {"1":"HMI_RX_IN","2":"GND"})

check("J_USB", {
    "A1":"GND","A4":"USB_5V","A5":"USB_CC1","A6":"USB_D+","A7":"USB_D-","A9":"USB_5V","A12":"GND",
    "B1":"GND","B4":"USB_5V","B5":"USB_CC2","B6":"USB_D+","B7":"USB_D-","B9":"USB_5V","B12":"GND","SH":"CHASSIS",
})
for n in range(1,5):
    check(f"K{n}", {"A1":"5V_FIELD","A2":f"RELAY{n}_COIL","11":f"RLY{n}_COM","14":f"RLY{n}_NO","12":f"RLY{n}_NC"})
    check(f"Q{n}", {"1":f"RELAY{n}_GATE","2":"GND","3":f"RELAY{n}_COIL"})
check("U7", {"1":"RS232_C1P","2":"RS232_VPLUS","3":"RS232_C1M","4":"RS232_C2P","5":"RS232_C2M","6":"RS232_VMINUS","11":"HMI_TX","12":"HMI_RX","13":"HMI_RS232_RX","14":"HMI_RS232_TX","15":"GND","16":"3V3"})
check("J_RS232", {"1":"GND","2":"HMI_RS232_TX","3":"HMI_RS232_RX"})

# Diagnostic buffer and LEDs are validated from exported KiCad connectivity.
check("U_LEDLOGIC", {
    "1":"RS485A_TX", "2":"DIAG_A_TX_DRV",
    "3":"RS485A_RX", "4":"DIAG_A_RX_DRV",
    "5":"RS485B_TX", "6":"DIAG_B_TX_DRV",
    "7":"GND", "8":"DIAG_B_RX_DRV", "9":"RS485B_RX",
    "10":"STATUS_GREEN_DRV", "11":"STATUS_LED_CTL",
    "12":"STATUS_RED_DRV", "13":"STATUS_ALERT_CTL", "14":"3V3",
})
for ref, drv, lednet in (
    ("RS485A_TX","DIAG_A_TX_DRV","LED_A_TX_A"),
    ("RS485A_RX","DIAG_A_RX_DRV","LED_A_RX_A"),
    ("RS485B_TX","DIAG_B_TX_DRV","LED_B_TX_A"),
    ("RS485B_RX","DIAG_B_RX_DRV","LED_B_RX_A"),
):
    check(f"R_{ref}_ACT", {"1":drv,"2":lednet})
    check(f"D_{ref}_ACT", {"1":"GND","2":lednet})
check("R_STATUS_RUN_PD", {"1":"STATUS_LED_CTL","2":"GND"})
check("R_STATUS_ALERT_PD", {"1":"STATUS_ALERT_CTL","2":"GND"})
check("R_STATUS_GREEN", {"1":"STATUS_GREEN_DRV","2":"STATUS_GREEN_K"})
check("D_STATUS_GREEN", {"1":"STATUS_GREEN_K","2":"3V3"})
check("R_STATUS_RED", {"1":"STATUS_RED_DRV","2":"STATUS_RED_K"})
check("D_STATUS_RED", {"1":"STATUS_RED_K","2":"3V3"})
check("C_LEDLOGIC", {"1":"3V3","2":"GND"})

for ref,pin in [("U2","24"),("U2","26"),("J_USB","A8"),("J_USB","B8")]: check_nc(ref,pin)

print(f"exported physical pin/net audit: PASS ({len(pin_to_net)} connected pins indexed; communications, USB/HMI protection and diagnostics verified)")
