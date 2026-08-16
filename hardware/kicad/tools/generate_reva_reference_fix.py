#!/usr/bin/env python3
"""Rev-A reference-circuit corrections layered over generate_reva_final.

The previous prototype manifest treated the Cetus J1B1211CCD PCB-side pins as
four consecutive MDI pins. The official Cetus/WIZnet pinout is instead:
  1 TD+, 2 TCT, 3 TD-, 4 RD+, 5 RCT, 6 RD-, 7 NC, 8 GHs_GND,
  9 GRN-, 10 GRN+, 11 YLW-, 12 YLW+, 13 chassis shield.
This wrapper also restores the W5500 analog 3V3A filter, MDI bias/centre-tap
network, optional 0R damping footprints, LED resistors and chassis capacitor.
"""
import generate_reva_final as base

g = base.g
comp = base.comp
set_desired_pin = base.set_desired_pin
require = base.require

# Dedicated ferrite symbol (two-pin passive, FB annotation).
g.DEFS.setdefault("FB", {
    "prefix":"FB", "description":"Ferrite bead", "pins":[["1","1"],["2","2"]]
})

# Correct MagJack symbol to the actual stock KiCad/Cetus footprint pad numbers.
jeth = comp("J_ETH")
g.DEFS[jeth["sym"]]["pins"] = [
    ["1","TD+"],["2","TCT"],["3","TD-"],["4","RD+"],["5","RCT"],["6","RD-"],
    ["7","NC"],["8","GHs_GND"],["9","GRN-"],["10","GRN+"],["11","YLW-"],["12","YLW+"],["13","CH_GND"],
]
jeth["nets"] = [
    "ETH_TXP_MAG","ETH_TCT","ETH_TXN_MAG","ETH_RXP_MAG","ETH_RCT","ETH_RXN_MAG",
    None,"CHASSIS","ETH_LED_LINK_K","3V3","ETH_LED_ACT_K","3V3","CHASSIS",
]

# W5500 AVDD pins use a filtered analog rail. Digital VDD pin 28 stays on 3V3.
for pin in ("4","8","11","15","17","21"):
    set_desired_pin("U2",pin,"3V3A")

# Components added once, all based on the official WIZnet reference circuit.
def add(ref,sym,value,footprint,nets,dnp=False,datasheet=""):
    if any(c["ref"]==ref for c in g.COMPS): return
    g.COMPS.append({
        "ref":ref,"sym":sym,"value":value,"footprint":footprint,
        "datasheet":datasheet,"dnp":dnp,"x":0,"y":0,"nets":nets,
    })

R0603="Resistor_SMD:R_0603_1608Metric"
C0603="Capacitor_SMD:C_0603_1608Metric"
C0805="Capacitor_SMD:C_0805_2012Metric"
R0805="Resistor_SMD:R_0805_2012Metric"

# Analog supply isolation + local decoupling.
add("FB_ETH","FB","600R@100MHz >=1A",R0805,["3V3","3V3A"])
for n in range(1,7):
    add(f"C_ETH_AV{n}","C","0.1uF",C0603,["3V3A","GND"])
add("C_ETH_ABULK","C","10uF 10V",C0805,["3V3A","GND"])

# Optional EMI damping positions are populated as 0R for Rev-A.
for suffix,chip,mag in (
    ("TXP","ETH_TXP","ETH_TXP_MAG"),("TXN","ETH_TXN","ETH_TXN_MAG"),
    ("RXP","ETH_RXP","ETH_RXP_MAG"),("RXN","ETH_RXN","ETH_RXN_MAG"),
):
    add(f"R_ETH_{suffix}_DAMP","RES","0R",R0603,[chip,mag])

# 100-ohm MDI matching network: 49.9R from each conductor to filtered 3V3A.
for suffix,net in (
    ("TXP","ETH_TXP_MAG"),("TXN","ETH_TXN_MAG"),
    ("RXP","ETH_RXP_MAG"),("RXN","ETH_RXN_MAG"),
):
    add(f"R_ETH_{suffix}_BIAS","RES","49.9R 1%",R0603,["3V3A",net])

# Transformer centre taps and EMI capacitors per WIZnet J1B1211CCD reference.
add("R_ETH_TCT","RES","10R 1%",R0603,["3V3A","ETH_TCT"])
add("C_ETH_TCT","C","22nF",C0603,["ETH_TCT","GND"])
add("C_ETH_RCT","C","6.8nF",C0603,["ETH_RCT","GND"])

# Active-low W5500 LINK/ACT outputs sink the MagJack LED cathodes through 330R.
add("R_ETH_LINK_LED","RES","330R",R0603,["ETH_LED_LINK","ETH_LED_LINK_K"])
add("R_ETH_ACT_LED","RES","330R",R0603,["ETH_LED_ACT","ETH_LED_ACT_K"])

# Chassis coupling shown in the WIZnet reference design.
add("C_ETH_CHASSIS","C","1nF 2kV",R0805,["GND","CHASSIS"])


