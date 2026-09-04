#!/usr/bin/env python3
"""Generate the controlled Rev-A native KiCad schematic and local symbol library.

The electrical manifest stores nets in the same top-to-bottom visual pin order
used when the schematic was authored.  The generator converts that order back
to physical pad numbers explicitly and asserts safety-critical sentinel pins.
Do not hand-edit generated KiCad sources; edit the controlled manifest/generator
and let KiCad ERC validate the result.
"""
from pathlib import Path
import base64, json, uuid, zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
MANIFEST = json.loads(zlib.decompress(base64.b64decode(
    (HERE / "reva_manifest.zlib.b64").read_text().strip()
)).decode())
DEFS = MANIFEST["defs"]
COMPS = MANIFEST["comps"]
NS = uuid.UUID("a1b2c3d4-e5f6-47a8-9b0c-112233445566")
ROOT_UUID = str(uuid.uuid5(NS, "Automatrix_PVDG_RevA:root"))
GRID = 1.27

# Use stock KiCad-10 footprints where an exact library footprint exists.
# The relay remains the exact HF3FF part electrically, but uses the Hongfa
# JQC-3FF/HF3FF-family PCB pattern after mechanical compatibility review.
FOOTPRINT_OVERRIDES = {
    "Automatrix:RJ45_J1B1211CCD": "Connector_RJ:RJ45_Cetus_J1B1211CCD_Horizontal",
    "Automatrix:USB_C_USB2_16P_THTShell": "Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal",
    "Automatrix:SM712": "Package_TO_SOT_SMD:SOT-23",
    "Automatrix:Relay_Hongfa_HF3FF_1FormC": "Relay_THT:Relay_SPDT_Hongfa_JQC-3FF_0XX-1Z",
}
for c in COMPS:
    c["footprint"] = FOOTPRINT_OVERRIDES.get(c["footprint"], c["footprint"])

# Match the standard relay footprint pad nomenclature: coil A1/A2, COM 11,
# NO 14, NC 12.  Pin list order is intentionally unchanged semantically so the
# visual-order manifest remains valid.
if "RELAY" in DEFS:
    DEFS["RELAY"]["pins"] = [
        ["A1", "COIL_A"], ["A2", "COIL_B"], ["11", "COM"],
        ["14", "NO"], ["12", "NC"],
    ]


def uid(key):
    return str(uuid.uuid5(NS, key))


def fnum(n):
    n = float(n)
    if abs(n - round(n)) < 1e-9:
        return str(int(round(n)))
    return f"{n:.3f}".rstrip("0").rstrip(".")


