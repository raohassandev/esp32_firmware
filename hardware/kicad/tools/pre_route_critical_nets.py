#!/usr/bin/env python3
"""Pre-route critical Rev-A nets that must not be left to the generic autorouter.

The W5500 ETH_RXP connection repeatedly remained as the sole ratsnest after
Freerouting.  Route it deterministically on B.Cu with a short F.Cu escape and a
through via outside the LQFP body, then lock the geometry so the remaining
board routes around it.
"""
from pathlib import Path
import sys
import pcbnew

WIDTH_MM = 0.20
VIA_MM = 0.60
DRILL_MM = 0.30


def mm(v):
    return pcbnew.FromMM(float(v))


def xy_mm(p):
    return (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))


def point(x, y):
    return pcbnew.VECTOR2I_MM(float(x), float(y))


def footprint(board, ref):
    for fp in board.Footprints():
        if fp.GetReference() == ref:
            return fp
    raise RuntimeError(f'footprint not found: {ref}')


def pad(fp, number):
    for p in fp.Pads():
        if p.GetNumber() == str(number):
            return p
    raise RuntimeError(f'pad not found: {fp.GetReference()}-{number}')


def add_track(board, start, end, layer, net):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(point(*start))
    t.SetEnd(point(*end))
    t.SetWidth(mm(WIDTH_MM))
    t.SetLayer(layer)
    t.SetNet(net)
    t.SetLocked(True)
    board.Add(t)
    return t


def add_via(board, at, net):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(point(*at))
    v.SetWidth(mm(VIA_MM))
    v.SetDrill(mm(DRILL_MM))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(net)
    v.SetLocked(True)
    board.Add(v)
    return v


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f'cannot load board: {path}')

    u2 = footprint(board, 'U2')
    j3 = footprint(board, 'J3')
    src_pad = pad(u2, 6)   # W5500 ETH_RXP
    dst_pad = pad(j3, 3)   # MagJack ETH_RXP
    if src_pad.GetNetname() != 'ETH_RXP' or dst_pad.GetNetname() != 'ETH_RXP':
        raise SystemExit(f'ETH_RXP endpoint mismatch: U2-6={src_pad.GetNetname()} J3-3={dst_pad.GetNetname()}')

    src = xy_mm(src_pad.GetPosition())
    dst = xy_mm(dst_pad.GetPosition())
    net = src_pad.GetNet()

    # U2-6 is on the left edge of the LQFP.  Escape left, place the via outside
    # the package body, then route on the otherwise-free B.Cu corridor.  The
    # final horizontal approach sits between MagJack pins 2 and 4 with >1 mm
    # centreline separation from either through-hole pad.
    escape = (src[0] - 1.25, src[1])
    bend1 = (126.80, src[1])
    bend2 = (128.00, dst[1])

    add_track(board, src, escape, pcbnew.F_Cu, net)
    add_via(board, escape, net)
    add_track(board, escape, bend1, pcbnew.B_Cu, net)
    add_track(board, bend1, bend2, pcbnew.B_Cu, net)
    add_track(board, bend2, dst, pcbnew.B_Cu, net)

    board.BuildConnectivity()
    conn = board.GetConnectivity()
    conn.Build(board)
    conn.RecalculateRatsnest()
    pcbnew.SaveBoard(str(path), board)
    print('ETH_RXP_LOCKED_PREROUTE: PASS')
    print('  U2-6  = %.4f, %.4f mm' % src)
    print('  via   = %.4f, %.4f mm' % escape)
    print('  J3-3  = %.4f, %.4f mm' % dst)
    print('  unrouted_after_preroute=%d' % int(conn.GetUnconnectedCount(False)))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: pre_route_critical_nets.py BOARD')
    main(sys.argv[1])
