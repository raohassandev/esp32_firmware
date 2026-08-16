#!/usr/bin/env python3
"""Build the Rev-A KiCad PCB from the validated schematic netlist.

H2 starts with deterministic footprint loading, net assignment and industrial
functional placement. Routing is added in controlled stages; this script never
silently drops a footprint or schematic pad. It runs inside KiCad 10's pcbnew
Python environment.
"""
from __future__ import annotations
from pathlib import Path
from collections import defaultdict
import json, os, re, sys
import pcbnew

ROOT = Path(__file__).resolve().parents[1]
NETLIST = ROOT / "Automatrix_PVDG_RevA.net"
REF_MAP = json.loads((ROOT / "REFERENCE_MAP.json").read_text(encoding="utf-8"))
INV_REF = {v:k for k,v in REF_MAP.items()}
OUT = ROOT / "Automatrix_PVDG_RevA.kicad_pcb"
FP_ROOT = Path(os.environ.get("KICAD10_FOOTPRINT_DIR", "/usr/share/kicad/footprints"))

BOARD_X = 145.0
BOARD_Y = 95.0
EDGE_MARGIN = 1.0
PLACEMENT_CLEARANCE = 0.20


def blocks(text, token):
    lines=text.splitlines(); out=[]; i=0
    while i < len(lines):
        if re.match(rf"\s*\({re.escape(token)}\s*$", lines[i]):
            depth=lines[i].count('(')-lines[i].count(')'); b=[lines[i]]; i+=1
            while i < len(lines) and depth>0:
                b.append(lines[i]); depth += lines[i].count('(')-lines[i].count(')'); i+=1
            out.append('\n'.join(b)); continue
        i+=1
    return out


def parse_netlist():
    text=NETLIST.read_text(encoding='utf-8')
    comps={}
    for b in blocks(text,'comp'):
        mref=re.search(r'\(ref\s+"([^"]+)"\)',b)
        if not mref: continue
        ref=mref.group(1)
        val=re.search(r'\(value\s+"([^"]*)"\)',b)
        fp=re.search(r'\(footprint\s+"([^"]*)"\)',b)
        comps[ref]={'value':val.group(1) if val else '', 'footprint':fp.group(1) if fp else ''}
    nets={}; pin_nets={}
    for b in blocks(text,'net'):
        m=re.search(r'\(name\s+"([^"]+)"\)',b)
        if not m: continue
        name=m.group(1).lstrip('/')
        nodes=[]
        for nm in re.finditer(r'\(node\s+\(ref\s+"([^"]+)"\)\s+\(pin\s+"([^"]+)"\)',b,re.S):
            node=(nm.group(1),nm.group(2)); nodes.append(node); pin_nets[node]=name
        nets[name]=nodes
    return comps,nets,pin_nets


def mm(x,y): return pcbnew.VECTOR2I_MM(float(x),float(y))


def add_edge(board,a,b):
    s=pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetStart(mm(*a)); s.SetEnd(mm(*b)); s.SetWidth(pcbnew.FromMM(0.05)); board.Add(s)


def load_fp(fp_id):
    if ':' not in fp_id: raise RuntimeError(f'bad footprint id {fp_id!r}')
    lib,name=fp_id.split(':',1); libpath=FP_ROOT/f'{lib}.pretty'
    if not libpath.exists(): raise RuntimeError(f'footprint library missing: {libpath}')
    fp=pcbnew.FootprintLoad(str(libpath),name)
    if fp is None: raise RuntimeError(f'footprint not found: {fp_id}')
    return fp


def optional_semantic(old):
    keys=('RS232','_DI','D_DI','R_DI','DIPU','RTC','_SD','SDCS','SDMISO','SDMOSI','SDSCLK')
    return old=='BT1' or any(k in old for k in keys)

# Fixed industrial anchors. Coordinates are footprint anchors, not enclosure cutout coordinates.
FIXED={
 'J_PWR':(9,89,0),'J_RS485A':(34,91,0),'J_RS485B':(55,91,0),'J_HMI':(80,91,0),'J_DI':(118,91,0),
 'J_ETH':(141,67,90),'J_USB':(143,43,90),'J_SD':(122,47,90),'J_RS232':(101,91,0),
 'J_RLY1':(20,4,180),'J_RLY2':(52,4,180),'J_RLY3':(84,4,180),'J_RLY4':(116,4,180),
 'K1':(20,18,0),'K2':(52,18,0),'K3':(84,18,0),'K4':(116,18,0),
 'Q1':(20,32,0),'Q2':(52,32,0),'Q3':(84,32,0),'Q4':(116,32,0),
 'U5':(22,56,0),'U6':(48,56,0),'U1':(76,72,0),'U2':(116,64,0),
 'U3':(38,78,0),'U4':(60,78,0),'U7':(92,79,0),'U_RTC':(91,55,0),
 'U_DI1':(105,78,0),'U_DI2':(115,78,0),'U_DI3':(125,78,0),'U_DI4':(135,78,0),
 'BT1':(100,51,0),'Y1':(126,61,0),'Y_RTC':(97,55,0),
 'SW_RESET':(68,51,0),'SW_BOOT':(76,51,0),
}

