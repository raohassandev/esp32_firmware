#!/usr/bin/env python3
"""Rev-A mechanical finalizer.

Field/user-facing connectors and safety-critical anchors are locked only after
KiCad-10 geometry proof. ESP32-S3 is treated as an RF edge module: its copper
pads/module stay on the PCB while the antenna courtyard is allowed to project
past the board edge. Power and optional logic are packed after the RF anchor so
we do not sacrifice antenna placement to an arbitrary early buck position.
"""
import pcbnew
import build_reva_pcb as b

MECH_CLEARANCE = 0.15
PAD_EDGE_MARGIN = 0.15
MOUNTING_KEEP_OUTS = ((5.0,5.0,4.3),(140.0,5.0,4.3),(5.0,90.0,4.3),(140.0,90.0,4.3))
EDGE_OVERHANG = {'J_ETH','J_USB'}
RF_EDGE = {'U1'}

# Coordinates below are outputs of previous KiCad-10 courtyard/pad checks.
# U5 is intentionally not fixed: it must yield to the ESP32 RF edge placement.
b.FIXED.clear()
b.FIXED.update({
    'J_PWR':(14.0,87.5,0),
    'J_RS485A':(37.0,87.5,0),
    'J_RS485B':(59.0,87.5,0),
    'J_HMI':(83.0,89.5,0),
    'J_RS232':(101.0,89.5,0),
    'J_DI':(119.5,79.5,0),
    'J_ETH':(132.5,65.0,90),
    'J_USB':(142.5,42.0,90),
    'J_SD':(119.5,42.5,90),
    'J_RLY1':(20.0,15.5,180),
    'J_RLY2':(52.0,7.5,180),
    'J_RLY3':(84.0,7.5,180),
    'J_RLY4':(116.0,7.5,180),
    'K1':(20.0,24.0,0),
    'K2':(52.0,18.0,0),
    'K3':(84.0,18.0,0),
    'K4':(116.0,18.0,0),
    'Q1':(13.0,32.0,0),
    'Q2':(52.0,35.0,0),
    'Q3':(84.0,35.0,0),
    'Q4':(109.0,32.0,0),
    'U3':(38.0,78.0,0),       # RS485-A
    'U4':(60.0,78.0,0),       # RS485-B
    'U2':(116.0,64.0,0),      # W5500
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
        fp.BuildCourtyardCaches()
        cy=fp.GetCourtyard(pcbnew.F_CrtYd)
        if cy is not None and cy.OutlineCount()>0:
            return cy.BBox()
    except Exception:
        pass
    pbox=_merge_pad_boxes(fp)
    return pbox if pbox is not None else fp.GetBoundingBox()


def _inside_board(box,margin):
    return (
        pcbnew.ToMM(box.GetLeft())>=margin and
        pcbnew.ToMM(box.GetTop())>=margin and
        pcbnew.ToMM(box.GetRight())<=b.BOARD_X-margin and
        pcbnew.ToMM(box.GetBottom())<=b.BOARD_Y-margin
    )


def _hits_mount_keepout(box):
    x0=pcbnew.ToMM(box.GetLeft()); y0=pcbnew.ToMM(box.GetTop())
    x1=pcbnew.ToMM(box.GetRight()); y1=pcbnew.ToMM(box.GetBottom())
    for cx,cy,r in MOUNTING_KEEP_OUTS:
        nx=min(max(cx,x0),x1); ny=min(max(cy,y0),y1)
        if (nx-cx)**2+(ny-cy)**2 < r*r:
            return True
    return False


def _try_position(fp,x,y,placed):
    fp.SetPosition(b.mm(x,y))
    old=b.INV_REF.get(fp.GetReference(),fp.GetReference())
    box=_physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
    if old in EDGE_OVERHANG or old in RF_EDGE:
        # Connector/RF edge footprints may have mechanical/antenna courtyard
        # outside the outline, but solder pads must remain on-board.
        pbox=_merge_pad_boxes(fp)
        if pbox is None or not _inside_board(pbox,PAD_EDGE_MARGIN):
            return False
    elif not _inside_board(box,b.EDGE_MARGIN):
        return False
    if _hits_mount_keepout(box):
        return False
    for other in placed:
        ob=_physical_box(other).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
        if box.Intersects(ob):
            return False
    return True


def _footprint_id(old):
    comps,_,_=b.parse_netlist()
    ref=b.REF_MAP.get(old,old)
    if ref not in comps or not comps[ref]['footprint']:
        raise RuntimeError(f'cannot resolve footprint for {old}/{ref}')
    return comps[ref]['footprint']


def _fixed_obstacle_boxes(exclude=None):
    out=[]
    for old,(x,y,rot) in b.FIXED.items():
        if old==exclude:
            continue
        fp=b.load_fp(_footprint_id(old)); fp.SetReference('TMP')
        fp.SetOrientationDegrees(rot); fp.SetPosition(b.mm(x,y))
        out.append((old,_physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))))
    return out


