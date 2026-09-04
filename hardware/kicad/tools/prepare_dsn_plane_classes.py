#!/usr/bin/env python3
"""Split GND out of KiCad's default Specctra class for plane-only handling.

Freerouting otherwise spends most of its search budget trying to route dozens of
GND ratsnest edges even though Rev-A has a dedicated In1.Cu/L2 GND reference.
This script creates a GND_PLANE class containing only GND; the workflow passes
`-inc GND_PLANE`, then KiCad refills the real copper planes after SES import.
"""
from pathlib import Path
import re
import sys


def balanced_block_end(text: str, start: int) -> int:
    depth = 0
    in_string = False
    esc = False
    for i in range(start, len(text)):
        ch = text[i]
        if in_string:
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return i + 1
    raise RuntimeError('unterminated Specctra class block')


def main(path_str: str) -> None:
    path = Path(path_str)
    text = path.read_text(encoding='utf-8')
    if '(class GND_PLANE GND' in text:
        print('DSN_GND_PLANE_CLASS: already prepared')
        return

    start = text.find('(class kicad_default')
    if start < 0:
        raise SystemExit('kicad_default class not found in DSN')
    end = balanced_block_end(text, start)
    block = text[start:end]
    circuit_pos = block.find('(circuit')
    if circuit_pos < 0:
        raise SystemExit('kicad_default circuit block missing')

    header = block[:circuit_pos]
    if not re.search(r'(?<![A-Za-z0-9_])GND(?![A-Za-z0-9_])', header):
        raise SystemExit('GND is not a member of kicad_default class')
    header = re.sub(r'\s+GND(?=\s)', '', header, count=1)
    block = header + block[circuit_pos:]

    via = re.search(r'\(use_via\s+"([^"]+)"\)', block)
    width = re.search(r'\(width\s+(\d+)\)', block)
    clearance = re.search(r'\(clearance\s+(\d+)\)', block)
    if not (via and width and clearance):
        raise SystemExit('cannot clone default DSN routing rule for GND plane class')

    gnd_class = (
        '\n    (class GND_PLANE GND\n'
        '      (circuit\n'
        f'        (use_via "{via.group(1)}")\n'
        '      )\n'
        '      (rule\n'
        f'        (width {width.group(1)})\n'
        f'        (clearance {clearance.group(1)})\n'
        '      )\n'
        '    )'
    )
    text = text[:start] + block + gnd_class + text[end:]
    path.write_text(text, encoding='utf-8')

    if not re.search(r'\(class\s+GND_PLANE\s+GND\b', text):
        raise SystemExit('failed to create GND_PLANE class')
    print('DSN_GND_PLANE_CLASS: PASS class=GND_PLANE net=GND')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: prepare_dsn_plane_classes.py BOARD.dsn')
    main(sys.argv[1])
