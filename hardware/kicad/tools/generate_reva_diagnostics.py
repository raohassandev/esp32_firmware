#!/usr/bin/env python3
"""Rev-A pre-fabrication electrical completion layered over reference fixes.

Adds buffered service indication, exact USB ESD parts, and a partial-power-safe
5 V HMI RX front end while preserving the frozen external interfaces.
"""
import generate_reva_reference_fix as base

g = base.g
comp = base.comp
set_desired_pin = base.set_desired_pin
require = base.require

# New symbols keep their real physical pad numbering. Their component net arrays
# are also stored in physical pin-number order. The legacy generator deliberately
# reverses each visual symbol side when placing labels; KiCad's placed-symbol Y
# transform reverses that side again. Exported-netlist CI is the independent
# proof that the final physical pads receive these intended nets.
g.DEFS.setdefault("LVC14", {
    "prefix": "U",
    "description": "SN74LVC14A hex Schmitt-trigger inverter",
    "pins": [
        ["1","1A"],["2","1Y"],["3","2A"],["4","2Y"],
        ["5","3A"],["6","3Y"],["7","GND"],["8","4Y"],
        ["9","4A"],["10","5Y"],["11","5A"],["12","6Y"],
        ["13","6A"],["14","VCC"],
    ],
})

g.DEFS.setdefault("LVC1G17", {
    "prefix": "U",
    "description": "SN74LVC1G17 single Schmitt-trigger buffer with Ioff",
    "pins": [["1","NC"],["2","A"],["3","GND"],["4","Y"],["5","VCC"]],
})

# Two-pin protection/indicator symbols use physical pad order directly too.
g.DEFS.setdefault("LED0805_DIAG", {
    "prefix": "D",
    "description": "0805 diagnostic LED, pin 1 cathode / pin 2 anode",
    "pins": [["1","K"],["2","A"]],
})
g.DEFS.setdefault("ESD1_UNI", {
    "prefix": "D",
    "description": "Single-channel unidirectional ESD protector, pin 1 I/O / pin 2 GND",
    "pins": [["1","IO"],["2","GND"]],
})
g.DEFS.setdefault("ESD1_BI", {
    "prefix": "D",
    "description": "Single-channel bidirectional ESD protector, pin 1 I/O / pin 2 GND",
    "pins": [["1","IO"],["2","GND"]],
})


def add(ref, sym, value, footprint, nets, dnp=False, datasheet=""):
    if any(c["ref"] == ref for c in g.COMPS):
        return
    g.COMPS.append({
        "ref": ref, "sym": sym, "value": value, "footprint": footprint,
        "datasheet": datasheet, "dnp": dnp, "x": 0, "y": 0, "nets": nets,
    })

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
LED0805 = "LED_SMD:LED_0805_2012Metric"
TSSOP14 = "Package_SO:TSSOP-14_4.4x5mm_P0.65mm"
SOT235 = "Package_TO_SOT_SMD:SOT-23-5"
SOD523 = "Diode_SMD:D_SOD-523"

# Stronger relay gate pull-downs give a firmer OFF state during reset/brownout
# with only ~0.33 mA additional load per GPIO when driven high.
for _ref in ("R_PD1", "R_PD2", "R_PD3", "R_PD4"):
    comp(_ref)["value"] = "10k"

# Freeze the USB D+/D- protection to a high-speed, low-capacitance exact part.
# TPD1E05U06 DYA is the SOD-523/SOT-5X3 two-pin package: pin 1 I/O, pin 2 GND.
for _ref, _net in (("D_USB_DN", "USB_D-"), ("D_USB_DP", "USB_D+")):
    _c = comp(_ref)
    _c["sym"] = "ESD1_UNI"
    _c["value"] = "TPD1E05U06DYAR"
    _c["footprint"] = SOD523
    _c["datasheet"] = "https://www.ti.com/product/TPD1E05U06"
    _c["nets"] = [_net, "GND"]

# Replace the passive 5 V HMI RX divider with a 3.3 V-powered Schmitt buffer.
# SN74LVC1G17 accepts inputs to 5.5 V and provides Ioff partial-power/back-drive
# protection, so a powered HMI cannot inject through the MCU input when 3V3 is
# absent. A bidirectional TVS remains on the connector-side UART signal.
g.COMPS[:] = [c for c in g.COMPS if c["ref"] not in {"R_HMIRX_TOP", "R_HMIRX_BOT"}]
add(
    "U_HMIBUF", "LVC1G17", "SN74LVC1G17DBVR", SOT235,
    [None, "HMI_RX_IN", "GND", "HMI_RX", "3V3"],
    datasheet="https://www.ti.com/product/SN74LVC1G17",
)
add("C_HMIBUF", "CAP", "0.1uF", C0603, ["3V3", "GND"])
add(
    "D_HMI_RX_ESD", "ESD1_BI", "TPD1E10B06DYAR", SOD523,
    ["HMI_RX_IN", "GND"],
    datasheet="https://www.ti.com/product/TPD1E10B06",
)

# GPIO21 is physical module pin 23 and was unconnected in Rev-A. Reserve it as
# the second system-state control; no existing interface moves.
set_desired_pin("U1", "23", "STATUS_ALERT_CTL")

