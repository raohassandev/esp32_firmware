#!/usr/bin/env python3
"""Mechanical finalizer for Rev-A deterministic PCB placement.

The base builder owns electrical net/footprint creation and functional zoning.
This wrapper owns production-style mechanical fit semantics and resolves the
whole fixed-anchor set before pcbnew placement, avoiding one-connector-at-a-time
CI failure loops.
"""
import math
import pcbnew
import build_reva_pcb as b

b.FIXED.update({
    'J_PWR': (14, 87, 0),
    'J_RS485A': (37, 87, 0),
    'J_RS485B': (59, 87, 0),
    'J_HMI': (83, 87, 0),
    'J_RS232': (101, 87, 0),
    'J_DI': (122, 87, 0),
    'J_ETH': (125, 65, 90),
    'J_USB': (135, 42, 90),
})

EDGE_OVERHANG = {'J_ETH', 'J_USB'}
MECH_CLEARANCE = 0.15
PAD_EDGE_MARGIN = 0.15
MOUNTING_KEEP_OUTS = ((5.0, 5.0, 4.3), (140.0, 5.0, 4.3), (5.0, 90.0, 4.3), (140.0, 90.0, 4.3))


def _merge_pad_boxes(fp):
    box = None
    for pad in fp.Pads():
        pb = pad.GetBoundingBox()
        if box is None:
            box = pb
        else:
            box.Merge(pb)
    return box


def _physical_box(fp):
    try:
        fp.BuildCourtyardCaches()
        cy = fp.GetCourtyard(pcbnew.F_CrtYd)
        if cy is not None and cy.OutlineCount() > 0:
            return cy.BBox()
    except Exception:
        pass
    box = _merge_pad_boxes(fp)
    if box is not None:
        return box
    return fp.GetBoundingBox()


def _inside_board(box, margin):
    x0 = pcbnew.ToMM(box.GetLeft())
    y0 = pcbnew.ToMM(box.GetTop())
    x1 = pcbnew.ToMM(box.GetRight())
    y1 = pcbnew.ToMM(box.GetBottom())
    return x0 >= margin and y0 >= margin and x1 <= b.BOARD_X - margin and y1 <= b.BOARD_Y - margin


def _box_mm(box):
    return (
        pcbnew.ToMM(box.GetLeft()), pcbnew.ToMM(box.GetTop()),
        pcbnew.ToMM(box.GetRight()), pcbnew.ToMM(box.GetBottom()),
    )


def _hits_mount_keepout(box):
    x0, y0, x1, y1 = _box_mm(box)
    for cx, cy, r in MOUNTING_KEEP_OUTS:
        nx = min(max(cx, x0), x1)
        ny = min(max(cy, y0), y1)
        if (nx-cx)**2 + (ny-cy)**2 < r*r:
            return True
    return False


def _try_position(fp, x, y, placed):
    fp.SetPosition(b.mm(x, y))
    old = b.INV_REF.get(fp.GetReference(), fp.GetReference())
    box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))

    if old in EDGE_OVERHANG:
        pbox = _merge_pad_boxes(fp)
        if pbox is None or not _inside_board(pbox, PAD_EDGE_MARGIN):
            return False
    elif not _inside_board(box, b.EDGE_MARGIN):
        return False

    if _hits_mount_keepout(box):
        return False

    for other in placed:
        ob = _physical_box(other).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
        if box.Intersects(ob):
            return False
    return True


def _autofit_edge_anchor(old, fp_id, y, rotation):
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    fp.SetOrientationDegrees(rotation)
    x = b.BOARD_X - 1.0
    while x >= 10.0:
        fp.SetPosition(b.mm(x, y))
        pbox = _merge_pad_boxes(fp)
        if pbox is not None and _inside_board(pbox, PAD_EDGE_MARGIN):
            print(f'edge anchor {old}: x={x:.1f} y={y:.1f} rot={rotation} pads-inside-board=PASS')
            return (round(x, 3), y, rotation)
        x -= 0.5
    raise RuntimeError(f'cannot fit {old} pads inside {b.BOARD_X}x{b.BOARD_Y} mm board')


