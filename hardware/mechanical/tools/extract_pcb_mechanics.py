#!/usr/bin/env python3
"""Extract enclosure-driving geometry from the frozen Rev-A KiCad PCB."""
from pathlib import Path
import json
import pcbnew
ROOT=Path(__file__).resolve().parents[2]
PCB=ROOT/'kicad'/'Automatrix_PVDG_RevA.kicad_pcb'
OUT=ROOT/'mechanical'/'PCB_MECHANICAL_HANDOFF.json'
ref_map=json.loads((ROOT/'kicad'/'REFERENCE_MAP.json').read_text())
SEMANTIC=['J_PWR','J_RS485A','J_RS485B','J_HMI','J_RS232','J_DI','J_ETH','J_USB','J_SD','J_RLY1','J_RLY2','J_RLY3','J_RLY4','K1','K2','K3','K4','U1']
if not PCB.exists(): raise SystemExit(f'H2 board missing: {PCB}')
board=pcbnew.LoadBoard(str(PCB))
bbox=board.GetBoardEdgesBoundingBox()
board_info={'width_mm':round(pcbnew.ToMM(bbox.GetWidth()),3),'height_mm':round(pcbnew.ToMM(bbox.GetHeight()),3),'origin_x_mm':round(pcbnew.ToMM(bbox.GetLeft()),3),'origin_y_mm':round(pcbnew.ToMM(bbox.GetTop()),3),'thickness_mm':round(pcbnew.ToMM(board.GetDesignSettings().GetBoardThickness()),3)}
fps={fp.GetReference():fp for fp in board.GetFootprints()}; items={}
for old in SEMANTIC:
 ref=ref_map.get(old,old); fp=fps.get(ref)
 if fp is None: items[old]=None; continue
 pos=fp.GetPosition(); box=fp.GetBoundingBox()
 items[old]={'reference':ref,'x_mm':round(pcbnew.ToMM(pos.x),3),'y_mm':round(pcbnew.ToMM(pos.y),3),'rotation_deg':round(fp.GetOrientationDegrees(),3),'bbox':{'left_mm':round(pcbnew.ToMM(box.GetLeft()),3),'top_mm':round(pcbnew.ToMM(box.GetTop()),3),'right_mm':round(pcbnew.ToMM(box.GetRight()),3),'bottom_mm':round(pcbnew.ToMM(box.GetBottom()),3)},'dnp':bool(fp.IsDNP())}
mounting=[]
for ref,fp in sorted(fps.items()):
 if ref.startswith('H') and fp.GetValue()=='M3':
  p=fp.GetPosition(); mounting.append({'reference':ref,'x_mm':round(pcbnew.ToMM(p.x),3),'y_mm':round(pcbnew.ToMM(p.y),3)})
payload={'source_board':str(PCB.relative_to(ROOT.parent)),'board':board_info,'mounting_holes':mounting,'semantic_items':items,'release_note':'Cutouts and DIN enclosure geometry must be regenerated whenever this JSON changes.'}
OUT.write_text(json.dumps(payload,indent=2)+'\n'); print(f'mechanical handoff PASS -> {OUT}')
