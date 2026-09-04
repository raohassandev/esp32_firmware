#!/usr/bin/env python3
"""Restore deterministic routes after Specctra SES import.

Deliberately avoids BuildConnectivity/RecalculateRatsnest here. KiCad CLI will
refill zones and rebuild connectivity in a separate native process afterwards,
which avoids the pcbnew SWIG ownership crash seen on large routed boards.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as critical
import route_reva_stragglers as stragglers


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    critical.route_ethernet(board)
    critical.route_usb(board)
    stragglers.route(board)
    pcbnew.SaveBoard(str(path),board)
    print('CONTROLLED_ROUTE_RESTORE: PASS high-speed + stable stragglers')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: restore_controlled_routes.py BOARD.kicad_pcb')
    main(sys.argv[1])
