#!/usr/bin/env python3
"""Mechanical finalizer for Rev-A deterministic PCB placement.

The base builder owns electrical net/footprint creation and functional zoning.
This wrapper owns production-style mechanical fit semantics:
- footprint courtyards, rather than reference/value text, drive collisions;
- ordinary components must keep their courtyard inside the board;
- RJ45/USB may intentionally overhang the enclosure edge, but every copper pad
  must remain inside the PCB;
- field terminals are moved inward enough for their actual KiCad courtyards;
- edge connector anchors are auto-fitted against real KiCad pad geometry so a
  library-origin change cannot silently push solder pads off the fabricated PCB.
"""
import pcbnew
import build_reva_pcb as b

# Revised field-terminal anchors after checking actual KiCad-10 courtyards.
b.FIXED.update({
    'J_PWR': (14, 87, 0),
    'J_RS485A': (37, 87, 0),
    'J_RS485B': (59, 87, 0),
    'J_HMI': (83, 87, 0),
    'J_RS232': (101, 87, 0),
    'J_DI': (122, 87, 0),
    # Edge connector positions are resolved dynamically below.
    'J_ETH': (132, 65, 90),
    'J_USB': (137, 42, 90),
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
    """Return transformed front-courtyard bbox, falling back to pad geometry."""
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
        # Body/courtyard may extend through the enclosure wall, but copper pads
        # and drilled pads may not fall off the fabricated PCB.
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


def _edge_anchor_candidates(y, rotation, *, xmin=118.0, xmax=141.0, step=0.5):
    """Prefer the furthest-right valid position while keeping all pads on PCB."""
    x = xmax
    out = []
    while x >= xmin - 1e-9:
        out.append((round(x, 3), y, rotation))
        x -= step
    return out


def _autofit_edge_anchor(old, fp_id, y, rotation):
    """Resolve a safe anchor from the actual footprint pad geometry."""
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    for x, yy, rot in _edge_anchor_candidates(y, rotation):
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(b.mm(x, yy))
        pbox = _merge_pad_boxes(fp)
        if pbox is not None and _inside_board(pbox, PAD_EDGE_MARGIN):
            print(f'edge anchor {old}: x={x:.1f} y={yy:.1f} rot={rot} pads-inside-board=PASS')
            return (x, yy, rot)
    raise RuntimeError(f'cannot fit {old} pads inside {b.BOARD_X}x{b.BOARD_Y} mm board')


# Resolve the two intentional edge connectors using the exact KiCad footprints
# selected by the schematic. These IDs are controlled in the Rev-A manifest.
b.FIXED['J_ETH'] = _autofit_edge_anchor(
    'J_ETH', 'Connector_RJ:RJ45_Hanrun_HR911105A', 65, 90
)
b.FIXED['J_USB'] = _autofit_edge_anchor(
    'J_USB', 'Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal', 42, 90
)

b.try_position = _try_position

if __name__ == '__main__':
    b.main()