# Functional zones are deliberate overlapping search envelopes. Collision checks
# still prevent physical overlap; the overlap lets related small passives pack
# tightly without forcing the board larger than the 145 x 95 mm working target.
ZONE_BOUNDS={
 'K1':(5,8,35,39),'K2':(36,8,67,39),'K3':(68,8,99,39),'K4':(100,8,140,39),
 'U5':(5,40,43,70),'U6':(38,42,62,69),'U1':(58,42,102,90),'U2':(98,42,140,78),
 'U3':(24,66,50,91),'U4':(48,66,72,91),'J_HMI':(70,70,101,92),'U7':(82,68,108,91),
 'U_DI1':(99,70,112,90),'U_DI2':(108,70,122,90),'U_DI3':(118,70,132,90),'U_DI4':(128,70,140,90),
 'U_RTC':(80,43,105,65),'J_SD':(108,40,139,57),'J_USB':(126,31,143,54),
}


def cluster_for(old):
    u=old.upper()
    for n in range(1,5):
        if f'RELAY{n}' in u or f'RLY{n}' in u or re.search(rf'(FLY|LED|_G|_PD){n}(?:\D|$)',u): return f'K{n}'
    if 'RS485A' in u: return 'U3'
    if 'RS485B' in u: return 'U4'
    if 'HMI' in u or 'RS232' in u: return 'U7' if 'RS232' in u else 'J_HMI'
    if 'RTC' in u or old=='BT1': return 'U_RTC'
    if '_SD' in u or u.startswith('R_SD') or u.startswith('C_SD') or old=='J_SD': return 'J_SD'
    if 'DI1' in u: return 'U_DI1'
    if 'DI2' in u: return 'U_DI2'
    if 'DI3' in u: return 'U_DI3'
    if 'DI4' in u: return 'U_DI4'
    if 'ETH' in u or old in ('Y1','R_EXRES','C_TOCAP','C_1V2','C_XI','C_XO','R_XTAL','R_PM0','R_PM1','R_PM2'): return 'U2'
    if 'USB' in u or 'CC' in u: return 'J_USB'
    if any(k in u for k in ('BUCK','VIN','REV','DTVS','CIN','COUT','COMP','FB','_RT')) or old in ('F1','L1','L2'): return 'U5'
    if old.startswith('C3_') or 'BUCK3' in u: return 'U6'
    if any(k in u for k in ('MCU','BOOT','STATUS','_EN')): return 'U1'
    return 'U1'

SLOTS=[
 (-9,-8),(-5,-8),(0,-8),(5,-8),(9,-8),(-10,-4),(-6,-4),(6,-4),(10,-4),
 (-10,4),(-6,4),(6,4),(10,4),(-9,8),(-5,8),(0,8),(5,8),(9,8),
 (-13,-10),(-13,-5),(-13,0),(-13,5),(-13,10),(13,-10),(13,-5),(13,0),(13,5),(13,10),
 (-18,-10),(-18,-5),(-18,0),(-18,5),(18,-10),(18,-5),(18,0),(18,5),
]


def bbox_fits_board(box):
    x0=pcbnew.ToMM(box.GetX()); y0=pcbnew.ToMM(box.GetY())
    x1=pcbnew.ToMM(box.GetRight()); y1=pcbnew.ToMM(box.GetBottom())
    return x0>=EDGE_MARGIN and y0>=EDGE_MARGIN and x1<=BOARD_X-EDGE_MARGIN and y1<=BOARD_Y-EDGE_MARGIN


def try_position(fp,x,y,placed):
    fp.SetPosition(mm(x,y))
    box=fp.GetBoundingBox().GetInflated(pcbnew.FromMM(PLACEMENT_CLEARANCE))
    if not bbox_fits_board(box): return False
    for other in placed:
        ob=other.GetBoundingBox().GetInflated(pcbnew.FromMM(PLACEMENT_CLEARANCE))
        if box.Intersects(ob): return False
    return True


def zone_candidates(anchor_old, base):
    ax=pcbnew.ToMM(base.x); ay=pcbnew.ToMM(base.y)
    xmin,ymin,xmax,ymax=ZONE_BOUNDS.get(anchor_old,(3,3,BOARD_X-3,BOARD_Y-3))
    pts=[]
    step=2.5
    x=xmin
    while x<=xmax+1e-6:
        y=ymin
        while y<=ymax+1e-6:
            pts.append((x,y)); y+=step
        x+=step
    pts.sort(key=lambda p:(abs(p[0]-ax)+abs(p[1]-ay),abs(p[1]-ay),abs(p[0]-ax),p[1],p[0]))
    return pts