def _footprint_id_for_semantic(old):
    comps, _, _ = b.parse_netlist()
    canonical = b.REF_MAP.get(old, old)
    if canonical not in comps:
        raise RuntimeError(f'cannot resolve canonical component for {old}')
    fp_id = comps[canonical]['footprint']
    if not fp_id:
        raise RuntimeError(f'{old}/{canonical} has no footprint in validated netlist')
    return fp_id


def _autofit_bottom_anchor(old, x, rotation=180):
    """Fit relay terminal while reserving the fixed M3 corner mounting pattern."""
    fp_id = _footprint_id_for_semantic(old)
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    fp.SetOrientationDegrees(rotation)
    dxs = [0.0]
    for d in (0.5,1.0,1.5,2.0,2.5,3.0,3.5,4.0,4.5,5.0,5.5,6.0,7.0,8.0):
        dxs.extend((d, -d))
    for dx in dxs:
        candidate_x = x + dx
        y = 1.0
        while y <= 18.0:
            fp.SetPosition(b.mm(candidate_x, y))
            box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
            if _inside_board(box, b.EDGE_MARGIN) and not _hits_mount_keepout(box):
                print(f'bottom anchor {old}: x={candidate_x:.1f} y={y:.1f} rot={rotation} board+M3-clear=PASS')
                return (round(candidate_x, 3), round(y, 3), rotation)
            y += 0.5
    raise RuntimeError(f'cannot fit bottom connector {old} inside board while preserving M3 mounting pattern')


def _x_candidates(preferred_x, span=24.0, step=0.5):
    yield float(preferred_x)
    delta = step
    while delta <= span + 1e-9:
        yield float(preferred_x) - delta
        yield float(preferred_x) + delta
        delta += step


def _autofit_top_anchor(old, x, rotation=0, allow_x_search=False):
    fp_id = _footprint_id_for_semantic(old)
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    fp.SetOrientationDegrees(rotation)
    xs = _x_candidates(x) if allow_x_search else (float(x),)
    for candidate_x in xs:
        y = b.BOARD_Y - 1.0
        while y >= b.BOARD_Y - 22.0:
            fp.SetPosition(b.mm(candidate_x, y))
            box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
            if _inside_board(box, b.EDGE_MARGIN) and not _hits_mount_keepout(box):
                print(f'top anchor {old}: x={candidate_x:.1f} y={y:.1f} rot={rotation} courtyard+M3-clear=PASS')
                return (round(candidate_x, 3), round(y, 3), rotation)
            y -= 0.5
    raise RuntimeError(f'cannot fit top connector {old} ({fp_id}) inside board')


def _fixed_obstacles(exclude_old):
    out = []
    for other_old, (x, y, rot) in b.FIXED.items():
        if other_old == exclude_old:
            continue
        try:
            fp_id = _footprint_id_for_semantic(other_old)
            fp = b.load_fp(fp_id)
        except Exception:
            continue
        fp.SetReference('TMP')
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(b.mm(x, y))
        out.append((other_old, _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))))
    return out


def _autofit_internal_anchor(old, preferred_x, preferred_y, rotation, bounds, step=0.5):
    fp_id = _footprint_id_for_semantic(old)
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    fp.SetOrientationDegrees(rotation)
    obstacles = _fixed_obstacles(old)
    xmin, ymin, xmax, ymax = bounds
    candidates = []
    x = xmin
    while x <= xmax + 1e-9:
        y = ymin
        while y <= ymax + 1e-9:
            candidates.append((x, y))
            y += step
        x += step
    candidates.sort(key=lambda p: (abs(p[0]-preferred_x)+abs(p[1]-preferred_y), abs(p[1]-preferred_y), abs(p[0]-preferred_x)))
    for x, y in candidates:
        fp.SetPosition(b.mm(x, y))
        box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
        if not _inside_board(box, b.EDGE_MARGIN) or _hits_mount_keepout(box):
            continue
        if any(box.Intersects(ob) for _, ob in obstacles):
            continue
        print(f'internal anchor {old}: x={x:.1f} y={y:.1f} rot={rotation} fixed-anchor-clear=PASS')
        return (round(x, 3), round(y, 3), rotation)
    raise RuntimeError(f'cannot fit internal connector {old} ({fp_id}) inside bounds={bounds}')


