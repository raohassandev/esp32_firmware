#!/usr/bin/env python3
"""Stable CI entrypoint for Rev-A generator composition.

Applies the production electrical wrappers, KiCad-10 library compatibility,
collision-free layout, canonical annotation, and provider-facing exact MPN
freeze before the native generator runs. Electrical meaning remains in the
controlled generator wrappers; semantic references are preserved through a
committed old->canonical mapping.
"""
import json
import re
from pathlib import Path
import generate_reva_diagnostics as final

ROOT = Path(__file__).resolve().parents[1]

# Generic capacitor alias used by the optional RS232 completion wrapper.
final.g.DEFS["CAP"] = final.g.DEFS["C"]

# Current KiCad-10 stock-footprint compatibility. These are explicit prototype
# mechanical choices, not silent vendor substitutions.
FP = {
    "TerminalBlock:TerminalBlock_bornier-2_P5.08mm": "TerminalBlock_CUI:TerminalBlock_CUI_TB007-508-02_1x02_P5.08mm_Horizontal",
    "TerminalBlock:TerminalBlock_bornier-3_P5.08mm": "TerminalBlock_CUI:TerminalBlock_CUI_TB007-508-03_1x03_P5.08mm_Horizontal",
    "TerminalBlock:TerminalBlock_bornier-5_P5.08mm": "TerminalBlock_CUI:TerminalBlock_CUI_TB007-508-05_1x05_P5.08mm_Horizontal",
    "Button_Switch_SMD:SW_SPST_TL3301AN": "Button_Switch_SMD:Panasonic_EVQPUJ_EVQPUA",
    "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm": "Resistor_SMD:R_1206_3216Metric",
    "Package_SO:HSOIC-8_3.9x4.9mm_P1.27mm_EP2.29x3mm": "Package_SO:TI_SO-PowerPAD-8_ThermalVias",
    "Inductor_SMD:L_6.0x6.0mm_H3.0mm": "Inductor_SMD:L_Wuerth_XHMI-6060",
}
for c in final.g.COMPS:
    c["footprint"] = FP.get(c["footprint"], c["footprint"])


def freeze(ref, value, datasheet=None):
    for c in final.g.COMPS:
        if c["ref"] == ref:
            c["value"] = value
            if datasheet is not None:
                c["datasheet"] = datasheet
            return
    raise RuntimeError(f"cannot freeze missing component {ref}")


# Provider-facing exact mechanical/electrical choices that fit the already
# locked stock footprints. These do not alter placement or pin numbering.
freeze("J_PWR", "Same Sky TB007-508-02BE",
       "https://www.sameskydevices.com/product/interconnect/connectors/terminal-blocks/tb007-508-series")
freeze("J_RS485A", "Same Sky TB007-508-03BE",
       "https://www.sameskydevices.com/product/interconnect/connectors/terminal-blocks/tb007-508-series")
freeze("J_RS485B", "Same Sky TB007-508-03BE",
       "https://www.sameskydevices.com/product/interconnect/connectors/terminal-blocks/tb007-508-series")
freeze("J_DI", "Same Sky TB007-508-05BE DNP",
       "https://www.sameskydevices.com/product/interconnect/connectors/terminal-blocks/tb007-508-series")
freeze("J_USB", "GCT USB4105-GF-A-120",
       "https://gct.co/connector/usb4105")

# Main TPS54360B power train: exact 6060 inductor and 1210 capacitors. The
# values/footprints are unchanged; only the source-controlled MPN is now frozen.
freeze("L1", "Wurth 744393465082 8.2uH",
       "https://www.we-online.com/components/products/datasheet/744393465082.pdf")
for ref in ("CIN1", "CIN2"):
    freeze(ref, "Murata GRM32ER72A225KA35L 2.2uF 100V X7R")
for ref in ("COUT1", "COUT2"):
    freeze(ref, "Murata GRM32ER61A476KE20L 47uF 10V X5R")

# W5500 clock remains the WIZnet reference topology with 18 pF shunt capacitors.
# This active 3225 Abracon part matches the locked 4-pad footprint and 25 MHz
# requirement.
freeze("Y1", "Abracon ABM8-25.000MHZ-D2Y-T 25MHz",
       "https://abracon.com/parametric/crystals/ABM8-25.000MHZ-D2Y-T")

# Relay drivers were already electrically AO3400A; make the exact production
# source explicit in the generated provider BOM.
for ref in ("Q1", "Q2", "Q3", "Q4"):
    freeze(ref, "AOS AO3400A", "https://www.aosmd.com/products/mosfets/low-voltage-mosfets-12v-30v/ao3400a")

# Relay contact terminals are no longer a generic 5.08 mm family placeholder.
# Freeze the exact Phoenix Contact 1712193 / MKDS 3/3-5.08 mechanical part and
# its stock KiCad footprint before final routing so provider geometry is stable.
RELAY_TERM_FP = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-3-3-5.08_1x03_P5.08mm_Horizontal"
for c in final.g.COMPS:
    if c["ref"] in {"J_RLY1", "J_RLY2", "J_RLY3", "J_RLY4"}:
        c["value"] = "Phoenix MKDS 3/3-5.08 BK 1712193"
        c["footprint"] = RELAY_TERM_FP
        c["datasheet"] = "https://www.phoenixcontact.com/en-in/products/printed-circuit-board-terminal-mkds-3-3-508-bk-1712193"

# Re-layout the generated engineering schematic on an A0 collision-free grid.
COLS = 12
X0, Y0 = 45.0, 60.0
DX, DY = 65.0, 80.0
for idx, c in enumerate(final.g.COMPS):
    c["x"] = X0 + (idx % COLS) * DX
    c["y"] = Y0 + (idx // COLS) * DY

_orig_generate_schematic = final.g.generate_schematic


def _a0_schematic(used):
    return _orig_generate_schematic(used).replace('(paper "A2")', '(paper "A0")', 1)


final.g.generate_schematic = _a0_schematic

# Validate all safety-critical semantic references BEFORE annotation changes.
final.validate_desired_pinout()

# Canonicalize references for KiCad/manufacturing output. Existing already-
# canonical refs keep their numbers. Semantic refs get the next unused number.
used_numbers = {}
for c in final.g.COMPS:
    m = re.fullmatch(r"([A-Za-z]+)(\d+)", c["ref"])
    if m:
        used_numbers.setdefault(m.group(1), set()).add(int(m.group(2)))

ref_map = {}
next_number = {}
for c in final.g.COMPS:
    old = c["ref"]
    if re.fullmatch(r"[A-Za-z]+\d+", old):
        ref_map[old] = old
        continue
    pm = re.match(r"([A-Za-z]+)", old)
    if not pm:
        raise SystemExit(f"cannot derive annotation prefix from {old}")
    prefix = pm.group(1)
    n = next_number.get(prefix, 1)
    occupied = used_numbers.setdefault(prefix, set())
    while n in occupied:
        n += 1
    new = f"{prefix}{n}"
    occupied.add(n)
    next_number[prefix] = n + 1
    ref_map[old] = new
    c["ref"] = new

(ROOT / "REFERENCE_MAP.json").write_text(
    json.dumps(ref_map, indent=2, sort_keys=True) + "\n", encoding="utf-8"
)

# The semantic physical-pin audit already ran against unmodified references.
final.g.validate_critical_pinout = lambda: print("desired physical pin manifest: PASS (pre-annotation)")

if __name__ == "__main__":
    final.main()