def main():
    print('pcbnew', pcbnew.GetBuildVersion())
    comps,nets,pin_nets=parse_netlist()
    board=pcbnew.BOARD(); board.SetFileName(str(OUT)); board.SetCopperLayerCount(4)
    board.GetDesignSettings().SetBoardThickness(pcbnew.FromMM(1.6))
    add_edge(board,(0,0),(BOARD_X,0)); add_edge(board,(BOARD_X,0),(BOARD_X,BOARD_Y)); add_edge(board,(BOARD_X,BOARD_Y),(0,BOARD_Y)); add_edge(board,(0,BOARD_Y),(0,0))

    netinfo={}
    for name in sorted(nets):
        if name.startswith('unconnected-('): continue
        ni=pcbnew.NETINFO_ITEM(board,name); board.Add(ni); netinfo[name]=ni

    fps={}; unresolved=[]
    for ref,c in comps.items():
        fp_id=c['footprint']
        try: fp=load_fp(fp_id)
        except Exception as exc:
            unresolved.append((ref,fp_id,str(exc))); continue
        fp.SetReference(ref); fp.SetValue(c['value'])
        old=INV_REF.get(ref,ref)
        if optional_semantic(old): fp.SetDNP(True)
        board.Add(fp); fps[ref]=fp
        by_num={p.GetNumber():p for p in fp.Pads()}
        for (nref,pin),name in pin_nets.items():
            if nref!=ref or name.startswith('unconnected-('): continue
            p=by_num.get(pin)
            if p is None:
                raise RuntimeError(f'{ref} footprint {fp_id} has no pad {pin} required by schematic net {name}')
            p.SetNet(netinfo[name])
    if unresolved:
        for row in unresolved: print('UNRESOLVED_FOOTPRINT',*row,file=sys.stderr)
        raise SystemExit(f'{len(unresolved)} footprints unresolved')

    # Place fixed anchors first and fail if the chosen industrial anchor layout
    # itself physically collides or leaves the board working envelope.
    placed=[]
    for ref,fp in fps.items():
        old=INV_REF.get(ref,ref)
        if old in FIXED:
            x,y,rot=FIXED[old]; fp.SetOrientationDegrees(rot)
            if not try_position(fp,x,y,placed):
                raise RuntimeError(f'fixed anchor collision/out-of-board: {ref}/{old} at {(x,y,rot)}')
            placed.append(fp)

    # Deterministic satellite placement around the appropriate functional anchor.
    slot_used=defaultdict(int)
    for ref,fp in fps.items():
        if fp in placed: continue
        old=INV_REF.get(ref,ref); anchor_old=cluster_for(old); anchor_ref=REF_MAP.get(anchor_old,anchor_old)
        anchor=fps.get(anchor_ref)
        if anchor is None: anchor=fps[REF_MAP.get('U1','U1')]
        base=anchor.GetPosition(); idx=slot_used[anchor_ref]; slot_used[anchor_ref]+=1
        candidates=[]
        for attempt in range(len(SLOTS)*3):
            dx,dy=SLOTS[(idx+attempt)%len(SLOTS)]; ring=1+(idx+attempt)//len(SLOTS)
            candidates.append((pcbnew.ToMM(base.x)+dx*ring, pcbnew.ToMM(base.y)+dy*ring))
        candidates.extend(zone_candidates(anchor_old,base))
        chosen=None
        seen=set()
        for x,y in candidates:
            key=(round(x,3),round(y,3))
            if key in seen: continue
            seen.add(key)
            if try_position(fp,x,y,placed): chosen=(x,y); break
        if chosen is None:
            raise RuntimeError(f'no collision-free placement slot for {ref}/{old} in functional zone {anchor_old}')
        placed.append(fp)

    # Board-only M3 mounting holes. Keep outside functional component area.
    for n,(x,y) in enumerate(((5,5),(140,5),(5,90),(140,90)),1):
        fp=load_fp('MountingHole:MountingHole_3.2mm_M3'); fp.SetReference(f'H{n}'); fp.SetValue('M3'); fp.SetBoardOnly(True)
        if not try_position(fp,x,y,placed): raise RuntimeError(f'M3 mounting hole H{n} collides at {(x,y)}')
        board.Add(fp); placed.append(fp)

    board.BuildConnectivity()
    pcbnew.SaveBoard(str(OUT),board)
    print(f'PCB placement generated: {OUT}')
    print(f'footprints={len(list(board.Footprints()))} nets={len(netinfo)} copper_layers={board.GetCopperLayerCount()}')
    print('H2 placement stage: generated; routing/DRC completion not yet claimed')

if __name__=='__main__': main()