def _autofit_esp32_rf_edge():
    """Place ESP32-S3-WROOM at the left board edge with antenna facing outward.

    90/270 degree rotations are tried first. Full courtyard collision is checked
    against every fixed field anchor, while only copper pads are required to be
    inside the PCB so the RF antenna courtyard can extend beyond the edge.
    """
    fp=b.load_fp(_footprint_id('U1')); fp.SetReference('TMP')
    obstacles=_fixed_obstacle_boxes('U1')
    candidates=[]
    for rot in (90,270,0,180):
        x=1.0
        while x<=48.0+1e-9:
            y=39.0
            while y<=76.0+1e-9:
                # Strong preference: smallest x (antenna at left edge), then y~56.
                score=(0 if rot in (90,270) else 20)+x+abs(y-56.0)*0.08
                candidates.append((score,x,y,rot))
                y+=0.5
            x+=0.5
    candidates.sort(key=lambda r:r[0])
    for _,x,y,rot in candidates:
        fp.SetOrientationDegrees(rot); fp.SetPosition(b.mm(x,y))
        pbox=_merge_pad_boxes(fp)
        if pbox is None or not _inside_board(pbox,PAD_EDGE_MARGIN):
            continue
        box=_physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
        if _hits_mount_keepout(box):
            continue
        if any(box.Intersects(ob) for _,ob in obstacles):
            continue
        b.FIXED['U1']=(round(x,3),round(y,3),rot)
        print(f'ESP32 RF-edge anchor: x={x:.1f} y={y:.1f} rot={rot} pads-inside/courtyard-clear=PASS')
        return
    raise RuntimeError('ESP32 RF-edge auto-fit failed against locked field anchors')


# Wider zones. Buck is deliberately moved away from the left RF edge.
b.ZONE_BOUNDS.update({
    'U1':(2,38,52,78),
    'U5':(28,40,58,68),
    'U6':(42,40,72,67),
    'U7':(72,66,108,88),
    'U_RTC':(72,42,108,67),
    'U_DI1':(96,68,112,88),
    'U_DI2':(104,68,121,88),
    'U_DI3':(113,68,131,88),
    'U_DI4':(122,68,139,88),
})


def _dense_zone_candidates(anchor_old,base):
    ax=pcbnew.ToMM(base.x); ay=pcbnew.ToMM(base.y)
    xmin,ymin,xmax,ymax=b.ZONE_BOUNDS.get(anchor_old,(3,3,b.BOARD_X-3,b.BOARD_Y-3))
    step=1.0 if anchor_old in ('U1','U5','U6','U7','U_RTC','U_DI1','U_DI2','U_DI3','U_DI4') else 2.0
    pts=[]; x=xmin
    while x<=xmax+1e-6:
        y=ymin
        while y<=ymax+1e-6:
            pts.append((x,y)); y+=step
        x+=step
    pts.sort(key=lambda p:(abs(p[0]-ax)+abs(p[1]-ay),abs(p[1]-ay),abs(p[0]-ax),p[1],p[0]))
    return pts


_autofit_esp32_rf_edge()
b.try_position=_try_position
b.zone_candidates=_dense_zone_candidates

if __name__=='__main__':
    b.main()
