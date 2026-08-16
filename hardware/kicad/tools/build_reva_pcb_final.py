#!/usr/bin/env python3
"""Mechanical finalizer for Rev-A deterministic PCB placement.

The base builder owns electrical net/footprint creation and functional zoning.
This wrapper owns production-style mechanical fit semantics:
- footprint courtyards, rather than reference/value text, drive collisions;
- ordinary components must keep their courtyard inside the board;
- RJ45/USB may intentionally overhang the enclosure edge, but every copper pad
  must remain inside the PCB;
- relay and service-edge field terminals are auto-fitted from their exact
  KiCad courtyard geometry rather than assumed connector depths;
- optional user-accessible connectors are fitted against the complete fixed
  anchor set so DNP/full-population variants cannot silently collide.
"""
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
    return (
        x0 >= margin and y0 >= margin and
        x1 <= b.BOARD_X - margin and y1 <= b.BOARD_Y - margin
    )


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
    fp_id = _footprint_id_for_semantic(old)
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    fp.SetOrientationDegrees(rotation)
    y = 1.0
    while y <= 15.0:
        fp.SetPosition(b.mm(x, y))
        box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
        if _inside_board(box, b.EDGE_MARGIN):
            print(f'bottom anchor {old}: x={x:.1f} y={y:.1f} rot={rotation} courtyard-inside-board=PASS')
            return (x, round(y, 3), rotation)
        y += 0.5
    raise RuntimeError(f'cannot fit bottom connector {old} inside board without changing PCB mechanics')


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
    box0 = _physical_box(fp)
    print(
        f'top-fit {old}: footprint={fp_id} '
        f'courtyard={pcbnew.ToMM(box0.GetWidth()):.1f}x{pcbnew.ToMM(box0.GetHeight()):.1f}mm '
        f'preferred_x={x:.1f}'
    )
    for candidate_x in xs:
        y = b.BOARD_Y - 1.0
        while y >= b.BOARD_Y - 18.0:
            fp.SetPosition(b.mm(candidate_x, y))
            box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
            if _inside_board(box, b.EDGE_MARGIN):
                print(f'top anchor {old}: x={candidate_x:.1f} y={y:.1f} rot={rotation} courtyard-inside-board=PASS')
                return (round(candidate_x, 3), round(y, 3), rotation)
            y -= 0.5
    raise RuntimeError(
        f'cannot fit top connector {old} ({fp_id}) inside board; '
        f'preferred_x={x}, allow_x_search={allow_x_search}'
    )


def _fixed_obstacles(exclude_old):
    """Instantiate current fixed anchors as geometry-only obstacles."""
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
    """Find a board-safe, fixed-anchor-safe position inside a functional window."""
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
        if not _inside_board(box, b.EDGE_MARGIN):
            continue
        if any(box.Intersects(ob) for _, ob in obstacles):
            continue
        print(f'internal anchor {old}: x={x:.1f} y={y:.1f} rot={rotation} fixed-anchor-clear=PASS')
        return (round(x, 3), round(y, 3), rotation)
    raise RuntimeError(f'cannot fit internal connector {old} ({fp_id}) inside bounds={bounds}')


b.FIXED['J_ETH'] = _autofit_edge_anchor(
    'J_ETH', 'Connector_RJ:RJ45_Cetus_J1B1211CCD_Horizontal', 65, 90
)
b.FIXED['J_USB'] = _autofit_edge_anchor(
    'J_USB', 'Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal', 42, 90
)

for old, x in (('J_RLY1', 20), ('J_RLY2', 52), ('J_RLY3', 84), ('J_RLY4', 116)):
    b.FIXED[old] = _autofit_bottom_anchor(old, x)

for old, x in (
    ('J_PWR', 14), ('J_RS485A', 37), ('J_RS485B', 59),
    ('J_HMI', 83), ('J_RS232', 101),
):
    b.FIXED[old] = _autofit_top_anchor(old, x)

b.FIXED['J_DI'] = _autofit_top_anchor('J_DI', 122, allow_x_search=True)

# microSD is optional but user-accessible. Fit it in its intended right-side
# functional region while explicitly avoiding W5500, RJ45 and USB fixed anchors.
b.FIXED['J_SD'] = _autofit_internal_anchor(
    'J_SD', preferred_x=116, preferred_y=47, rotation=90,
    bounds=(104, 38, 126, 56), step=0.5,
)

b.try_position = _try_position

if __name__ == '__main__':
    b.main()
