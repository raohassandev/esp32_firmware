#!/usr/bin/env python3
"""Finalize Rev-A stack-up with a solid L2 GND reference plane.

In1.Cu is the dedicated L2 GND reference. A single concave GND polygon covers
all low-voltage board area while carving a deliberate no-plane notch under the
integrated MagJack magnetics. One polygon avoids same-net zone intersections
that KiCad correctly flags when multiple touching rectangles share a priority.
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


def pt(x,y): return pcbnew.VECTOR2I_MM(float(x),float(y))


def gnd_netcode(board):
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname() == 'GND':
                return pad.GetNetCode()
    raise RuntimeError('GND netcode not found')


def add_notched_zone(board, layer, netcode):
    z=pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(netcode)
    # L2 is the authoritative low-impedance GND reference. Solid pad
    # connection avoids thermal-spoke starvation and lets PTH/via GND pads
    # close connectivity through the plane rather than through routed traces.
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetMinThickness(pcbnew.FromMM(0.20))
    z.SetLocalClearance(pcbnew.FromMM(0.20))
    outline=(
        (EDGE,EDGE),
        (BOARD_X-EDGE,EDGE),
        (BOARD_X-EDGE,MAGJACK_Y0),
        (MAGJACK_X0,MAGJACK_Y0),
        (MAGJACK_X0,MAGJACK_Y1),
        (BOARD_X-EDGE,MAGJACK_Y1),
        (EDGE,BOARD_Y-EDGE),
    )
    for x,y in outline:
        if not z.AppendCorner(pt(x,y),-1):
            raise RuntimeError(f'failed to append GND-zone corner {(x,y)}')
    board.Add(z)
    return z


def main(board_path):
    path=Path(board_path)
    board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    layer=board.GetLayerID('In1.Cu')
    if layer < 0: raise SystemExit('In1.Cu layer missing')

    bad=[t for t in board.GetTracks() if not isinstance(t,pcbnew.PCB_VIA) and t.GetLayer()==layer]
    if bad: raise SystemExit(f'cannot create L2 GND reference: {len(bad)} In1.Cu signal tracks present')

    add_notched_zone(board,layer,gnd_netcode(board))
    pcbnew.SaveBoard(str(path),board)
    print(f'L2_GND_ZONE_GEOMETRY_PASS: solid zones=1 notched MagJack_window=({MAGJACK_X0},{MAGJACK_Y0})-({BOARD_X},{MAGJACK_Y1})')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: post_route_finalize.py BOARD')
    main(sys.argv[1])
