#!/usr/bin/env python3
"""Remove routed primitives on controlled high-speed nets without pcbnew deletion.

KiCad's SWIG ownership can crash when many BOARD tracks are removed from Python.
This text-safe pass strips only top-level segment/arc/via S-expressions whose
`(net "...")` is one of the controlled USB/Ethernet nets. The file is then
reloaded in a fresh pcbnew process for deterministic route restoration.
"""
from pathlib import Path
import re
import sys

TARGETS = {
    'ETH_TXP','ETH_TXN','ETH_RXP','ETH_RXN',
    'USB_D+','USB_D-','USB_D+_MCU','USB_D-_MCU',
}
START_RE = re.compile(r'^\s*\((segment|arc|via)\s*$')
NET_RE = re.compile(r'\(net\s+"([^"]+)"\)')


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


def main(path_str: str) -> None:
    path = Path(path_str)
    lines = path.read_text(encoding='utf-8').splitlines(keepends=True)
    out = []
    removed = {name: 0 for name in TARGETS}
    i = 0
    while i < len(lines):
        if not START_RE.match(lines[i]):
            out.append(lines[i]); i += 1; continue
        block = [lines[i]]
        depth = paren_delta(lines[i]); i += 1
        while i < len(lines) and depth > 0:
            block.append(lines[i]); depth += paren_delta(lines[i]); i += 1
        text = ''.join(block)
        m = NET_RE.search(text)
        if m and m.group(1) in TARGETS:
            removed[m.group(1)] += 1
        else:
            out.extend(block)
    path.write_text(''.join(out), encoding='utf-8')
    total = sum(removed.values())
    if total == 0:
        raise SystemExit('critical-track strip found no routed primitives')
    print(f'CRITICAL_TEXT_STRIP_PASS removed={total} detail={removed}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: strip_critical_tracks_text.py BOARD.kicad_pcb')
    main(sys.argv[1])
