#!/usr/bin/env python3
"""Tie every safe F.Cu GND fill island to the solid L2 GND reference.

Surface pours are intentionally allowed to fracture around dense signal routing.
Each pad-connected filled outline receives either an ordinary through-via inside
the island or, for narrow fragments, a short DRC-aware F.Cu GND tail to a safe
through-via. This closes the island onto the continuous In1.Cu reference without
placing vias inside footprint/rule-area keepouts.
"""
from pathlib import Path
import math
import sys
import pcbnew

VIA_D=0.60
DRILL=0.30
CLEAR=0.24
TRACK_W=0.20
EDGE=1.0
LOGIC_Y0=30.0
MAG_X0=127.0
MAG_Y0=55.5
MAG_Y1=74.5
TAIL_RADIUS=4.0
TAIL_STEP=0.25


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


def orient(ax,ay,bx,by,cx,cy):
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax)


def on_segment(ax,ay,bx,by,cx,cy,eps=1e-9):
    return (min(ax,bx)-eps<=cx<=max(ax,bx)+eps and min(ay,by)-eps<=cy<=max(ay,by)+eps)


def segments_intersect(a,b,c,d):
    ax,ay=a; bx,by=b; cx,cy=c; dx,dy=d
    o1=orient(ax,ay,bx,by,cx,cy); o2=orient(ax,ay,bx,by,dx,dy)
    o3=orient(cx,cy,dx,dy,ax,ay); o4=orient(cx,cy,dx,dy,bx,by)
    eps=1e-9
    if ((o1>eps and o2<-eps) or (o1<-eps and o2>eps)) and ((o3>eps and o4<-eps) or (o3<-eps and o4>eps)):
        return True
    if abs(o1)<=eps and on_segment(ax,ay,bx,by,cx,cy): return True
    if abs(o2)<=eps and on_segment(ax,ay,bx,by,dx,dy): return True
    if abs(o3)<=eps and on_segment(cx,cy,dx,dy,ax,ay): return True
    if abs(o4)<=eps and on_segment(cx,cy,dx,dy,bx,by): return True
    return False


def seg_seg_dist(a,b,c,d):
    if segments_intersect(a,b,c,d): return 0.0
    return min(
        point_seg_dist(a[0],a[1],c[0],c[1],d[0],d[1]),
        point_seg_dist(b[0],b[1],c[0],c[1],d[0],d[1]),
        point_seg_dist(c[0],c[1],a[0],a[1],b[0],b[1]),
        point_seg_dist(d[0],d[1],a[0],a[1],b[0],b[1]),
    )


def segment_hits_box(a,b,bb):
    x0=pcbnew.ToMM(bb.GetLeft()); x1=pcbnew.ToMM(bb.GetRight())
    y0=pcbnew.ToMM(bb.GetTop()); y1=pcbnew.ToMM(bb.GetBottom())
    ax,ay=a; bx,by=b
    if x0<=ax<=x1 and y0<=ay<=y1: return True
    if x0<=bx<=x1 and y0<=by<=y1: return True
    edges=[((x0,y0),(x1,y0)),((x1,y0),(x1,y1)),((x1,y1),(x0,y1)),((x0,y1),(x0,y0))]
    return any(segments_intersect(a,b,c,d) for c,d in edges)


def rule_keepouts(board, kind):
    out=[]
    try:
        zones=list(board.Zones())
    except Exception:
        zones=[board.GetArea(i) for i in range(board.GetAreaCount())]
    for fp in board.Footprints():
        try: zones.extend(list(fp.Zones()))
        except Exception: pass
    for z in zones:
        try:
            if not z.GetIsRuleArea(): continue
            blocked=(z.GetDoNotAllowVias() if kind=='via' else z.GetDoNotAllowTracks())
            if blocked: out.append(z)
        except Exception:
            continue
    return out


def safe(board,x,y,gcode,via_keepouts):
    if x<EDGE or x>145.0-EDGE or y<LOGIC_Y0+0.35 or y>95.0-EDGE: return False
    if x>MAG_X0-0.6 and MAG_Y0-0.6<y<MAG_Y1+0.6: return False
    pos=pt(x,y)
    for z in via_keepouts:
        bb=z.GetBoundingBox(); bb.Inflate(mm(VIA_D/2))
        if bb.Contains(pos): return False
    radius=VIA_D/2+CLEAR
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetCode()==gcode: continue
            bb=pad.GetBoundingBox(); bb.Inflate(mm(radius))
            if bb.Contains(pos): return False
    for item in board.GetTracks():
        if item.GetNetCode()==gcode: continue
        if isinstance(item,pcbnew.PCB_VIA):
            ix,iy=xy(item.GetPosition()); req=radius+pcbnew.ToMM(item.GetWidth())/2
            if math.hypot(x-ix,y-iy)<req: return False
        else:
            ax,ay=xy(item.GetStart()); bx,by=xy(item.GetEnd()); req=radius+pcbnew.ToMM(item.GetWidth())/2
            if point_seg_dist(x,y,ax,ay,bx,by)<req: return False
    return True


def track_safe(board,a,b,gcode,track_keepouts):
    radius=TRACK_W/2+CLEAR
    for z in track_keepouts:
        bb=z.GetBoundingBox(); bb.Inflate(mm(radius))
        if segment_hits_box(a,b,bb): return False
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetCode()==gcode: continue
            try:
                if not pad.IsOnLayer(pcbnew.F_Cu): continue
            except Exception:
                pass
            bb=pad.GetBoundingBox(); bb.Inflate(mm(radius))
            if segment_hits_box(a,b,bb): return False
    for item in board.GetTracks():
        if item.GetNetCode()==gcode: continue
        if isinstance(item,pcbnew.PCB_VIA):
            p=xy(item.GetPosition()); req=radius+pcbnew.ToMM(item.GetWidth())/2
            if point_seg_dist(p[0],p[1],a[0],a[1],b[0],b[1])<req: return False
        else:
            try:
                if item.GetLayer()!=pcbnew.F_Cu: continue
            except Exception:
                continue
            c=xy(item.GetStart()); d=xy(item.GetEnd()); req=radius+pcbnew.ToMM(item.GetWidth())/2
            if seg_seg_dist(a,b,c,d)<req: return False
    return True


