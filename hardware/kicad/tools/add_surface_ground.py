#!/usr/bin/env python3
"""Add F.Cu/B.Cu solid GND pours after SES import."""
from pathlib import Path
import sys
import pcbnew

EDGE=0.50
LOGIC_Y0=30.0
MAGJACK_X0=127.0
MAGJACK_Y0=55.5
MAGJACK_Y1=74.5
BOARD_X=145.0
BOARD_Y=95.0


def pt(x,y): return pcbnew.VECTOR2I_MM(float(x),float(y))


def gnd_netcode(board):
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname()=='GND': return pad.GetNetCode()
    raise RuntimeError('GND netcode not found')


def add_zone(board,layer,netcode):
    z=pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNetCode(netcode)
    # The surface pours are the low-impedance GND return for logic-side pads.
    # Use solid pad connection so the post-router pour itself closes the GND
    # connectivity instead of leaving thermal spokes starved by dense routing.
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetMinThickness(pcbnew.FromMM(0.20))
    z.SetLocalClearance(pcbnew.FromMM(0.20))
    outline=((EDGE,LOGIC_Y0),(BOARD_X-EDGE,LOGIC_Y0),(BOARD_X-EDGE,MAGJACK_Y0),
             (MAGJACK_X0,MAGJACK_Y0),(MAGJACK_X0,MAGJACK_Y1),
             (BOARD_X-EDGE,MAGJACK_Y1),(BOARD_X-EDGE,BOARD_Y-EDGE),(EDGE,BOARD_Y-EDGE))
    for x,y in outline:
        if not z.AppendCorner(pt(x,y),-1): raise RuntimeError(f'zone corner failed {(x,y)}')
    board.Add(z)


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    nc=gnd_netcode(board)
    add_zone(board,pcbnew.F_Cu,nc); add_zone(board,pcbnew.B_Cu,nc)
    pcbnew.SaveBoard(str(path),board)
    print('SURFACE_GND_GEOMETRY: PASS solid F.Cu+B.Cu logic pours; relay/MagJack exclusions retained')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: add_surface_ground.py BOARD.kicad_pcb')
    main(sys.argv[1])
