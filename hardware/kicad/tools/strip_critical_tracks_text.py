#!/usr/bin/env python3
"""Remove controlled routed primitives without pcbnew/SWIG deletion.

KiCad's SWIG ownership can crash when many BOARD tracks are removed from Python.
This text-safe pass strips top-level segment/arc/via S-expressions on the
controlled USB/Ethernet nets. It also strips the four known USB GND stitching
vias so the deterministic restore can recreate them exactly once.
"""
from pathlib import Path
import math
import re
import sys

TARGETS = {
    'ETH_TXP','ETH_TXN','ETH_RXP','ETH_RXN',
    'USB_D+','USB_D-','USB_D+_MCU','USB_D-_MCU',
}
USB_GND_STITCH = ((126.0,57.0),(130.0,57.0),(126.0,39.0),(130.0,39.0))
START_RE = re.compile(r'^\s*\((segment|arc|via)\s*$')
NET_RE = re.compile(r'\(net\s+"([^"]+)"\)')
AT_RE = re.compile(r'\(at\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\)')


def paren_delta(line: str) -> int:
    depth = 0
    quoted = False
    esc = False
    for ch in line:
        if quoted:
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == '"':
                quoted = False
        elif ch == '"':
            quoted = True
        elif ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
    return depth


def is_usb_gnd_stitch(kind: str, net: str, text: str) -> bool:
    if kind != 'via' or net != 'GND':
        return False
    m = AT_RE.search(text)
    if not m:
        return False
    x,y = float(m.group(1)), float(m.group(2))
    return any(math.hypot(x-a,y-b) < 0.06 for a,b in USB_GND_STITCH)


def main(path_str: str) -> None:
    path = Path(path_str)
    lines = path.read_text(encoding='utf-8').splitlines(keepends=True)
    out = []
    removed = {name: 0 for name in TARGETS}
    stitch_removed = 0
    i = 0
    while i < len(lines):
        sm = START_RE.match(lines[i])
        if not sm:
            out.append(lines[i]); i += 1; continue
        kind = sm.group(1)
        block = [lines[i]]
        depth = paren_delta(lines[i]); i += 1
        while i < len(lines) and depth > 0:
            block.append(lines[i]); depth += paren_delta(lines[i]); i += 1
        text = ''.join(block)
        nm = NET_RE.search(text)
        net = nm.group(1) if nm else ''
        if net in TARGETS:
            removed[net] += 1
        elif is_usb_gnd_stitch(kind, net, text):
            stitch_removed += 1
        else:
            out.extend(block)
    path.write_text(''.join(out), encoding='utf-8')
    total = sum(removed.values())
    if total == 0:
        raise SystemExit('critical-track strip found no routed primitives')
    print(f'CRITICAL_TEXT_STRIP_PASS removed={total} usb_gnd_stitch_removed={stitch_removed} detail={removed}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: strip_critical_tracks_text.py BOARD.kicad_pcb')
    main(sys.argv[1])
