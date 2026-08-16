#!/usr/bin/env python3
"""Rev-A mechanical finalizer.

Field/user-facing connectors and safety-critical anchors are locked only after
KiCad-10 geometry proof. ESP32-S3 is treated as an RF edge module: its physical
body/pads stay clear of components while the antenna keepout/courtyard may
project past the PCB edge. Critical USB and Ethernet routing corridors are kept
free of unrelated component bodies before routing starts.

All functional IC anchors are fixed before dependent passives are placed. The
base builder's wide fallback slots are disabled here so support parts cannot
escape their functional zones into the relay/contact side of the board.
"""
import re
import pcbnew
import build_reva_pcb as b

MECH_CLEARANCE = 0.15
RF_BODY_MARGIN = 2.0
PAD_EDGE_MARGIN = 0.15
MOUNTING_KEEP_OUTS = ((5.0,5.0,4.3),(140.0,5.0,4.3),(5.0,90.0,4.3),(140.0,90.0,4.3))
EDGE_OVERHANG = {'J_ETH','J_USB'}
RF_EDGE = {'U1'}

USB_CORRIDORS = (
    (18.0, 51.5, 137.5, 54.8),
    (16.0, 51.5, 30.0, 70.0),
    (132.0, 40.0, 143.5, 55.0),
)
ETH_CORRIDOR = (110.5, 56.0, 134.0, 70.0)
USB_ALLOWED = {'U1','J_USB','J_ETH','R_MCU_DM_SER','R_MCU_DP_SER','C_MCU_DM_USB','C_MCU_DP_USB'}
ETH_ALLOWED = {'U2','J_ETH'}

b.FIXED.clear()
b.FIXED.update({
    'J_PWR':(14.0,87.5,0), 'J_RS485A':(37.0,87.5,0), 'J_RS485B':(59.0,87.5,0),
    'J_HMI':(83.0,89.5,0), 'J_RS232':(101.0,89.5,0), 'J_DI':(119.5,79.5,0),
    'J_ETH':(132.5,65.0,90), 'J_USB':(142.5,42.0,90), 'J_SD':(119.5,42.5,90),
    'J_RLY1':(20.0,15.5,180), 'J_RLY2':(52.0,7.5,180),
    'J_RLY3':(84.0,7.5,180), 'J_RLY4':(116.0,7.5,180),
    'K1':(20.0,24.0,0), 'K2':(52.0,18.0,0), 'K3':(84.0,18.0,0), 'K4':(116.0,18.0,0),
    'Q1':(13.0,32.0,0), 'Q2':(52.0,35.0,0), 'Q3':(84.0,35.0,0), 'Q4':(109.0,32.0,0),
    'U5':(20.0,75.0,0), 'U6':(52.0,62.0,0),
    'U3':(38.0,78.0,0), 'U4':(60.0,78.0,0),
    'U7':(84.0,78.0,0), 'U_RTC':(88.0,47.0,0),
    'BT1':(80.0,64.0,0), 'Y_RTC':(96.0,47.0,0),
    'U_DI1':(96.0,82.0,0), 'U_DI2':(107.0,82.0,0),
    'U_DI3':(113.0,88.0,0), 'U_DI4':(124.0,88.0,0),
    'U2':(116.0,64.0,0), 'Y1':(106.0,64.0,0),
    'R_MCU_DM_SER':(23.5,64.0,0), 'R_MCU_DP_SER':(23.5,66.0,0),
    'C_MCU_DM_USB':(27.0,62.5,0), 'C_MCU_DP_USB':(27.0,67.5,0),
})


def _merge_pad_boxes(fp):
    box=None
    for pad in fp.Pads():
        pb=pad.GetBoundingBox()
        if box is None: box=pb
        else: box.Merge(pb)
    return box


def _physical_box(fp):
    try:
        fp.BuildCourtyardCaches(); cy=fp.GetCourtyard(pcbnew.F_CrtYd)
        if cy is not None and cy.OutlineCount()>0: return cy.BBox()
    except Exception: pass
    pbox=_merge_pad_boxes(fp)
    return pbox if pbox is not None else fp.GetBoundingBox()


