#!/usr/bin/env python3
"""Finalize Rev-A stack-up with a solid L2 GND reference plane.

In1.Cu is the dedicated L2 GND reference. A single concave GND polygon covers
all low-voltage board area while carving a deliberate no-plane notch under the
integrated MagJack magnetics. One polygon avoids same-net zone intersections
that KiCad correctly flags when multiple touching rectangles share a priority.

Before Specctra export, reserve short locked GND escape spokes for dense SMD
ground pads that cannot accept a through-via after autorouting because signal
traces occupy the required vertical clearance. These fixed spokes/vias are
exported in the DSN as pre-existing wiring so the autorouter must route around
the required L2 return-path access.
"""
from pathlib import Path
import sys
import pcbnew

EDGE = 0.50
BOARD_X = 145.0
BOARD_Y = 95.0
MAGJACK_X0 = 127.0
MAGJACK_Y0 = 55.5
MAGJACK_Y1 = 74.5

VIA_D = 0.60
VIA_DRILL = 0.30
VIA_CLEAR = 0.24
TRACK_W = 0.20
TRACK_CLEAR = 0.24

# Recurring post-router GND islands are reserved before Specctra export so the
# signal router must leave legal vertical access to the solid L2 reference.
# Run #42 proved C35:1 and U2:23; Run #43 then exposed U2:29, C34:2 and U14:7
# after the router re-packed low-speed traces around the newly reserved vias.
# Edge pads escape perpendicular away from their packages; passive GND pads
# escape away from the opposite terminal. Immediate KiCad pre-route DRC remains
# the release authority for these dense but deterministic reservations.
PRE_ROUTE_GND_ESCAPES = (
    ("C14", "2", 0.90, 0.00),
    ("C16", "2", 0.90, 0.00),
    ("C31", "2", 0.90, 0.00),
    ("C28", "2", 0.90, 0.00),
    ("R69", "2", 0.90, 0.00),
    ("U2", "9", 1.285, 0.00),
    ("C35", "1", -0.90, 0.00),
    ("U2", "23", 0.00, -1.50),
    ("U2", "29", -1.285, 0.00),
    ("C34", "2", 0.90, 0.00),
    ("U14", "7", -1.285, 0.00),
)


def pt(x, y):
    return pcbnew.VECTOR2I_MM(float(x), float(y))


def xy(p):
    return pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)


def bbox_mm(bb):
    return (
        pcbnew.ToMM(bb.GetLeft()),
        pcbnew.ToMM(bb.GetRight()),
        pcbnew.ToMM(bb.GetTop()),
        pcbnew.ToMM(bb.GetBottom()),
    )


def boxes_overlap(a, b):
    ax0, ax1, ay0, ay1 = a
    bx0, bx1, by0, by1 = b
    return not (ax1 < bx0 or bx1 < ax0 or ay1 < by0 or by1 < ay0)


def gnd_net(board):
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname() == "GND":
                return pad.GetNet()
    raise RuntimeError("GND net not found")


def find_pad(board, ref, number):
    for fp in board.Footprints():
        if fp.GetReference() != ref:
            continue
        for pad in fp.Pads():
            if str(pad.GetNumber()) == str(number):
                return pad
        raise RuntimeError(f"pre-route GND escape pad missing: {ref}:{number}")
    raise RuntimeError(f"pre-route GND escape footprint missing: {ref}")


def assert_escape_clear(board, pad, via_xy, gcode):
    px, py = xy(pad.GetPosition())
    vx, vy = via_xy
    via_r = VIA_D / 2 + VIA_CLEAR
    track_r = TRACK_W / 2 + TRACK_CLEAR

    if not (EDGE + via_r <= vx <= BOARD_X - EDGE - via_r and EDGE + via_r <= vy <= BOARD_Y - EDGE - via_r):
        raise RuntimeError(f"pre-route GND via outside board-safe area at {(vx, vy)}")
    if vx >= MAGJACK_X0 - via_r and MAGJACK_Y0 - via_r <= vy <= MAGJACK_Y1 + via_r:
        raise RuntimeError(f"pre-route GND via enters MagJack exclusion at {(vx, vy)}")

    # All controlled escapes are axis-aligned. Test the spoke centreline bbox
    # against pads inflated once by half-track-width + clearance. Immediate
    # KiCad DRC after the locked critical routes is the final geometry authority.
    corridor = (min(px, vx), max(px, vx), min(py, vy), max(py, vy))
    for fp in board.Footprints():
        for other in fp.Pads():
            if other.GetNetCode() == gcode:
                continue
            bx0, bx1, by0, by1 = bbox_mm(other.GetBoundingBox())
            via_box = (bx0 - via_r, bx1 + via_r, by0 - via_r, by1 + via_r)
            if via_box[0] <= vx <= via_box[1] and via_box[2] <= vy <= via_box[3]:
                raise RuntimeError(
                    f"pre-route GND via {via_xy} violates pad clearance to "
                    f"{other.GetParent().GetReference()}:{other.GetNumber()} net={other.GetNetname()}"
                )
            try:
                on_front = other.IsOnLayer(pcbnew.F_Cu)
            except Exception:
                on_front = True
            if on_front:
                track_box = (bx0 - track_r, bx1 + track_r, by0 - track_r, by1 + track_r)
                if boxes_overlap(corridor, track_box):
                    raise RuntimeError(
                        f"pre-route GND spoke {pad.GetParent().GetReference()}:{pad.GetNumber()} "
                        f"violates F.Cu pad clearance to {other.GetParent().GetReference()}:{other.GetNumber()} "
                        f"net={other.GetNetname()}"
                    )


