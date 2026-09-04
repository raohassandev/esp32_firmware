#!/usr/bin/env python3
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SCAD=ROOT/'mechanical'/'Automatrix_PVDG_RevA_enclosure.scad'
PCB_BUILDER=ROOT/'kicad'/'tools'/'build_reva_pcb.py'
scad=SCAD.read_text(); pcb=PCB_BUILDER.read_text(); low=scad.lower()
def number(text,name):
 m=re.search(rf'^\s*{re.escape(name)}\s*=\s*([0-9.]+)\s*;',text,re.M)
 if not m: raise SystemExit(f'missing SCAD parameter {name}')
 return float(m.group(1))
def py_number(text,name):
 m=re.search(rf'^\s*{re.escape(name)}\s*=\s*([0-9.]+)\s*$',text,re.M)
 if not m: raise SystemExit(f'missing PCB constant {name}')
 return float(m.group(1))
bx=py_number(pcb,'BOARD_X'); by=py_number(pcb,'BOARD_Y'); px=number(scad,'pcb_x'); py=number(scad,'pcb_y'); ox=number(scad,'outer_x'); oy=number(scad,'outer_y'); wall=number(scad,'wall'); inset=number(scad,'pcb_hole_inset')
if (px,py)!=(bx,by): raise SystemExit(f'enclosure PCB mismatch: SCAD={(px,py)} PCB={(bx,by)}')
if inset!=5.0: raise SystemExit(f'mounting inset must be 5 mm, got {inset}')
if ox<px+2*wall or oy<py+2*wall: raise SystemExit('shell does not contain PCB plus walls')
for text in ('relay/contact side = pcb y=0','selv/service side = pcb y=max','rj45 + usb-c + optional microsd = pcb x=max'):
 if text not in low: raise SystemExit(f'mechanical coordinate convention missing: {text}')
print(f'Enclosure contract PASS: PCB {px:g}x{py:g} mm, inset {inset:g} mm, shell {ox:g}x{oy:g} mm')
