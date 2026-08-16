#!/usr/bin/env python3
"""Stable CI entrypoint for Rev-A generator composition.

Applies KiCad-10 library compatibility and a collision-free A0 schematic grid
before the native generator runs. Electrical meaning remains in the controlled
manifest/final wrapper; this file owns presentation/library compatibility only.
"""
import generate_reva_final as final

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

# Re-layout the generated engineering schematic on an A0 collision-free grid.
# Compact coordinates previously allowed labels from unrelated symbols to touch,
# which KiCad correctly interpreted as real electrical connections.
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

if __name__ == "__main__":
    final.main()