def qs(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


def snap(value):
    return round(float(value) / GRID) * GRID


def comp_xy(c):
    return snap(c["x"]), snap(c["y"])


def manifest_pin_order(sym):
    """Return the physical pins corresponding to c['nets'] order.

    Ordinary rectangular symbols were authored top-to-bottom on the left side,
    then top-to-bottom on the right side.  The local pin list itself is stored
    numerically/semantically, so each half must be reversed.  The MOSFET symbol
    has gate left and drain/source on the right, giving G,D,S visual order.
    """
    pins = DEFS[sym]["pins"]
    n = len(pins)
    if n == 2:
        return pins[:]
    if n == 3 and sym in ("NMOS", "PMOS"):
        return [pins[0], pins[2], pins[1]]
    left = (n + 1) // 2
    return list(reversed(pins[:left])) + list(reversed(pins[left:]))


def pin_net_map(c):
    pins = manifest_pin_order(c["sym"])
    if len(c["nets"]) != len(pins):
        raise ValueError(f'{c["ref"]}: net count mismatch')
    return {str(pin[0]): net for pin, net in zip(pins, c["nets"])}


def require_pin(ref, pin, expected):
    c = next(x for x in COMPS if x["ref"] == ref)
    actual = pin_net_map(c).get(str(pin))
    if actual != expected:
        raise ValueError(f"{ref} physical pin {pin}: expected {expected!r}, got {actual!r}")


def validate_critical_pinout():
    # ESP32-S3-WROOM-1 physical module pins per Espressif module pinout.
    expected_u1 = {
        "1": "GND", "2": "3V3", "3": "ESP_EN",
        "4": "RELAY1_CTL", "5": "RELAY2_CTL", "6": "RELAY3_CTL", "7": "RELAY4_CTL",
        "8": "HMI_TX", "9": "RS485B_DE", "10": "RS485B_TX", "11": "RS485B_RX",
        "12": "ETH_RST", "13": "USB_D-", "14": "USB_D+",
        "17": "ETH_INT", "18": "ETH_CS", "19": "ETH_MOSI", "20": "ETH_SCLK", "21": "ETH_MISO",
        "22": "HMI_RX", "24": "DI3_LOGIC", "25": "DI4_LOGIC",
        "27": "ESP_BOOT", "28": "STATUS_LED_CTL", "29": "SD_CS", "30": "SD_MISO",
        "31": "RTC_SDA", "32": "RTC_SCL", "33": "SD_SCLK", "34": "SD_MOSI",
        "35": "RS485A_DE", "36": "RS485A_RX", "37": "RS485A_TX",
        "38": "DI2_LOGIC", "39": "DI1_LOGIC", "40": "GND", "41": "GND",
    }
    for pin, net in expected_u1.items():
        require_pin("U1", pin, net)

    # W5500 48-LQFP physical pins per WIZnet datasheet.
    expected_u2 = {
        "1": "ETH_TXN", "2": "ETH_TXP", "3": "GND", "4": "3V3",
        "5": "ETH_RXN", "6": "ETH_RXP", "8": "3V3", "9": "GND",
        "10": "ETH_EXRES", "11": "3V3", "14": "GND", "15": "3V3",
        "16": "GND", "17": "3V3", "19": "GND", "20": "ETH_TOCAP",
        "21": "3V3", "22": "ETH_1V2", "23": "GND",
        "24": "ETH_LED_SPD", "25": "ETH_LED_LINK", "26": "ETH_LED_DUP", "27": "ETH_LED_ACT",
        "28": "3V3", "29": "GND", "30": "ETH_XI", "31": "ETH_XO",
        "32": "ETH_CS", "33": "ETH_SCLK", "34": "ETH_MISO", "35": "ETH_MOSI",
        "36": "ETH_INT", "37": "ETH_RST", "43": "ETH_PMODE2",
        "44": "ETH_PMODE1", "45": "ETH_PMODE0", "48": "GND",
    }
    for pin, net in expected_u2.items():
        require_pin("U2", pin, net)

    for ref, prefix in (("U3", "RS485A"), ("U4", "RS485B")):
        for pin, net in {
            "1": f"{prefix}_RX", "2": f"{prefix}_DE", "3": f"{prefix}_DE",
            "4": f"{prefix}_TX", "5": "GND", "6": f"{prefix}_A",
            "7": f"{prefix}_B", "8": "3V3",
        }.items():
            require_pin(ref, pin, net)

    for pin, net in {"1":"5V_HMI", "2":"GND", "3":"HMI_TX_OUT", "4":"HMI_RX_IN"}.items():
        require_pin("J_HMI", pin, net)
    for pin, net in {"1":"USB_5V", "2":"USB_D-", "3":"USB_D+", "4":"USB_CC1", "5":"USB_CC2", "6":"GND"}.items():
        require_pin("J_USB", pin, net)

    for n in range(1, 5):
        ref = f"K{n}"
        for pin, net in {"A1":"5V_FIELD", "A2":f"RELAY{n}_COIL", "11":f"RLY{n}_COM", "14":f"RLY{n}_NO", "12":f"RLY{n}_NC"}.items():
            require_pin(ref, pin, net)
        qref = f"Q{n}"
        for pin, net in {"1":f"RELAY{n}_GATE", "2":"GND", "3":f"RELAY{n}_COIL"}.items():
            require_pin(qref, pin, net)

    print("critical physical pin mapping: PASS")


def pin_layout(sym):
    pins = DEFS[sym]["pins"]
    nums = [str(p[0]) for p in pins]
    n = len(nums)
    out = {}
    if n == 2:
        out[nums[0]] = (-10.16, 0.0, 0)
        out[nums[1]] = (10.16, 0.0, 180)
        return out, 5.08
    if n == 3 and sym in ("NMOS", "PMOS"):
        out[nums[0]] = (-10.16, 0.0, 0)
        out[nums[1]] = (10.16, 2.54, 180)
        out[nums[2]] = (10.16, -2.54, 180)
        return out, 7.62
    left = (n + 1) // 2
    right = n - left
    pitch = 2.54
    for i, num in enumerate(nums[:left]):
        out[num] = (-10.16, (i - (left - 1) / 2) * pitch, 0)
    for i, num in enumerate(nums[left:]):
        out[num] = (10.16, (i - (right - 1) / 2) * pitch, 180)
    return out, max(5.08, max(left, right) * pitch + 2.54)


def lib_symbol(sym, qualified=True, indent="    "):
    d = DEFS[sym]
    layout, h = pin_layout(sym)
    name = f"Automatrix:{sym}" if qualified else sym
    i = indent
    lines = [
        f'{i}(symbol {qs(name)}',
        f'{i}  (pin_names (offset 0.508))',
        f'{i}  (exclude_from_sim no)',
        f'{i}  (in_bom yes)',
        f'{i}  (on_board yes)',
        f'{i}  (property "Reference" {qs(d["prefix"])} (at 0 {fnum(-h/2-2.54)} 0) (effects (font (size 1.27 1.27))))',
        f'{i}  (property "Value" {qs(sym)} (at 0 {fnum(h/2+2.54)} 0) (effects (font (size 1.27 1.27))))',
        f'{i}  (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'{i}  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'{i}  (property "Description" {qs(d["description"])} (at 0 0 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'{i}  (symbol {qs(sym + "_0_1")}',
        f'{i}    (rectangle (start -7.62 {fnum(-h/2)}) (end 7.62 {fnum(h/2)}) (stroke (width 0) (type default)) (fill (type background)))',
        f'{i}  )',
        f'{i}  (symbol {qs(sym + "_1_1")}',
    ]
    for number, name in d["pins"]:
        x, y, angle = layout[str(number)]
        lines.extend([
            f'{i}    (pin passive line (at {fnum(x)} {fnum(y)} {angle}) (length 2.54)',
            f'{i}      (name {qs(name)} (effects (font (size 1.016 1.016))))',
            f'{i}      (number {qs(number)} (effects (font (size 1.016 1.016))))',
            f'{i}    )',
        ])
    lines.extend([f'{i}  )', f'{i})'])
    return "\n".join(lines)


def instance(c):
    d = DEFS[c["sym"]]
    _, h = pin_layout(c["sym"])
    x, y = comp_xy(c)
    lines = [
        '  (symbol',
        f'    (lib_id "Automatrix:{c["sym"]}")',
        f'    (at {fnum(x)} {fnum(y)} 0)',
        '    (unit 1)',
        '    (exclude_from_sim no)',
        '    (in_bom yes)',
        '    (on_board yes)',
        f'    (dnp {"yes" if c.get("dnp") else "no"})',
        f'    (uuid {uid("comp:" + c["ref"])})',
        f'    (property "Reference" {qs(c["ref"])} (at {fnum(x)} {fnum(y-h/2-2.54)} 0) (effects (font (size 1.27 1.27))))',
        f'    (property "Value" {qs(c["value"])} (at {fnum(x)} {fnum(y+h/2+2.54)} 0) (effects (font (size 1.27 1.27))))',
        f'    (property "Footprint" {qs(c["footprint"])} (at {fnum(x)} {fnum(y)} 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'    (property "Datasheet" {qs(c.get("datasheet") or "~")} (at {fnum(x)} {fnum(y)} 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'    (property "Description" {qs(d["description"])} (at {fnum(x)} {fnum(y)} 0) (effects (font (size 1.27 1.27))) (hide yes))',
    ]
    for number, _ in d["pins"]:
        lines.append(f'    (pin {qs(number)} (uuid {uid("pin:" + c["ref"] + ":" + str(number))}))')
    lines.extend([
        '    (instances',
        '      (project "Automatrix_PVDG_RevA"',
        f'        (path "/{ROOT_UUID}"',
        f'          (reference {qs(c["ref"])})',
        '          (unit 1)',
        '        )',
        '      )',
        '    )',
        '  )',
    ])
    return "\n".join(lines)


def connections(c):
    layout, _ = pin_layout(c["sym"])
    pins = manifest_pin_order(c["sym"])
    if len(c["nets"]) != len(pins):
        raise ValueError(f'{c["ref"]}: net count mismatch')
    x0, y0 = comp_xy(c)
    lines = []
    for (number, _), net in zip(pins, c["nets"]):
        lx, ly, _ = layout[str(number)]
        x, y = x0 + lx, y0 + ly
        if net is None:
            lines.append(f'  (no_connect (at {fnum(x)} {fnum(y)}) (uuid {uid("nc:" + c["ref"] + ":" + str(number))}))')
        else:
            angle = 0 if lx < 0 else 180
            justify = '(justify left bottom)' if lx < 0 else '(justify right bottom)'
            lines.append(
                f'  (label {qs(net)} (at {fnum(x)} {fnum(y)} {angle}) '
                f'(effects (font (size 1.016 1.016)) {justify}) '
                f'(uuid {uid("label:" + c["ref"] + ":" + str(number) + ":" + net)}))'
            )
    return "\n".join(lines)


def used_symbols():
    result = []
    for c in COMPS:
        if c["sym"] not in result:
            result.append(c["sym"])
    return result


def generate_symbol_library(used):
    lines = ['(kicad_symbol_lib', '  (version 20231120)', '  (generator kicad_symbol_editor)']
    lines.extend(lib_symbol(s, qualified=False, indent='  ') for s in used)
    lines.append(')')
    return "\n".join(lines) + "\n"


def generate_schematic(used):
    lines = [
        '(kicad_sch',
        '  (version 20231120)',
        '  (generator eeschema)',
        f'  (uuid {ROOT_UUID})',
        '  (paper "A2")',
        '  (title_block',
        '    (title "Automatrix ESP32 PV-DG Controller")',
        '    (date "2026-08-16")',
        '    (rev "Rev-A")',
        '    (company "Automatrix Engineering")',
        '    (comment 1 "2x RS485 + Ethernet + HMI UART + 4x Form-C relays")',
        '    (comment 2 "Optional: 4x isolated DI, RTC, microSD, RS232 HMI")',
        '  )',
        '  (lib_symbols',
    ]
    lines.extend(lib_symbol(s) for s in used)
    lines.append('  )')
    lines.extend(connections(c) for c in COMPS)
    lines.extend(instance(c) for c in COMPS)
    lines.extend(['  (sheet_instances', '    (path "/" (page "1"))', '  )', ')'])
    return "\n".join(lines) + "\n"


def main():
    validate_critical_pinout()
    used = used_symbols()
    sch = ROOT / "Automatrix_PVDG_RevA.kicad_sch"
    sym = ROOT / "Automatrix.kicad_sym"
    sch.write_text(generate_schematic(used), encoding="utf-8")
    sym.write_text(generate_symbol_library(used), encoding="utf-8")
    (ROOT / "sym-lib-table").write_text(
        '(sym_lib_table\n  (version 7)\n  (lib (name "Automatrix")(type "KiCad")(uri "${KIPRJMOD}/Automatrix.kicad_sym")(options "")(descr "Rev-A project local symbols"))\n)\n',
        encoding="utf-8",
    )
    print(f"generated {sch} with {len(COMPS)} component instances")
    print(f"generated {sym} with {len(used)} project-local symbols")


if __name__ == "__main__":
    main()