def reserve_gnd_escapes(board, gnet):
    gcode = gnet.GetNetCode()
    non_gnd_tracks = [t for t in board.GetTracks() if t.GetNetCode() != gcode]
    if non_gnd_tracks:
        raise RuntimeError(
            f"pre-route GND escape reservation must run before signal routing; "
            f"found {len(non_gnd_tracks)} non-GND track/via item(s)"
        )

    added = 0
    for ref, number, dx, dy in PRE_ROUTE_GND_ESCAPES:
        pad = find_pad(board, ref, number)
        if pad.GetNetCode() != gcode:
            raise RuntimeError(f"pre-route GND escape contract mismatch: {ref}:{number} net={pad.GetNetname()}")
        px, py = xy(pad.GetPosition())
        vx, vy = px + dx, py + dy
        assert_escape_clear(board, pad, (vx, vy), gcode)

        via = pcbnew.PCB_VIA(board)
        via.SetPosition(pt(vx, vy))
        via.SetWidth(pcbnew.FromMM(VIA_D))
        via.SetDrill(pcbnew.FromMM(VIA_DRILL))
        via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        via.SetNet(gnet)
        via.SetLocked(True)
        board.Add(via)

        track = pcbnew.PCB_TRACK(board)
        track.SetStart(pad.GetPosition())
        track.SetEnd(pt(vx, vy))
        track.SetWidth(pcbnew.FromMM(TRACK_W))
        track.SetLayer(pcbnew.F_Cu)
        track.SetNet(gnet)
        track.SetLocked(True)
        board.Add(track)

        print(f"PRE_ROUTE_GND_ESCAPE: {ref}:{number} pad=({px:.3f},{py:.3f}) via=({vx:.3f},{vy:.3f})")
        added += 1

    if added != len(PRE_ROUTE_GND_ESCAPES):
        raise RuntimeError(f"pre-route GND escape reservation incomplete: {added}/{len(PRE_ROUTE_GND_ESCAPES)}")
    print(f"PRE_ROUTE_GND_ESCAPE_PASS: reserved={added} via={VIA_D:.2f}/{VIA_DRILL:.2f}mm track={TRACK_W:.2f}mm")


def add_notched_zone(board, layer, netcode):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(netcode)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetMinThickness(pcbnew.FromMM(0.20))
    z.SetLocalClearance(pcbnew.FromMM(0.20))
    outline = (
        (EDGE, EDGE),
        (BOARD_X - EDGE, EDGE),
        (BOARD_X - EDGE, MAGJACK_Y0),
        (MAGJACK_X0, MAGJACK_Y0),
        (MAGJACK_X0, MAGJACK_Y1),
        (BOARD_X - EDGE, MAGJACK_Y1),
        (BOARD_X - EDGE, BOARD_Y - EDGE),
        (EDGE, BOARD_Y - EDGE),
    )
    for x, y in outline:
        if not z.AppendCorner(pt(x, y), -1):
            raise RuntimeError(f"failed to append GND-zone corner {(x, y)}")
    board.Add(z)
    return z


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f"cannot load board: {path}")
    layer = board.GetLayerID("In1.Cu")
    if layer < 0:
        raise SystemExit("In1.Cu layer missing")

    bad = [t for t in board.GetTracks() if not isinstance(t, pcbnew.PCB_VIA) and t.GetLayer() == layer]
    if bad:
        raise SystemExit(f"cannot create L2 GND reference: {len(bad)} In1.Cu signal tracks present")

    gnet = gnd_net(board)
    add_notched_zone(board, layer, gnet.GetNetCode())
    reserve_gnd_escapes(board, gnet)
    pcbnew.SaveBoard(str(path), board)
    print(
        f"L2_GND_ZONE_GEOMETRY_PASS: solid zones=1 notched MagJack_window="
        f"({MAGJACK_X0},{MAGJACK_Y0})-({BOARD_X},{MAGJACK_Y1})"
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: post_route_finalize.py BOARD")
    main(sys.argv[1])
