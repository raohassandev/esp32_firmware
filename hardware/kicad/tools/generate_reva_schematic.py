#!/usr/bin/env python3
"""Generate the controlled Rev-A native KiCad schematic.

The electrical manifest is compressed separately so the complete schematic is
reproducible. Do not hand-edit Automatrix_PVDG_RevA.kicad_sch; edit the
controlled manifest/generator and regenerate, then validate with native KiCad.
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


def uid(key):
    return str(uuid.uuid5(NS, key))


def fnum(n):
    n = float(n)
    if abs(n - round(n)) < 1e-9:
        return str(int(round(n)))
    return f"{n:.3f}".rstrip("0").rstrip(".")


def qs(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"') + '"'


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


def lib_symbol(sym):
    d = DEFS[sym]
    layout, h = pin_layout(sym)
    lines = [
        f'    (symbol "Automatrix:{sym}"',
        '      (pin_names (offset 0.508))',
        '      (exclude_from_sim no)',
        '      (in_bom yes)',
        '      (on_board yes)',
        f'      (property "Reference" {qs(d["prefix"])} (at 0 {fnum(-h/2-2.54)} 0) (effects (font (size 1.27 1.27))))',
        f'      (property "Value" {qs(sym)} (at 0 {fnum(h/2+2.54)} 0) (effects (font (size 1.27 1.27))))',
        '      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27))) (hide yes))',
        '      (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'      (property "Description" {qs(d["description"])} (at 0 0 0) (effects (font (size 1.27 1.27))) (hide yes))',
        f'      (symbol "{sym}_0_1"',
        f'        (rectangle (start -7.62 {fnum(-h/2)}) (end 7.62 {fnum(h/2)}) (stroke (width 0) (type default)) (fill (type background)))',
        '      )',
        f'      (symbol "{sym}_1_1"',
    ]
    for number, name in d["pins"]:
        x, y, angle = layout[str(number)]
        lines.extend([
            f'        (pin passive line (at {fnum(x)} {fnum(y)} {angle}) (length 2.54)',
            f'          (name {qs(name)} (effects (font (size 1.016 1.016))))',
            f'          (number {qs(number)} (effects (font (size 1.016 1.016))))',
            '        )',
        ])
    lines.extend(['      )', '    )'])
    return "\n".join(lines)


def instance(c):
    d = DEFS[c["sym"]]
    _, h = pin_layout(c["sym"])
    x, y = c["x"], c["y"]
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
        f'    (property "Reference" {qs(c["ref"])} (at {fnum(x)} {fnum(y-h/2-2.5)} 0) (effects (font (size 1.27 1.27))))',
        f'    (property "Value" {qs(c["value"])} (at {fnum(x)} {fnum(y+h/2+2.5)} 0) (effects (font (size 1.27 1.27))))',
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
    d = DEFS[c["sym"]]
    layout, _ = pin_layout(c["sym"])
    if len(c["nets"]) != len(d["pins"]):
        raise ValueError(f'{c["ref"]}: net count mismatch')
    lines = []
    for (number, _), net in zip(d["pins"], c["nets"]):
        lx, ly, _ = layout[str(number)]
        x, y = c["x"] + lx, c["y"] + ly
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


def generate():
    used = []
    for c in COMPS:
        if c["sym"] not in used:
            used.append(c["sym"])
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
    out = ROOT / "Automatrix_PVDG_RevA.kicad_sch"
    data = generate()
    out.write_text(data, encoding="utf-8")
    print(f"generated {out} with {len(COMPS)} component instances")


if __name__ == "__main__":
    main()
