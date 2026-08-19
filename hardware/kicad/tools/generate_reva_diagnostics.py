#!/usr/bin/env python3
"""Rev-A diagnostic-indicator completion layered over reference-circuit fixes.

Adds non-loading logic-side RS485 TX/RX activity indication and a two-channel
system status indicator without touching the RS485 A/B field pair. GPIO35 stays
the RUN/status control; previously unused ESP32-S3-WROOM-1 GPIO21 becomes the
second status control. RUN=green, FAULT=red, WARNING=red+green.
"""
import generate_reva_reference_fix as base

g = base.g
comp = base.comp
set_desired_pin = base.set_desired_pin
require = base.require

# SN74LVC14APWR: six Schmitt-trigger inverters, 3.3 V, TSSOP-14.
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

# Dedicated 0805 indicator symbol. KiCad LED_0805 pad 1 is cathode, pad 2 anode.
g.DEFS.setdefault("LED0805_DIAG", {
    "prefix": "D",
    "description": "0805 diagnostic LED, pin 1 cathode / pin 2 anode",
    "pins": [["1","K"],["2","A"]],
})

# The generic generator reverses rectangular multi-pin symbols by side because
# the legacy manifest stores visual-order nets. Diagnostic components below are
# authored directly in physical pin order, so keep LVC14 pins in physical order.
_orig_manifest_pin_order = g.manifest_pin_order

def _diag_manifest_pin_order(sym):
    if sym == "LVC14":
        return g.DEFS[sym]["pins"][:]
    return _orig_manifest_pin_order(sym)

g.manifest_pin_order = _diag_manifest_pin_order


def add(ref, sym, value, footprint, nets, dnp=False, datasheet=""):
    if any(c["ref"] == ref for c in g.COMPS):
        return
    g.COMPS.append({
        "ref": ref, "sym": sym, "value": value, "footprint": footprint,
        "datasheet": datasheet, "dnp": dnp, "x": 0, "y": 0, "nets": nets,
    })

# Strengthen the four relay gate pull-downs for a firmer hardware-OFF state in
# noisy industrial wiring while adding only ~0.33 mA GPIO load when driven high.
for _ref in ("R_PD1", "R_PD2", "R_PD3", "R_PD4"):
    comp(_ref)["value"] = "10k"

# GPIO21 is physical module pin 23 on ESP32-S3-WROOM-1 and was unconnected in
# Rev-A. Reserve it as the second status control; no existing interface moves.
set_desired_pin("U1", "23", "STATUS_ALERT_CTL")

# Replace the original one-colour firmware status LED with a two-colour state
# pair. Keeping two ordinary 0805 LEDs avoids a custom multi-colour footprint.
g.COMPS[:] = [c for c in g.COMPS if c["ref"] not in {"D_STATUS", "R_STATUS"}]

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
LED0805 = "LED_SMD:LED_0805_2012Metric"
TSSOP14 = "Package_SO:TSSOP-14_4.4x5mm_P0.65mm"

# Physical pins: 1A/1Y=A-TX, 2A/2Y=A-RX, 3A/3Y=B-TX,
# 4A/4Y=B-RX, 5A/5Y=RUN green, 6A/6Y=FAULT red.
add(
    "U_DIAG", "LVC14", "SN74LVC14APWR", TSSOP14,
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
add("C_DIAG", "CAP", "0.1uF", C0603, ["3V3", "GND"])

# High-impedance logic-side activity sensing: UART idle-high becomes inverter
# output low, so LEDs are dark at idle and flash on low data bits.
for ref, drv, lednet, color, mpn in (
    ("RS485A_TX", "DIAG_A_TX_DRV", "LED_A_TX_A", "AMBER", "150080AS75000"),
    ("RS485A_RX", "DIAG_A_RX_DRV", "LED_A_RX_A", "GREEN", "150080VS75000"),
    ("RS485B_TX", "DIAG_B_TX_DRV", "LED_B_TX_A", "AMBER", "150080AS75000"),
    ("RS485B_RX", "DIAG_B_RX_DRV", "LED_B_RX_A", "GREEN", "150080VS75000"),
):
    add(f"R_{ref}_ACT", "RES", "680R", R0603, [drv, lednet])
    add(f"D_{ref}_ACT", "LED0805_DIAG", f"{color} {mpn}", LED0805, ["GND", lednet])

# Status controls are active-high at the MCU. Inverter outputs sink the LED
# cathodes. 100k pulldowns make both indicators OFF while the MCU is reset.
add("R_STATUS_RUN_PD", "RES", "100k", R0603, ["STATUS_LED_CTL", "GND"])
add("R_STATUS_ALERT_PD", "RES", "100k", R0603, ["STATUS_ALERT_CTL", "GND"])
add("R_STATUS_GREEN", "RES", "680R", R0603, ["STATUS_GREEN_DRV", "STATUS_GREEN_K"])
add("D_STATUS_GREEN", "LED0805_DIAG", "BRIGHT GREEN 150080VS75000", LED0805, ["STATUS_GREEN_K", "3V3"])
add("R_STATUS_RED", "RES", "680R", R0603, ["STATUS_RED_DRV", "STATUS_RED_K"])
add("D_STATUS_RED", "LED0805_DIAG", "RED 150080RS75000", LED0805, ["STATUS_RED_K", "3V3"])


def _desired_map(c):
    pins = g.DEFS[c["sym"]]["pins"]
    if len(pins) != len(c["nets"]):
        raise ValueError(f"{c['ref']}: pin/net length mismatch")
    return {str(pin[0]): net for pin, net in zip(pins, c["nets"])}


def validate_desired_pinout():
    base.validate_desired_pinout()
    require("U1", "23", "STATUS_ALERT_CTL")
    expected = {
        "1":"RS485A_TX", "2":"DIAG_A_TX_DRV",
        "3":"RS485A_RX", "4":"DIAG_A_RX_DRV",
        "5":"RS485B_TX", "6":"DIAG_B_TX_DRV",
        "7":"GND", "8":"DIAG_B_RX_DRV", "9":"RS485B_RX",
        "10":"STATUS_GREEN_DRV", "11":"STATUS_LED_CTL",
        "12":"STATUS_RED_DRV", "13":"STATUS_ALERT_CTL", "14":"3V3",
    }
    actual = _desired_map(comp("U_DIAG"))
    for pin, net in expected.items():
        if actual.get(pin) != net:
            raise ValueError(f"U_DIAG pin {pin}: expected {net}, got {actual.get(pin)}")
    if any(c["ref"] in {"D_STATUS", "R_STATUS"} for c in g.COMPS):
        raise ValueError("legacy single-colour status LED still present")
    print("diagnostic physical-pin manifest: PASS")

# Make both direct wrapper validation and the lower generator use the augmented
# product-wide physical-pin assertions.
g.validate_critical_pinout = validate_desired_pinout


def main():
    base.main()


if __name__ == "__main__":
    main()
