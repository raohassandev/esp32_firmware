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
- edge connector anchors are auto-fitted against real KiCad pad geometry so a
  library-origin change cannot silently push solder pads off the fabricated PCB.
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
    """Search full board width, preferring the furthest-right valid pad-safe anchor."""
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
    """Move a bottom-edge terminal inward only as far as its real courtyard requires."""
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


def _autofit_top_anchor(old, x, rotation=0):
    """Move a service-edge connector inward only as far as its real courtyard requires."""
    fp_id = _footprint_id_for_semantic(old)
    fp = b.load_fp(fp_id)
    fp.SetReference('TMP')
    fp.SetOrientationDegrees(rotation)
    y = b.BOARD_Y - 1.0
    while y >= b.BOARD_Y - 18.0:
        fp.SetPosition(b.mm(x, y))
        box = _physical_box(fp).GetInflated(pcbnew.FromMM(MECH_CLEARANCE))
        if _inside_board(box, b.EDGE_MARGIN):
            print(f'top anchor {old}: x={x:.1f} y={y:.1f} rot={rotation} courtyard-inside-board=PASS')
            return (x, round(y, 3), rotation)
        y -= 0.5
    raise RuntimeError(f'cannot fit top connector {old} inside board without changing PCB mechanics')


# Auto-fit the two intentional edge-overhang connectors from their actual stock
# KiCad footprints. Copper/drilled pads must stay inside the fabricated PCB.
b.FIXED['J_ETH'] = _autofit_edge_anchor(
    'J_ETH', 'Connector_RJ:RJ45_Cetus_J1B1211CCD_Horizontal', 65, 90
)
b.FIXED['J_USB'] = _autofit_edge_anchor(
    'J_USB', 'Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal', 42, 90
)

# Relay field terminals live on the bottom contact edge.
for old, x in (('J_RLY1', 20), ('J_RLY2', 52), ('J_RLY3', 84), ('J_RLY4', 116)):
    b.FIXED[old] = _autofit_bottom_anchor(old, x)

# Service row is the opposite edge. This covers mandatory power, dual RS485 and
# HMI plus optional RS232/DI so DNP features cannot silently make the board
# mechanically impossible when the Full variant is populated.
for old, x in (
    ('J_PWR', 14), ('J_RS485A', 37), ('J_RS485B', 59),
    ('J_HMI', 83), ('J_RS232', 101), ('J_DI', 122),
):
    b.FIXED[old] = _autofit_top_anchor(old, x)

b.try_position = _try_position

if __name__ == '__main__':
    b.main()
