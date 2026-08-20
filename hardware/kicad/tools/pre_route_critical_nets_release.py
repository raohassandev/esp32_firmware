#!/usr/bin/env python3
"""Release critical-route wrapper for Rev-A USB + current W5500 MDI topology.

The generic autorouter is deliberately not allowed to define the long USB pair
or W5500 MDI topology. This wrapper owns only the minimum critical trunks needed
for the frozen SI contract; short USB-C duplicate-contact and ESD branches are
left to Freerouting so the dense Type-C fanout is not over-constrained.

Run #28 proved that direct diagonal escapes from the W5500 and a fully manual
Type-C duplicate fanout were too aggressive for the actual pad geometry. The
release topology below therefore:
- escapes every W5500 MDI pin straight out of the 0.5 mm-pitch pad row before
  fanning to the damping resistors;
- moves the long USB D+/D- trunks to In2.Cu after a symmetric via pair, avoiding
  the dense 5 V power block on F.Cu;
- connects only the A6/A7 Type-C data contacts in the locked trunk. B6/B7 and
  the USB ESD branches remain ordinary same-net branches for Freerouting.
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


def _escape_to(board, src, dst, x_escape):
    """Leave the W5500 pad row horizontally before any vertical fan-out."""
    s = _xy(src)
    d = _xy(dst)
    e = (x_escape, s[1])
    base.add_track(board, s, e, pcbnew.F_Cu, base.net_from_pad(src), base.WIDTH_ETH_MM)
    base.add_track(board, e, d, pcbnew.F_Cu, base.net_from_pad(src), base.WIDTH_ETH_MM)


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

    # J1B1211CCD physical pin mapping at the frozen 90-degree orientation.
    j_tx_p = _require_net(base.pad_number(j3, 1), 'ETH_TXP_MAG')
    j_tx_n = _require_net(base.pad_number(j3, 3), 'ETH_TXN_MAG')
    j_rx_p = _require_net(base.pad_number(j3, 4), 'ETH_RXP_MAG')
    j_rx_n = _require_net(base.pad_number(j3, 6), 'ETH_RXN_MAG')

    # Run #28 showed that diagonal tracks beginning directly at the 0.5 mm-pitch
    # W5500 row clipped adjacent pads. Escape all four conductors horizontally
    # beyond the package edge first; only then fan to the staggered resistors.
    x_escape = 121.45
    for src, dst in (
        (tx_p, r_txp_c),
        (tx_n, r_txn_c),
        (rx_p, r_rxp_c),
        (rx_n, r_rxn_c),
    ):
        _escape_to(board, src, dst, x_escape)

    # RX remains via-free on F.Cu. Route to an x=128 staging line, then enter
    # each alternating MagJack PTH on its exact destination Y.
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

    # TX uses one symmetric transition per conductor. Do not share a diagonal
    # corridor on F.Cu: first leave each resistor horizontally to its own via,
    # then route entirely on In2.Cu to the exact MagJack PTH Y.
    stxp = _xy(r_txp_m)
    stxn = _xy(r_txn_m)
    dtxp = _xy(j_tx_p)
    dtxn = _xy(j_tx_n)
    vtxp = (stxp[0] + 0.95, stxp[1])
    vtxn = (stxn[0] + 0.95, stxn[1])

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
        'straight-pad-escape + damping split; TX=In2/1via RX=F.Cu/0via'
    )


def route_usb(board):
    F = pcbnew.F_Cu
    IN2 = board.GetLayerID('In2.Cu')
    u1 = base.footprint(board, base.semantic_ref('U1'))
    j2 = base.footprint(board, base.semantic_ref('J_USB'))
    rdm = base.footprint(board, base.semantic_ref('R_MCU_DM_SER'))
    rdp = base.footprint(board, base.semantic_ref('R_MCU_DP_SER'))

    mcu_dm = _require_net(base.pad_number(u1, 13), 'USB_D-_MCU')
    mcu_dp = _require_net(base.pad_number(u1, 14), 'USB_D+_MCU')
    rdm_mcu = base.pad_net(rdm, 'USB_D-_MCU')
    rdm_ext = base.pad_net(rdm, 'USB_D-')
    rdp_mcu = base.pad_net(rdp, 'USB_D+_MCU')
    rdp_ext = base.pad_net(rdp, 'USB_D+')

    # Both MCU data pads are on the same ESP32 edge. Escape outward (+Y) before
    # turning toward the series resistors. Run #28's DM -Y escape passed within
    # 0.165 mm of U1 pad 15, so no critical track is allowed on that inside edge.
    mdm = _xy(mcu_dm)
    mdp = _xy(mcu_dp)
    dm_mcu_dst = _xy(rdm_mcu)
    dp_mcu_dst = _xy(rdp_mcu)
    base.add_polyline(
        board,
        [
            mdm,
            (mdm[0], mdm[1] + 1.05),
            (21.0, mdm[1] + 1.05),
            (22.0, dm_mcu_dst[1]),
            dm_mcu_dst,
        ],
        F,
        base.net_from_pad(mcu_dm),
        base.WIDTH_USB_MM,
    )
    base.add_polyline(
        board,
        [
            mdp,
            (mdp[0], mdp[1] + 2.25),
            (21.5, mdp[1] + 2.25),
            dp_mcu_dst,
        ],
        F,
        base.net_from_pad(mcu_dp),
        base.WIDTH_USB_MM,
    )

    # Lock only the primary A6/A7 contacts. The interleaved B6/B7 duplicate
    # contacts and D1/D2 ESD branches are intentionally left for Freerouting as
    # short local branches; manually forcing all four contacts caused the Type-C
    # shorts/crossings seen in Run #28.
    a6 = _require_net(base.pad_number(j2, 'A6'), 'USB_D+')
    a7 = _require_net(base.pad_number(j2, 'A7'), 'USB_D-')

    sdm = _xy(rdm_ext)
    sdp = _xy(rdp_ext)
    adm = _xy(a7)
    adp = _xy(a6)

    # Symmetric two-via trunks. The long run is on In2.Cu, below the continuous
    # In1.Cu/L2 GND reference, and therefore passes under the dense 5 V power
    # block without colliding with its F.Cu SMD pads.
    dm_v1 = (26.0, sdm[1])
    dp_v1 = (26.0, sdp[1])
    dm_v2 = (135.0, 46.0)
    dp_v2 = (135.0, 46.8)

    base.add_track(board, sdm, dm_v1, F, base.net_from_pad(rdm_ext), base.WIDTH_USB_MM)
    base.add_via(board, dm_v1, base.net_from_pad(rdm_ext))
    base.add_polyline(
        board,
        [dm_v1, (35.0, 51.5), (127.0, 51.5), dm_v2],
        IN2,
        base.net_from_pad(rdm_ext),
        base.WIDTH_USB_MM,
    )
    base.add_via(board, dm_v2, base.net_from_pad(rdm_ext))
    base.add_track(board, dm_v2, adm, F, base.net_from_pad(rdm_ext), base.WIDTH_USB_MM)

    base.add_track(board, sdp, dp_v1, F, base.net_from_pad(rdp_ext), base.WIDTH_USB_MM)
    base.add_via(board, dp_v1, base.net_from_pad(rdp_ext))
    base.add_polyline(
        board,
        [dp_v1, (36.0, 52.2), (127.5, 52.2), dp_v2],
        IN2,
        base.net_from_pad(rdp_ext),
        base.WIDTH_USB_MM,
    )
    base.add_via(board, dp_v2, base.net_from_pad(rdp_ext))
    base.add_track(board, dp_v2, adp, F, base.net_from_pad(rdp_ext), base.WIDTH_USB_MM)

    print(
        'USB_CRITICAL_PREROUTE: PASS '
        'primary A6/A7 trunk on In2; 2 symmetric vias/conductor; local branches deferred'
    )


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f'cannot load board: {path}')
    route_ethernet(board)
    route_usb(board)
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
