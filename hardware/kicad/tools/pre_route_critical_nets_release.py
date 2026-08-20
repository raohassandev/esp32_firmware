#!/usr/bin/env python3
"""Release critical-route wrapper for Rev-A USB + current W5500 MDI topology.

USB routing is shared with pre_route_critical_nets.py. Ethernet MDI routing here
matches the current four 0R damping resistors and J1B1211CCD MagJack at its fixed
90-degree board-edge orientation. Critical tracks are locked before DSN export,
so Freerouting only owns the remaining low-speed/non-critical routing problem.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as base


def _xy(p):
    return base.xy_mm(p.GetPosition())


def _require_net(p, name):
    if p.GetNetname() != name:
        raise RuntimeError(
            f'{p.GetParent().GetReference()} pad {p.GetNumber()}: '
            f'{p.GetNetname()} != {name}'
        )
    return p


def _resistor(board, semantic, chip_net, mag_net):
    fp = base.footprint(board, base.semantic_ref(semantic))
    p1 = _require_net(base.pad_number(fp, 1), chip_net)
    p2 = _require_net(base.pad_number(fp, 2), mag_net)
    return p1, p2


def route_ethernet(board):
    u2 = base.footprint(board, base.semantic_ref('U2'))
    j3 = base.footprint(board, base.semantic_ref('J_ETH'))
    F = pcbnew.F_Cu
    IN2 = board.GetLayerID('In2.Cu')

    tx_n = _require_net(base.pad_number(u2, 1), 'ETH_TXN')
    tx_p = _require_net(base.pad_number(u2, 2), 'ETH_TXP')
    rx_n = _require_net(base.pad_number(u2, 5), 'ETH_RXN')
    rx_p = _require_net(base.pad_number(u2, 6), 'ETH_RXP')

    r_txp_c, r_txp_m = _resistor(board, 'R_ETH_TXP_DAMP', 'ETH_TXP', 'ETH_TXP_MAG')
    r_txn_c, r_txn_m = _resistor(board, 'R_ETH_TXN_DAMP', 'ETH_TXN', 'ETH_TXN_MAG')
    r_rxp_c, r_rxp_m = _resistor(board, 'R_ETH_RXP_DAMP', 'ETH_RXP', 'ETH_RXP_MAG')
    r_rxn_c, r_rxn_m = _resistor(board, 'R_ETH_RXN_DAMP', 'ETH_RXN', 'ETH_RXN_MAG')

    # J1B1211CCD physical pin mapping at the frozen 90-degree orientation:
    # 1=TXP, 3=TXN, 4=RXP, 6=RXN. The alternating PTH columns are on 1.27 mm
    # pitch, so final approaches use each destination's exact Y coordinate.
    j_tx_p = _require_net(base.pad_number(j3, 1), 'ETH_TXP_MAG')
    j_tx_n = _require_net(base.pad_number(j3, 3), 'ETH_TXN_MAG')
    j_rx_p = _require_net(base.pad_number(j3, 4), 'ETH_RXP_MAG')
    j_rx_n = _require_net(base.pad_number(j3, 6), 'ETH_RXN_MAG')

    # Chip-side stubs are short, via-free F.Cu routes. The release placement
    # alternates the damping-resistor rotations so these four connections retain
    # source order without crossing, while their MagJack-side pads already match
    # J3's reversed pair ordering.
    for src, dst in (
        (tx_p, r_txp_c),
        (tx_n, r_txn_c),
        (rx_p, r_rxp_c),
        (rx_n, r_rxn_c),
    ):
        base.add_track(board, _xy(src), _xy(dst), F, base.net_from_pad(src), base.WIDTH_ETH_MM)

    # RX pair stays entirely on F.Cu and is via-free. Stop just left of the
    # MagJack alternating pad column, then enter each PTH horizontally on its
    # exact Y. With the frozen 1.5 mm PTH diameter this preserves >0.20 mm
    # copper clearance to the adjacent offset PTH row.
    for src, dst in ((r_rxp_m, j_rx_p), (r_rxn_m, j_rx_n)):
        s = _xy(src)
        d = _xy(dst)
        base.add_polyline(
            board,
            [s, (128.0, d[1]), d],
            F,
            base.net_from_pad(src),
            base.WIDTH_ETH_MM,
        )

    # TX pair transitions once per conductor to In2.Cu. TXP leaves horizontally;
    # TXN uses a short F.Cu diagonal to y=63.0 before its via, avoiding the TXP
    # In2 corridor. Both then stop at x=128 and enter their MagJack PTH on the
    # exact destination Y, so neither track cuts through the alternating TCT/RXP
    # pins. This topology is ~13/14 mm end-to-end with one via each.
    stxp = _xy(r_txp_m)
    stxn = _xy(r_txn_m)
    dtxp = _xy(j_tx_p)
    dtxn = _xy(j_tx_n)
    vtxp = (stxp[0] + 0.95, stxp[1])
    vtxn = (stxn[0] + 0.95, dtxn[1] + 0.54)  # 63.00 mm for frozen J3 geometry

    base.add_track(board, stxp, vtxp, F, base.net_from_pad(r_txp_m), base.WIDTH_ETH_MM)
    base.add_via(board, vtxp, base.net_from_pad(r_txp_m))
    base.add_polyline(
        board,
        [vtxp, (128.0, dtxp[1]), dtxp],
        IN2,
        base.net_from_pad(r_txp_m),
        base.WIDTH_ETH_MM,
    )

    base.add_track(board, stxn, vtxn, F, base.net_from_pad(r_txn_m), base.WIDTH_ETH_MM)
    base.add_via(board, vtxn, base.net_from_pad(r_txn_m))
    base.add_polyline(
        board,
        [vtxn, (128.0, dtxn[1]), dtxn],
        IN2,
        base.net_from_pad(r_txn_m),
        base.WIDTH_ETH_MM,
    )

    print(
        'ETHERNET_CRITICAL_PREROUTE: PASS '
        'J3=90deg damping-split TX=In2/1via RX=F.Cu/0via exact-Y PTH approaches'
    )


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f'cannot load board: {path}')
    route_ethernet(board)
    base.route_usb(board)
    board.BuildConnectivity()
    conn = board.GetConnectivity()
    conn.Build(board)
    conn.RecalculateRatsnest()
    pcbnew.SaveBoard(str(path), board)
    print('CRITICAL_PREROUTE_LOCKED: PASS USB + Ethernet MDI')
    print('  unrouted_after_critical_preroute=%d' % int(conn.GetUnconnectedCount(False)))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: pre_route_critical_nets_release.py BOARD')
    main(sys.argv[1])
