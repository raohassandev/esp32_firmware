#!/usr/bin/env python3
"""Fail closed on Rev-A hardware design-control invariants.

This is not a substitute for ERC/DRC or bench validation. It prevents accidental
scope loss while the KiCad design evolves.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
status = (ROOT / "IMPLEMENTATION_STATUS.md").read_text(encoding="utf-8")
parts = (ROOT / "COMPONENT_SELECTION.md").read_text(encoding="utf-8")

required_status_terms = [
    "ESP32-S3-WROOM-1-N8",
    "Two independent protected half-duplex RS485 ports",
    "Four SPDT/Form-C electromechanical dry-contact relay outputs",
    "serial touch-HMI UART",
    "10/100 Ethernet",
    "Four optically isolated 12/24 V digital inputs",
    "RTC",
    "microSD",
]
for term in required_status_terms:
    if term not in status:
        raise SystemExit(f"missing Rev-A scope term: {term}")

required_parts = {
    "MCU": "ESP32-S3-WROOM-1-N8",
    "Ethernet": "WIZnet W5500",
    "RS485": "THVD1410D",
    "Power": "TPS54360B",
}
for label, term in required_parts.items():
    if term not in parts:
        raise SystemExit(f"missing controlled {label} selection: {term}")

# Mandatory GPIOs must be unique. GPIO0 is separately allowed as the BOOT strap.
pairs = re.findall(r"\|\s*([^|]+?)\s*\|\s*([0-9,\.]+)\s*\|", status)
seen = {}
for name, raw in pairs:
    for token in raw.replace("..", ",").split(","):
        token = token.strip()
        if not token.isdigit():
            continue
        gpio = int(token)
        if gpio == 0:
            continue
        if gpio in seen:
            raise SystemExit(f"GPIO{gpio} assigned twice: {seen[gpio]} and {name.strip()}")
        seen[gpio] = name.strip()

for path in [
    ROOT / "Automatrix_PVDG_RevA.kicad_pro",
    ROOT / "Automatrix_PVDG_RevA.kicad_sch",
]:
    if not path.exists() or path.stat().st_size < 32:
        raise SystemExit(f"missing/empty KiCad source: {path}")

print("Rev-A design-control invariants: PASS")
