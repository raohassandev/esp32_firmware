#!/usr/bin/env python3
"""Finalize Rev-A stack-up after signal routing.

In1.Cu is reserved as the L2 GND reference layer. Three GND polygons cover the
board while deliberately leaving the MagJack/transformer region unpoured. KiCad
CLI refills the zones in the following workflow step, so embedded ESP32 antenna
keepouts and footprint clearances remain authoritative.
"""
from pathlib import Path
import sys
import pcbnew

EDGE = 0.50
# Leave an intentional no-plane window beneath/around the integrated magnetics.
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


def add_rect_zone(board, layer, netcode, rect):
    x0,y0,x1,y1=rect
    z=pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(netcode)
    for x,y in ((x0,y0),(x1,y0),(x1,y1),(x0,y1)):
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

    # Fail closed if any routed signal segment leaked onto the reference layer.
    bad=[t for t in board.GetTracks() if not isinstance(t,pcbnew.PCB_VIA) and t.GetLayer()==layer]
    if bad: raise SystemExit(f'cannot create L2 GND reference: {len(bad)} In1.Cu signal tracks present')

    netcode=gnd_netcode(board)
    zones=[
        (EDGE,EDGE,MAGJACK_X0,95.0-EDGE),
        (MAGJACK_X0,EDGE,145.0-EDGE,MAGJACK_Y0),
        (MAGJACK_X0,MAGJACK_Y1,145.0-EDGE,95.0-EDGE),
    ]
    for rect in zones: add_rect_zone(board,layer,netcode,rect)
    pcbnew.SaveBoard(str(path),board)
    print(f'L2_GND_ZONE_GEOMETRY_PASS: zones={len(zones)} MagJack_window=({MAGJACK_X0},{MAGJACK_Y0})-({145.0},{MAGJACK_Y1})')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: post_route_finalize.py BOARD')
    main(sys.argv[1])
