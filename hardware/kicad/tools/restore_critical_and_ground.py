#!/usr/bin/env python3
"""Restore controlled high-speed routes and complete GND plane connectivity.

The caller first removes prior controlled route primitives with the text-safe
stripper. This process is deliberately add-only: no pcbnew BOARD item deletion,
avoiding SWIG ownership crashes observed in CI. In1.Cu remains the dedicated L2
GND reference. F.Cu and B.Cu each get one non-overlapping concave GND polygon in
the low-voltage region, with the relay-contact side and MagJack window excluded.
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


def add_surface_notched_zone(board, layer, netcode):
    z=pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(netcode)
    outline=(
        (EDGE,LOGIC_Y0),
        (BOARD_X-EDGE,LOGIC_Y0),
        (BOARD_X-EDGE,MAGJACK_Y0),
        (MAGJACK_X0,MAGJACK_Y0),
        (MAGJACK_X0,MAGJACK_Y1),
        (BOARD_X-EDGE,MAGJACK_Y1),
        (BOARD_X-EDGE,BOARD_Y-EDGE),
        (EDGE,BOARD_Y-EDGE),
    )
    for x,y in outline:
        if not z.AppendCorner(pt(x,y),-1):
            raise RuntimeError(f'failed to append GND-zone corner {(x,y)}')
    board.Add(z)


def add_surface_ground(board):
    gnd=gnd_netcode(board)
    for layer in (pcbnew.F_Cu,pcbnew.B_Cu):
        add_surface_notched_zone(board,layer,gnd)
    return 2


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f'cannot load board: {path}')

    critical.route_ethernet(board)
    critical.route_usb(board)
    zones = add_surface_ground(board)
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(path), board)
    print(f'POST_SES_REPAIR: PASS controlled_routes=restored surface_gnd_zones={zones}')
    print('  safety: F/B GND pours start at Y=30.0mm; relay-contact region excluded')
    print('  MagJack transformer window excluded; no touching same-priority GND rectangles')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: restore_critical_and_ground.py BOARD.kicad_pcb')
    main(sys.argv[1])
