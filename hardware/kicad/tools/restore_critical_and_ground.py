#!/usr/bin/env python3
"""Restore controlled high-speed routes and complete GND plane connectivity.

The caller first removes prior controlled route primitives with the text-safe
stripper. This process is deliberately add-only: no pcbnew BOARD item deletion,
avoiding SWIG ownership crashes observed in CI. In1.Cu remains the dedicated L2
GND reference; F/B GND pours are limited to the low-voltage logic region.
"""
from pathlib import Path
import sys
import pcbnew

import pre_route_critical_nets as critical

EDGE = 0.50
LOGIC_Y0 = 30.0
MAGJACK_X0 = 127.0
MAGJACK_Y0 = 55.5
MAGJACK_Y1 = 74.5
BOARD_X = 145.0
BOARD_Y = 95.0


def pt(x,y): return pcbnew.VECTOR2I_MM(float(x),float(y))


def gnd_netcode(board):
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname() == 'GND':
                return pad.GetNetCode()
    raise RuntimeError('GND netcode not found')


def add_rect_zone(board, layer, netcode, rect):
    x0,y0,x1,y1 = rect
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(netcode)
    for x,y in ((x0,y0),(x1,y0),(x1,y1),(x0,y1)):
        if not z.AppendCorner(pt(x,y), -1):
            raise RuntimeError(f'failed to append GND-zone corner {(x,y)}')
    board.Add(z)


def add_surface_ground(board):
    gnd = gnd_netcode(board)
    layers = (pcbnew.F_Cu, pcbnew.B_Cu)
    rects = (
        (EDGE, LOGIC_Y0, MAGJACK_X0, BOARD_Y-EDGE),
        (MAGJACK_X0, LOGIC_Y0, BOARD_X-EDGE, MAGJACK_Y0),
        (MAGJACK_X0, MAGJACK_Y1, BOARD_X-EDGE, BOARD_Y-EDGE),
    )
    for layer in layers:
        for rect in rects:
            add_rect_zone(board, layer, gnd, rect)
    return len(layers) * len(rects)


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f'cannot load board: {path}')

    # Text-safe stripper has already removed only the controlled route items.
    critical.route_ethernet(board)
    critical.route_usb(board)
    zones = add_surface_ground(board)
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(path), board)
    print(f'POST_SES_REPAIR: PASS controlled_routes=restored surface_gnd_zones={zones}')
    print('  safety: F/B GND pours start at Y=30.0mm; relay-contact region excluded')
    print('  MagJack transformer window excluded on F/B; In1.Cu L2 plane preserved')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: restore_critical_and_ground.py BOARD.kicad_pcb')
    main(sys.argv[1])
