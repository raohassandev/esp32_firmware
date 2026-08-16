#!/usr/bin/env python3
"""Tie every safe F.Cu GND fill island to the solid L2 GND reference.

Surface pours are intentionally allowed to fracture around dense signal routing.
Each resulting filled outline receives one ordinary through-via where clearance
permits, so it is electrically tied to the continuous In1.Cu reference instead
of remaining a same-net isolated copper island.
"""
from pathlib import Path
import math
import sys
import pcbnew

VIA_D=0.60
DRILL=0.30
CLEAR=0.24
EDGE=1.0
LOGIC_Y0=30.0
MAG_X0=127.0
MAG_Y0=55.5
MAG_Y1=74.5


def mm(v): return pcbnew.FromMM(float(v))
def pt(x,y): return pcbnew.VECTOR2I_MM(float(x),float(y))
def xy(p): return (pcbnew.ToMM(p.x),pcbnew.ToMM(p.y))


def point_seg_dist(px,py,ax,ay,bx,by):
    vx=bx-ax; vy=by-ay; wx=px-ax; wy=py-ay
    vv=vx*vx+vy*vy
    if vv<=1e-12: return math.hypot(px-ax,py-ay)
    t=max(0.0,min(1.0,(wx*vx+wy*vy)/vv))
    qx=ax+t*vx; qy=ay+t*vy
    return math.hypot(px-qx,py-qy)


def safe(board,x,y,gcode):
    if x<EDGE or x>145.0-EDGE or y<LOGIC_Y0+0.6 or y>95.0-EDGE: return False
    if x>MAG_X0-0.6 and MAG_Y0-0.6<y<MAG_Y1+0.6: return False
    radius=VIA_D/2+CLEAR
    pos=pt(x,y)
    # Avoid non-GND pads on every layer. Bounding-box expansion is deliberately
    # conservative; DRC remains the final authority.
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetCode()==gcode: continue
            bb=pad.GetBoundingBox()
            bb.Inflate(mm(radius))
            if bb.Contains(pos): return False
    for item in board.GetTracks():
        if item.GetNetCode()==gcode: continue
        if isinstance(item,pcbnew.PCB_VIA):
            ix,iy=xy(item.GetPosition())
            req=radius+pcbnew.ToMM(item.GetWidth())/2
            if math.hypot(x-ix,y-iy)<req: return False
        else:
            ax,ay=xy(item.GetStart()); bx,by=xy(item.GetEnd())
            req=radius+pcbnew.ToMM(item.GetWidth())/2
            if point_seg_dist(x,y,ax,ay,bx,by)<req: return False
    return True


def candidates(outline):
    pts=[xy(outline.CPoint(i)) for i in range(outline.PointCount())]
    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
    minx,maxx=min(xs),max(xs); miny,maxy=min(ys),max(ys)
    cx=(minx+maxx)/2; cy=(miny+maxy)/2
    # Centre-out deterministic 0.5mm scan within this specific filled outline.
    cand=[]
    x=minx+0.35
    while x<=maxx-0.35+1e-9:
        y=miny+0.35
        while y<=maxy-0.35+1e-9:
            cand.append((math.hypot(x-cx,y-cy),x,y)); y+=0.5
        x+=0.5
    cand.sort()
    for _,x,y in cand:
        p=pt(x,y)
        try:
            inside=outline.PointInside(p)
        except Exception:
            inside=False
        if inside: yield x,y


def add_via(board,x,y,net):
    v=pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x,y)); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(DRILL))
    v.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu); v.SetNet(net); v.SetLocked(True)
    board.Add(v)


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    gnet=None
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname()=='GND': gnet=pad.GetNet(); break
        if gnet: break
    if not gnet: raise SystemExit('GND net missing')
    gcode=gnet.GetNetCode(); layer=pcbnew.F_Cu
    zones=[]
    try: zones=list(board.Zones())
    except Exception: zones=[board.GetArea(i) for i in range(board.GetAreaCount())]
    gz=[z for z in zones if z.GetNetCode()==gcode and z.GetLayer()==layer and z.HasFilledPolysForLayer(layer)]
    if not gz: raise SystemExit('filled F.Cu GND zone missing; refill zones before stitching')

    added=0; skipped=0; outlines=0
    for z in gz:
        polys=z.GetFilledPolysList(layer)
        for idx in range(polys.OutlineCount()):
            outlines+=1; outline=polys.COutline(idx); chosen=None
            for x,y in candidates(outline):
                if safe(board,x,y,gcode): chosen=(x,y); break
            if chosen is None:
                skipped+=1; continue
            add_via(board,chosen[0],chosen[1],gnet); added+=1
    if added==0: raise SystemExit(f'GND stitch failed: outlines={outlines} no safe candidates')
    pcbnew.SaveBoard(str(path),board)
    print(f'GND_ISLAND_STITCH: outlines={outlines} vias_added={added} skipped={skipped}')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: stitch_ground_islands.py BOARD.kicad_pcb')
    main(sys.argv[1])
