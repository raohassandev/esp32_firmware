#!/usr/bin/env python3
"""Restore controlled high-speed routes and complete GND plane connectivity.

Specctra SES import can replace/omit pre-routed fixed tracks. After every generic
routing attempt we therefore rebuild the Rev-A controlled USB/Ethernet topology,
then add F.Cu/B.Cu GND pours in the low-voltage logic region. In1.Cu remains the
dedicated continuous L2 GND reference; relay contact/mains-capable region below
Y=30 mm and the MagJack transformer window are deliberately excluded.
"""
from pathlib import Path
import math
import sys
import pcbnew

import pre_route_critical_nets as critical

CRITICAL_NETS = {
    'ETH_TXP','ETH_TXN','ETH_RXP','ETH_RXN',
    'USB_D+','USB_D-','USB_D+_MCU','USB_D-_MCU',
}
USB_GND_STITCH = ((134.0,48.0),(137.5,48.0),(134.0,39.5),(137.5,39.5))
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


def remove_critical_tracks(board):
    removed = 0
    for item in list(board.GetTracks()):
        net = item.GetNetname()
        drop = net in CRITICAL_NETS
        if isinstance(item, pcbnew.PCB_VIA) and net == 'GND':
            p = item.GetPosition()
            x,y = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
            drop = drop or any(math.hypot(x-a,y-b) < 0.05 for a,b in USB_GND_STITCH)
        if drop:
            board.Remove(item)
            removed += 1
    return removed


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

    removed = remove_critical_tracks(board)
    critical.route_ethernet(board)
    critical.route_usb(board)
    zones = add_surface_ground(board)
    board.BuildConnectivity()
    pcbnew.SaveBoard(str(path), board)
    print(f'POST_SES_REPAIR: PASS removed_critical_items={removed} surface_gnd_zones={zones}')
    print('  safety: F/B GND pours start at Y=30.0mm; relay-contact region excluded')
    print('  MagJack transformer window excluded on F/B; In1.Cu L2 plane preserved')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: restore_critical_and_ground.py BOARD.kicad_pcb')
    main(sys.argv[1])