def _rf_body_box(fp):
    pbox=_merge_pad_boxes(fp)
    if pbox is None: return fp.GetBoundingBox()
    return pbox.GetInflated(pcbnew.FromMM(RF_BODY_MARGIN))


def _collision_box(fp,old=None):
    if old is None: old=b.INV_REF.get(fp.GetReference(),fp.GetReference())
    if old in RF_EDGE: return _rf_body_box(fp)
    return _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))


def _inside_board(box,margin):
    return (pcbnew.ToMM(box.GetLeft())>=margin and pcbnew.ToMM(box.GetTop())>=margin and
            pcbnew.ToMM(box.GetRight())<=b.BOARD_X-margin and pcbnew.ToMM(box.GetBottom())<=b.BOARD_Y-margin)


def _box_hits_rect(box, rect):
    x0=pcbnew.ToMM(box.GetLeft()); y0=pcbnew.ToMM(box.GetTop())
    x1=pcbnew.ToMM(box.GetRight()); y1=pcbnew.ToMM(box.GetBottom())
    rx0,ry0,rx1,ry1=rect
    return not (x1 <= rx0 or x0 >= rx1 or y1 <= ry0 or y0 >= ry1)


def _hits_mount_keepout(box):
    x0=pcbnew.ToMM(box.GetLeft()); y0=pcbnew.ToMM(box.GetTop())
    x1=pcbnew.ToMM(box.GetRight()); y1=pcbnew.ToMM(box.GetBottom())
    for cx,cy,r in MOUNTING_KEEP_OUTS:
        nx=min(max(cx,x0),x1); ny=min(max(cy,y0),y1)
        if (nx-cx)**2+(ny-cy)**2 < r*r: return True
    return False


def _is_mounting_hole(old,fp):
    ref=fp.GetReference() or ''
    return old.startswith('H') or ref.startswith('H') or 'MountingHole' in fp.GetFPIDAsString()


def _try_position(fp,x,y,placed):
    fp.SetPosition(b.mm(x,y))
    old=b.INV_REF.get(fp.GetReference(),fp.GetReference())
    box=_collision_box(fp,old)
    if old in EDGE_OVERHANG or old in RF_EDGE:
        pbox=_merge_pad_boxes(fp)
        if pbox is None or not _inside_board(pbox,PAD_EDGE_MARGIN): return False
    elif not _inside_board(box,b.EDGE_MARGIN): return False
    if not _is_mounting_hole(old,fp) and _hits_mount_keepout(box): return False
    if old not in USB_ALLOWED and any(_box_hits_rect(box,r) for r in USB_CORRIDORS): return False
    if old not in ETH_ALLOWED and _box_hits_rect(box,ETH_CORRIDOR): return False
    for other in placed:
        oold=b.INV_REF.get(other.GetReference(),other.GetReference())
        if box.Intersects(_collision_box(other,oold)): return False
    return True


def _footprint_id(old):
    comps,_,_=b.parse_netlist(); ref=b.REF_MAP.get(old,old)
    if ref not in comps or not comps[ref]['footprint']: raise RuntimeError(f'cannot resolve footprint for {old}/{ref}')
    return comps[ref]['footprint']


def _fixed_obstacle_boxes(exclude=None):
    out=[]
    for old,(x,y,rot) in b.FIXED.items():
        if old==exclude: continue
        fp=b.load_fp(_footprint_id(old)); fp.SetReference('TMP')
        fp.SetOrientationDegrees(rot); fp.SetPosition(b.mm(x,y))
        out.append((old,_collision_box(fp,old)))
    return out