# Replace the original one-colour firmware status LED with green + red. Combined
# illumination represents WARNING; red alone represents FAULT.
g.COMPS[:] = [c for c in g.COMPS if c["ref"] not in {"D_STATUS", "R_STATUS"}]

add(
    "U_LEDLOGIC", "LVC14", "SN74LVC14APWR", TSSOP14,
    [
        "RS485A_TX", "DIAG_A_TX_DRV",
        "RS485A_RX", "DIAG_A_RX_DRV",
        "RS485B_TX", "DIAG_B_TX_DRV",
        "GND", "DIAG_B_RX_DRV", "RS485B_RX",
        "STATUS_GREEN_DRV", "STATUS_LED_CTL",
        "STATUS_RED_DRV", "STATUS_ALERT_CTL", "3V3",
    ],
    datasheet="https://www.ti.com/product/SN74LVC14A",
)
add("C_LEDLOGIC", "CAP", "0.1uF", C0603, ["3V3", "GND"])

# Logic-side activity sensing only: UART idle-high becomes inverter output low,
# so LEDs are dark at idle and flash on low data bits. Field A/B is untouched.
for ref, drv, lednet, color, mpn in (
    ("RS485A_TX", "DIAG_A_TX_DRV", "LED_A_TX_A", "AMBER", "150080AS75000"),
    ("RS485A_RX", "DIAG_A_RX_DRV", "LED_A_RX_A", "GREEN", "150080VS75000"),
    ("RS485B_TX", "DIAG_B_TX_DRV", "LED_B_TX_A", "AMBER", "150080AS75000"),
    ("RS485B_RX", "DIAG_B_RX_DRV", "LED_B_RX_A", "GREEN", "150080VS75000"),
):
    add(f"R_{ref}_ACT", "RES", "680R", R0603, [drv, lednet])
    add(f"D_{ref}_ACT", "LED0805_DIAG", f"{color} {mpn}", LED0805, ["GND", lednet])

# Status controls are active-high at the MCU. Inverter outputs sink LED
# cathodes. Pull-downs make both status LEDs OFF while the MCU is reset.
add("R_STATUS_RUN_PD", "RES", "100k", R0603, ["STATUS_LED_CTL", "GND"])
add("R_STATUS_ALERT_PD", "RES", "100k", R0603, ["STATUS_ALERT_CTL", "GND"])
add("R_STATUS_GREEN", "RES", "680R", R0603, ["STATUS_GREEN_DRV", "STATUS_GREEN_K"])
add("D_STATUS_GREEN", "LED0805_DIAG", "BRIGHT GREEN 150080VS75000", LED0805, ["STATUS_GREEN_K", "3V3"])
add("R_STATUS_RED", "RES", "680R", R0603, ["STATUS_RED_DRV", "STATUS_RED_K"])
add("D_STATUS_RED", "LED0805_DIAG", "RED 150080RS75000", LED0805, ["STATUS_RED_K", "3V3"])


def _physical_intent_map(c):
    pins = g.DEFS[c["sym"]]["pins"]
    if len(pins) != len(c["nets"]):
        raise ValueError(f"{c['ref']}: pin/net length mismatch")
    return {str(pin[0]): net for pin, net in zip(pins, c["nets"])}


def validate_desired_pinout():
    base.validate_desired_pinout()
    require("U1", "23", "STATUS_ALERT_CTL")

    expected_diag = {
        "1":"RS485A_TX", "2":"DIAG_A_TX_DRV",
        "3":"RS485A_RX", "4":"DIAG_A_RX_DRV",
        "5":"RS485B_TX", "6":"DIAG_B_TX_DRV",
        "7":"GND", "8":"DIAG_B_RX_DRV", "9":"RS485B_RX",
        "10":"STATUS_GREEN_DRV", "11":"STATUS_LED_CTL",
        "12":"STATUS_RED_DRV", "13":"STATUS_ALERT_CTL", "14":"3V3",
    }
    actual = _physical_intent_map(comp("U_LEDLOGIC"))
    for pin, net in expected_diag.items():
        if actual.get(pin) != net:
            raise ValueError(f"U_LEDLOGIC pin {pin}: expected {net}, got {actual.get(pin)}")

    expected_hmi = {"1":None, "2":"HMI_RX_IN", "3":"GND", "4":"HMI_RX", "5":"3V3"}
    actual_hmi = _physical_intent_map(comp("U_HMIBUF"))
    for pin, net in expected_hmi.items():
        if actual_hmi.get(pin) != net:
            raise ValueError(f"U_HMIBUF pin {pin}: expected {net}, got {actual_hmi.get(pin)}")

    for ref, net in (("D_USB_DN","USB_D-"),("D_USB_DP","USB_D+")):
        c = comp(ref)
        if _physical_intent_map(c) != {"1":net, "2":"GND"}:
            raise ValueError(f"{ref}: exact USB ESD physical mapping failed")

    if any(c["ref"] in {"D_STATUS", "R_STATUS", "R_HMIRX_TOP", "R_HMIRX_BOT"} for c in g.COMPS):
        raise ValueError("superseded status/divider components still present")
    print("diagnostic, USB ESD, and HMI partial-power physical intent: PASS")

# Make both direct wrapper validation and the lower generator use the augmented
# product-wide physical-pin assertions.
g.validate_critical_pinout = validate_desired_pinout


def main():
    base.main()


if __name__ == "__main__":
    main()