def _local_candidates(px, py, span=8.0, step=1.0):
    pts = []
    d = 0.0
    while d <= span + 1e-9:
        offsets = [(0,0)] if d == 0 else []
        n = int(round(d / step))
        for ix in range(-n, n+1):
            iy = n - abs(ix)
            offsets.append((ix*step, iy*step))
            if iy:
                offsets.append((ix*step, -iy*step))
        for dx, dy in offsets:
            pts.append((px+dx, py+dy))
        d += step
    seen = set()
    for p in pts:
        k = (round(p[0],3), round(p[1],3))
        if k not in seen:
            seen.add(k)
            yield p


def _resolve_all_fixed_anchor_collisions():
    """Pairwise-solve all fixed anchors once, before b.main() sees them.

    Field transceivers and Ethernet/power controllers are placed before the MCU,
    because short field/PHY connections are less negotiable than MCU position.
    """
    connector_order = [
        'J_PWR','J_RS485A','J_RS485B','J_HMI','J_RS232','J_DI',
        'J_RLY1','J_RLY2','J_RLY3','J_RLY4','J_ETH','J_USB','J_SD',
    ]
    internal_order = [
        'K1','K2','K3','K4','Q1','Q2','Q3','Q4',
        'U5','U6','U3','U4','U2','U7','U1','U_RTC',
        'U_DI1','U_DI2','U_DI3','U_DI4','BT1','Y1','Y_RTC','SW_RESET','SW_BOOT',
    ]
    order = connector_order + internal_order
    chosen = []

    for old in order:
        if old not in b.FIXED:
            continue
        fp_id = _footprint_id_for_semantic(old)
        fp = b.load_fp(fp_id)
        fp.SetReference('TMP')
        px, py, rot = b.FIXED[old]
        fp.SetOrientationDegrees(rot)

        locked = old in connector_order
        candidates = [(px, py)] if locked else _local_candidates(px, py, span=24.0, step=1.0)
        found = None
        for x, y in candidates:
            fp.SetPosition(b.mm(x, y))
            box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
            if old in EDGE_OVERHANG:
                pbox = _merge_pad_boxes(fp)
                if pbox is None or not _inside_board(pbox, PAD_EDGE_MARGIN):
                    continue
            elif not _inside_board(box, b.EDGE_MARGIN):
                continue
            if _hits_mount_keepout(box):
                continue
            if any(box.Intersects(ob) for _, ob in chosen):
                continue
            found = (round(x,3), round(y,3), rot, box)
            break

        if found is None:
            if locked:
                fp.SetPosition(b.mm(px, py))
                box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
                conflicts = [name for name, ob in chosen if box.Intersects(ob)]
                raise RuntimeError(f'fixed connector geometry conflict {old} at {(px,py,rot)} with {conflicts}')
            raise RuntimeError(f'cannot locally resolve fixed internal anchor {old} near {(px,py,rot)}')

        x, y, rot, box = found
        b.FIXED[old] = (x, y, rot)
        chosen.append((old, box))
        if (x, y) != (px, py):
            print(f'fixed-anchor resolve {old}: {(px,py)} -> {(x,y)}')

    print(f'fixed-anchor global mechanical pass: PASS ({len(chosen)} anchors; mounting keepouts reserved)')


b.FIXED['J_ETH'] = _autofit_edge_anchor('J_ETH', 'Connector_RJ:RJ45_Cetus_J1B1211CCD_Horizontal', 65, 90)
b.FIXED['J_USB'] = _autofit_edge_anchor('J_USB', 'Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal', 42, 90)
for old, x in (('J_RLY1', 20), ('J_RLY2', 52), ('J_RLY3', 84), ('J_RLY4', 116)):
    b.FIXED[old] = _autofit_bottom_anchor(old, x)
for old, x in (('J_PWR', 14), ('J_RS485A', 37), ('J_RS485B', 59), ('J_HMI', 83), ('J_RS232', 101)):
    b.FIXED[old] = _autofit_top_anchor(old, x)
b.FIXED['J_DI'] = _autofit_top_anchor('J_DI', 122, allow_x_search=True)
b.FIXED['J_SD'] = _autofit_internal_anchor('J_SD', 116, 47, 90, bounds=(102, 36, 128, 58), step=0.5)

_resolve_all_fixed_anchor_collisions()
b.try_position = _try_position

if __name__ == '__main__':
    b.main()