def bounds(outline):
    pts=[xy(outline.CPoint(i)) for i in range(outline.PointCount())]
    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
    return min(xs),max(xs),min(ys),max(ys)


def candidates(outline, step=0.5, inset=0.35):
    minx,maxx,miny,maxy=bounds(outline); cx=(minx+maxx)/2; cy=(miny+maxy)/2
    cand=[]; x=minx+inset
    while x<=maxx-inset+1e-9:
        y=miny+inset
        while y<=maxy-inset+1e-9:
            cand.append((math.hypot(x-cx,y-cy),x,y)); y+=step
        x+=step
    cand.append((0.0,cx,cy)); cand.sort()
    for _,x,y in cand:
        try: inside=outline.PointInside(pt(x,y))
        except Exception: inside=False
        if inside: yield x,y


def choose_candidate(board,outline,gcode,via_keepouts):
    for x,y in candidates(outline,0.5,0.35):
        if safe(board,x,y,gcode,via_keepouts): return (x,y)
    for x,y in candidates(outline,0.20,0.31):
        if safe(board,x,y,gcode,via_keepouts): return (x,y)
    return None


def gnd_pads_in_outline(board,outline,gcode):
    out=[]
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetCode()!=gcode: continue
            try: inside=outline.PointInside(pad.GetPosition())
            except Exception: inside=False
            if inside: out.append(pad)
    return out


def tail_points(px,py):
    cand=[]; dx=-TAIL_RADIUS
    while dx<=TAIL_RADIUS+1e-9:
        dy=-TAIL_RADIUS
        while dy<=TAIL_RADIUS+1e-9:
            d=math.hypot(dx,dy)
            if 0.75<=d<=TAIL_RADIUS: cand.append((d,px+dx,py+dy))
            dy+=TAIL_STEP
        dx+=TAIL_STEP
    cand.sort(key=lambda r:(r[0],abs(r[2]-py),abs(r[1]-px),r[2],r[1]))
    for _,x,y in cand: yield x,y


def choose_tail(board,outline,gcode,via_keepouts,track_keepouts):
    for pad in gnd_pads_in_outline(board,outline,gcode):
        a=xy(pad.GetPosition())
        for x,y in tail_points(*a):
            b=(x,y)
            if not safe(board,x,y,gcode,via_keepouts): continue
            if not track_safe(board,a,b,gcode,track_keepouts): continue
            return pad,a,b
    return None


def add_via(board,x,y,net):
    v=pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x,y)); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(DRILL))
    v.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu); v.SetNet(net); v.SetLocked(True); board.Add(v)


def add_track(board,a,b,net):
    t=pcbnew.PCB_TRACK(board); t.SetStart(pt(*a)); t.SetEnd(pt(*b)); t.SetWidth(mm(TRACK_W))
    t.SetLayer(pcbnew.F_Cu); t.SetNet(net); t.SetLocked(True); board.Add(t)


def pad_label(pad):
    try: return f'{pad.GetParent().GetReference()}:{pad.GetNumber()}'
    except Exception: return f'pad:{pad.GetNumber()}'


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
    try: zones=list(board.Zones())
    except Exception: zones=[board.GetArea(i) for i in range(board.GetAreaCount())]
    gz=[z for z in zones if z.GetNetCode()==gcode and z.GetLayer()==layer and z.HasFilledPolysForLayer(layer)]
    if not gz: raise SystemExit('filled F.Cu GND zone missing; refill zones before stitching')
    via_keepouts=rule_keepouts(board,'via'); track_keepouts=rule_keepouts(board,'track')

    added=0; tails=0; skipped=0; outlines=0
    for z in gz:
        polys=z.GetFilledPolysList(layer)
        for idx in range(polys.OutlineCount()):
            outlines+=1; outline=polys.COutline(idx)
            chosen=choose_candidate(board,outline,gcode,via_keepouts)
            if chosen is not None:
                add_via(board,chosen[0],chosen[1],gnet); added+=1; continue
            tail=choose_tail(board,outline,gcode,via_keepouts,track_keepouts)
            if tail is not None:
                pad,a,b=tail; add_track(board,a,b,gnet); add_via(board,b[0],b[1],gnet)
                added+=1; tails+=1
                print(f'GND_ISLAND_TAIL idx={idx} pad={pad_label(pad)} from=({a[0]:.3f},{a[1]:.3f}) via=({b[0]:.3f},{b[1]:.3f})')
                continue
            skipped+=1; minx,maxx,miny,maxy=bounds(outline)
            print(f'GND_ISLAND_SKIP idx={idx} bounds=({minx:.3f},{miny:.3f})-({maxx:.3f},{maxy:.3f})')
    if added==0: raise SystemExit(f'GND stitch failed: outlines={outlines} no safe candidates')
    pcbnew.SaveBoard(str(path),board)
    print(f'GND_ISLAND_STITCH: outlines={outlines} vias_added={added} tail_routes={tails} skipped={skipped} via_keepouts={len(via_keepouts)} track_keepouts={len(track_keepouts)}')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: stitch_ground_islands.py BOARD.kicad_pcb')
    main(sys.argv[1])