def validate_desired_pinout():
    # Preserve the original product-wide assertions, but with corrected U2/J_ETH.
    expected_u1={
        "1":"GND","2":"3V3","3":"ESP_EN","4":"RELAY1_CTL","5":"RELAY2_CTL","6":"RELAY3_CTL","7":"RELAY4_CTL",
        "8":"HMI_TX","9":"RS485B_DE","10":"RS485B_TX","11":"RS485B_RX","12":"ETH_RST","13":"USB_D-_MCU","14":"USB_D+_MCU",
        "17":"ETH_INT","18":"ETH_CS","19":"ETH_MOSI","20":"ETH_SCLK","21":"ETH_MISO","22":"HMI_RX","24":"DI3_LOGIC","25":"DI4_LOGIC",
        "27":"ESP_BOOT","28":"STATUS_LED_CTL","29":"SD_CS","30":"SD_MISO","31":"RTC_SDA","32":"RTC_SCL","33":"SD_SCLK","34":"SD_MOSI",
        "35":"RS485A_DE","36":"RS485A_RX","37":"RS485A_TX","38":"DI2_LOGIC","39":"DI1_LOGIC","40":"GND","41":"GND",
    }
    for pin,net in expected_u1.items(): require("U1",pin,net)

    expected_u2={
        "1":"ETH_TXN","2":"ETH_TXP","3":"GND","4":"3V3A","5":"ETH_RXN","6":"ETH_RXP","8":"3V3A","9":"GND",
        "10":"ETH_EXRES","11":"3V3A","14":"GND","15":"3V3A","16":"GND","17":"3V3A","19":"GND","20":"ETH_TOCAP",
        "21":"3V3A","22":"ETH_1V2","23":"GND","25":"ETH_LED_LINK","27":"ETH_LED_ACT","28":"3V3","29":"GND",
        "30":"ETH_XI","31":"ETH_XO","32":"ETH_CS","33":"ETH_SCLK","34":"ETH_MISO","35":"ETH_MOSI","36":"ETH_INT",
        "37":"ETH_RST","43":"ETH_PMODE2","44":"ETH_PMODE1","45":"ETH_PMODE0","48":"GND",
    }
    for pin,net in expected_u2.items(): require("U2",pin,net)

    for pin,net in {
        "1":"ETH_TXP_MAG","2":"ETH_TCT","3":"ETH_TXN_MAG","4":"ETH_RXP_MAG","5":"ETH_RCT","6":"ETH_RXN_MAG",
        "8":"CHASSIS","9":"ETH_LED_LINK_K","10":"3V3","11":"ETH_LED_ACT_K","12":"3V3","13":"CHASSIS",
    }.items(): require("J_ETH",pin,net)
    if base.desired_pin_net_map(jeth)["7"] is not None:
        raise ValueError("J_ETH physical pin 7 must be NC")

    for ref,pfx in (("U3","RS485A"),("U4","RS485B")):
        for pin,net in {"1":f"{pfx}_RX","2":f"{pfx}_DE","3":f"{pfx}_DE","4":f"{pfx}_TX","5":"GND","6":f"{pfx}_A","7":f"{pfx}_B","8":"3V3"}.items():
            require(ref,pin,net)
    for pin,net in {"1":"5V_HMI","2":"GND","3":"HMI_TX_OUT","4":"HMI_RX_IN"}.items(): require("J_HMI",pin,net)
    for pin,net in {
        "A1":"GND","A4":"USB_5V","A5":"USB_CC1","A6":"USB_D+","A7":"USB_D-","A9":"USB_5V","A12":"GND",
        "B1":"GND","B4":"USB_5V","B5":"USB_CC2","B6":"USB_D+","B7":"USB_D-","B9":"USB_5V","B12":"GND","SH":"CHASSIS",
    }.items(): require("J_USB",pin,net)
    for n in range(1,5):
        for pin,net in {"A1":"5V_FIELD","A2":f"RELAY{n}_COIL","11":f"RLY{n}_COM","14":f"RLY{n}_NO","12":f"RLY{n}_NC"}.items(): require(f"K{n}",pin,net)
        for pin,net in {"1":f"RELAY{n}_GATE","2":"GND","3":f"RELAY{n}_COIL"}.items(): require(f"Q{n}",pin,net)
    for pin,net in {"1":"RS232_C1P","2":"RS232_VPLUS","3":"RS232_C1M","4":"RS232_C2P","5":"RS232_C2M","6":"RS232_VMINUS","11":"HMI_TX","12":"HMI_RX","13":"HMI_RS232_RX","14":"HMI_RS232_TX","15":"GND","16":"3V3"}.items(): require("U7",pin,net)
    for pin,net in {"1":"GND","2":"HMI_RS232_TX","3":"HMI_RS232_RX"}.items(): require("J_RS232",pin,net)
    print("desired physical pin manifest: PASS (W5500/J1B1211CCD reference corrected)")

# Make the lower-level generator invoke our corrected assertions too.
g.validate_critical_pinout = validate_desired_pinout


def main():
    base.main()

if __name__ == "__main__":
    main()
