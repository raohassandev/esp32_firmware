#!/usr/bin/env python3
"""Text-safely remove routed segments/arcs/vias for one named net."""
from pathlib import Path
import re
import sys

START_RE = re.compile(r'^\s*\((segment|arc|via)\s*$')
NET_RE = re.compile(r'\(net\s+"([^"]+)"\)')


def paren_delta(line: str) -> int:
    depth=0; quoted=False; esc=False
    for ch in line:
        if quoted:
            if esc: esc=False
            elif ch=='\\': esc=True
            elif ch=='"': quoted=False
        elif ch=='"': quoted=True
        elif ch=='(': depth += 1
        elif ch==')': depth -= 1
    return depth


def main(path_str: str, target: str) -> None:
    path=Path(path_str)
    lines=path.read_text(encoding='utf-8').splitlines(keepends=True)
    out=[]; removed=0; i=0
    while i < len(lines):
        sm=START_RE.match(lines[i])
        if not sm:
            out.append(lines[i]); i += 1; continue
        block=[lines[i]]; depth=paren_delta(lines[i]); i += 1
        while i < len(lines) and depth > 0:
            block.append(lines[i]); depth += paren_delta(lines[i]); i += 1
        text=''.join(block); nm=NET_RE.search(text); net=nm.group(1) if nm else ''
        if net == target: removed += 1
        else: out.extend(block)
    if removed == 0: raise SystemExit(f'no routed primitives found for {target}')
    path.write_text(''.join(out),encoding='utf-8')
    print(f'NET_TRACK_STRIP_PASS net={target} removed={removed}')


if __name__=='__main__':
    if len(sys.argv)!=3: raise SystemExit('usage: strip_net_tracks_text.py BOARD NET')
    main(sys.argv[1],sys.argv[2])
