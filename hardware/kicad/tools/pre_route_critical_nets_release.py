#!/usr/bin/env python3
"""Release critical-route wrapper for Rev-A USB + current W5500 MDI topology.

USB routing is shared with pre_route_critical_nets.py. Ethernet MDI routing here
matches the current four 0R damping resistors that split each W5500 conductor
into chip-side and MagJack-side nets. All critical tracks are locked before DSN
export so Freerouting is limited to the remaining non-critical routing problem.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as base


def _xy(p): return base.xy_mm(p.GetPosition())


def _require_net(p, name):
    if p.GetNetname() != name:
        raise RuntimeError(f'{p.GetParent().GetReference()} pad {p.GetNumber()}: {p.GetNetname()} != {name}')
    return p


def _resistor(board, semantic, chip_net, mag_net):
    fp=base.footprint(board, base.semantic_ref(semantic))
    p1=_require_net(base.pad_number(fp,1), chip_net)
    p2=_require_net(base.pad_number(fp,2), mag_net)
    return p1,p2


def route_ethernet(board):
    u2=base.footprint(board, base.semantic_ref('U2'))
    j3=base.footprint(board, base.semantic_ref('J_ETH'))
    F=pcbnew.F_Cu
    IN2=board.GetLayerID('In2.Cu')

    tx_n=_require_net(base.pad_number(u2,1),'ETH_TXN')
    tx_p=_require_net(base.pad_number(u2,2),'ETH_TXP')
    rx_n=_require_net(base.pad_number(u2,5),'ETH_RXN')
    rx_p=_require_net(base.pad_number(u2,6),'ETH_RXP')

    r_txp_c,r_txp_m=_resistor(board,'R_ETH_TXP_DAMP','ETH_TXP','ETH_TXP_MAG')
    r_txn_c,r_txn_m=_resistor(board,'R_ETH_TXN_DAMP','ETH_TXN','ETH_TXN_MAG')
    r_rxp_c,r_rxp_m=_resistor(board,'R_ETH_RXP_DAMP','ETH_RXP','ETH_RXP_MAG')
    r_rxn_c,r_rxn_m=_resistor(board,'R_ETH_RXN_DAMP','ETH_RXN','ETH_RXN_MAG')

    j_tx_p=_require_net(base.pad_number(j3,1),'ETH_TXP_MAG')
    j_tx_n=_require_net(base.pad_number(j3,3),'ETH_TXN_MAG')
    j_rx_p=_require_net(base.pad_number(j3,4),'ETH_RXP_MAG')
    j_rx_n=_require_net(base.pad_number(j3,6),'ETH_RXN_MAG')

    # Chip-side MDI stubs stay entirely on F.Cu and are deliberately short.
    for src,dst in ((tx_p,r_txp_c),(tx_n,r_txn_c),(rx_p,r_rxp_c),(rx_n,r_rxn_c)):
        base.add_track(board,_xy(src),_xy(dst),F,base.net_from_pad(src),base.WIDTH_ETH_MM)

    # TX pair: one via per conductor after its damping resistor, then In2.Cu to
    # the through-hole MagJack pins. Keeping both conductors on the same layer
    # gives equal transition count while leaving F.Cu available for the RX pair.
    stxp=_xy(r_txp_m); stxn=_xy(r_txn_m)
    dtxp=_xy(j_tx_p); dtxn=_xy(j_tx_n)
    vtxp=(stxp[0]+0.95, stxp[1])
    vtxn=(stxn[0]+0.95, stxn[1])
    base.add_track(board,stxp,vtxp,F,base.net_from_pad(r_txp_m),base.WIDTH_ETH_MM)
    base.add_via(board,vtxp,base.net_from_pad(r_txp_m))
    base.add_polyline(board,[vtxp,(127.0,vtxp[1]),(130.0,dtxp[1]-0.35),dtxp],IN2,base.net_from_pad(r_txp_m),base.WIDTH_ETH_MM)
    base.add_track(board,stxn,vtxn,F,base.net_from_pad(r_txn_m),base.WIDTH_ETH_MM)
    base.add_via(board,vtxn,base.net_from_pad(r_txn_m))
    base.add_polyline(board,[vtxn,(127.0,vtxn[1]),(130.0,dtxn[1]+0.35),dtxn],IN2,base.net_from_pad(r_txn_m),base.WIDTH_ETH_MM)

    # RX pair: via-free F.Cu routes approach the two MagJack RX pins from the
    # right. The upper/lower corridors preserve conductor ordering and stay
    # clear of the TX MagJack pins before the final pad approaches.
    srxp=_xy(r_rxp_m); srxn=_xy(r_rxn_m)
    drxp=_xy(j_rx_p); drxn=_xy(j_rx_n)
    rxp_pts=[srxp,(129.0,62.8),(134.8,64.0),(137.0,66.0),(137.0,drxp[1]),drxp]
    rxn_pts=[srxn,(126.0,65.0),(128.0,68.5),(131.0,70.5),(138.0,72.5),(138.0,drxn[1]),drxn]
    base.add_polyline(board,rxp_pts,F,base.net_from_pad(r_rxp_m),base.WIDTH_ETH_MM)
    base.add_polyline(board,rxn_pts,F,base.net_from_pad(r_rxn_m),base.WIDTH_ETH_MM)

    print('ETHERNET_CRITICAL_PREROUTE: PASS 4x damping split; TX=In2/1via RX=F.Cu/0via')


def main(board_path):
    path=Path(board_path)
    board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    route_ethernet(board)
    base.route_usb(board)
    board.BuildConnectivity()
    conn=board.GetConnectivity(); conn.Build(board); conn.RecalculateRatsnest()
    pcbnew.SaveBoard(str(path),board)
    print('CRITICAL_PREROUTE_LOCKED: PASS USB + Ethernet MDI')
    print('  unrouted_after_critical_preroute=%d' % int(conn.GetUnconnectedCount(False)))


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: pre_route_critical_nets_release.py BOARD')
    main(sys.argv[1])