def _autofit_esp32_rf_edge():
    fp=b.load_fp(_footprint_id('U1')); fp.SetReference('TMP')
    obstacles=_fixed_obstacle_boxes('U1')
    fp.SetOrientationDegrees(0); fp.SetPosition(b.mm(50,50))
    pbox=_merge_pad_boxes(fp); cbox=_physical_box(fp)
    print('ESP32 geometry: pads=%.1fx%.1fmm courtyard=%.1fx%.1fmm' % (
        pcbnew.ToMM(pbox.GetWidth()),pcbnew.ToMM(pbox.GetHeight()),
        pcbnew.ToMM(cbox.GetWidth()),pcbnew.ToMM(cbox.GetHeight())))
    candidates=[]
    for rot in (90,270,0,180):
        x=1.0
        while x<=55.0+1e-9:
            y=37.0
            while y<=76.0+1e-9:
                score=(0 if rot in (90,270) else 30)+x+abs(y-56.0)*0.08
                candidates.append((score,x,y,rot)); y+=0.5
            x+=0.5
    candidates.sort(key=lambda r:r[0])
    for _,x,y,rot in candidates:
        fp.SetOrientationDegrees(rot); fp.SetPosition(b.mm(x,y))
        pbox=_merge_pad_boxes(fp)
        if pbox is None or not _inside_board(pbox,PAD_EDGE_MARGIN): continue
        body=_rf_body_box(fp)
        if _hits_mount_keepout(body): continue
        if any(body.Intersects(ob) for _,ob in obstacles): continue
        b.FIXED['U1']=(round(x,3),round(y,3),rot)
        print(f'ESP32 RF-edge anchor: x={x:.1f} y={y:.1f} rot={rot} body/pads-clear=PASS')
        return
    raise RuntimeError('ESP32 RF-edge auto-fit failed after separating antenna keepout from body collision')


b.ZONE_BOUNDS.update({
    'Q1':(5,30,31,39), 'Q2':(39,30,65,39), 'Q3':(71,30,97,39), 'Q4':(101,30,134,39),
    'U1':(2,36,58,82),
    'U5':(5.5,68.5,43,86),
    'U6':(38,55.5,76,72),
    'U3':(28,70,47,85), 'U4':(50,70,69,85), 'U7':(72,69,96,85),
    'U_RTC':(78,42,103,50.8), 'U2':(92,55,107,75),
    'U_DI1':(94,72,104,88), 'U_DI2':(104,72,114,88),
    'U_DI3':(112,78,122,91), 'U_DI4':(122,78,135,91),
    'J_SD':(108,40,131,50.8), 'J_USB':(126,31,143,50.8),
})

_ORIGINAL_CLUSTER = b.cluster_for

def _cluster_for_final(old):
    u=old.upper()
    # USB/field ideal-diode OR parts belong to the 5 V power architecture, not
    # the USB data/ESD connector cluster.
    if old in ('D_OR_USB','D_OR_FIELD') or '_OR_USB' in u or '_OR_FIELD' in u:
        return 'U5'
    if old.startswith('C3_') or 'BUCK3' in u or old=='L2':
        return 'U6'
    for n in range(1,5):
        if re.search(rf'(FLY|LED|_G|_PD){n}(?:\D|$)',u):
            return f'Q{n}'
    return _ORIGINAL_CLUSTER(old)


def _dense_zone_candidates(anchor_old,base):
    ax=pcbnew.ToMM(base.x); ay=pcbnew.ToMM(base.y)
    xmin,ymin,xmax,ymax=b.ZONE_BOUNDS.get(anchor_old,(3,30,b.BOARD_X-3,b.BOARD_Y-3))
    step=1.0 if anchor_old in ('U1','U2','U5','U6','U7','U_RTC','U_DI1','U_DI2','U_DI3','U_DI4','Q1','Q2','Q3','Q4') else 1.5
    pts=[]; x=xmin
    while x<=xmax+1e-6:
        y=ymin
        while y<=ymax+1e-6: pts.append((x,y)); y+=step
        x+=step
    pts.sort(key=lambda p:(abs(p[0]-ax)+abs(p[1]-ay),abs(p[1]-ay),abs(p[0]-ax),p[1],p[0]))
    return pts


_autofit_esp32_rf_edge()
b.try_position=_try_position
b.cluster_for=_cluster_for_final
b.zone_candidates=_dense_zone_candidates
b.SLOTS=[]

if __name__=='__main__': b.main()
