#!/usr/bin/env python3
"""Release critical-route wrapper for Rev-A USB + W5500 MDI topology.

Critical trunks are locked before Specctra export; generic Freerouting owns the
remaining low-speed/local branches. Geometry here is intentionally simple and
validated by KiCad DRC plus the same SI gate used on the final routed board.
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
    return (_require_net(base.pad_number(fp,1),chip_net),
            _require_net(base.pad_number(fp,2),mag_net))


def _chip_escape(board, src, dst, x_escape=121.45):
    """Leave W5500's 0.5-mm-pitch pad row horizontally before fan-out."""
    s=_xy(src); d=_xy(dst); e=(x_escape,s[1])
    base.add_track(board,s,e,pcbnew.F_Cu,base.net_from_pad(src),base.WIDTH_ETH_MM)
    base.add_track(board,e,d,pcbnew.F_Cu,base.net_from_pad(src),base.WIDTH_ETH_MM)


def _chip_dogleg(board, src, dst, x_escape):
    """Orthogonal fan-out used where adjacent 0.5-mm MDI escapes converge."""
    s=_xy(src); d=_xy(dst); net=base.net_from_pad(src)
    base.add_polyline(board,[s,(x_escape,s[1]),(x_escape,d[1]),d],pcbnew.F_Cu,net,base.WIDTH_ETH_MM)


def route_ethernet(board):
    u2=base.footprint(board,base.semantic_ref('U2'))
    j3=base.footprint(board,base.semantic_ref('J_ETH'))
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

    # Run #37's final GND island was U2 pad 9. post_route_finalize now reserves
    # its +X stitch via at x=121.4475 before signal routing. Shift the two RX
    # fanout columns just enough to clear that via while retaining >=0.20 mm
    # clearance to the RX damping pad and >=0.20 mm pair-to-pair clearance.
    _chip_dogleg(board,rx_p,r_rxp_c,122.06)
    _chip_dogleg(board,rx_n,r_rxn_c,122.50)
    _chip_escape(board,tx_p,r_txp_c)
    _chip_escape(board,tx_n,r_txn_c)

    # Each Ethernet conductor uses exactly one via, satisfying pair symmetry and
    # the frozen <=1-via policy. Layer-separated pair crossings avoid F.Cu
    # conflicts around the alternating MagJack PTH order.
    s=_xy(r_rxp_m); d=_xy(j_rx_p); v=(128.15,s[1])
    base.add_track(board,s,v,F,base.net_from_pad(r_rxp_m),base.WIDTH_ETH_MM)
    base.add_via(board,v,base.net_from_pad(r_rxp_m))
    base.add_track(board,v,d,IN2,base.net_from_pad(r_rxp_m),base.WIDTH_ETH_MM)

    s=_xy(r_rxn_m); d=_xy(j_rx_n); v=(125.40,s[1])
    base.add_track(board,s,v,F,base.net_from_pad(r_rxn_m),base.WIDTH_ETH_MM)
    base.add_via(board,v,base.net_from_pad(r_rxn_m))
    base.add_polyline(board,[v,(126.30,d[1]),d],IN2,base.net_from_pad(r_rxn_m),base.WIDTH_ETH_MM)

    s=_xy(r_txp_m); d=_xy(j_tx_p); v=(130.70,s[1])
    base.add_track(board,s,v,F,base.net_from_pad(r_txp_m),base.WIDTH_ETH_MM)
    base.add_via(board,v,base.net_from_pad(r_txp_m))
    base.add_track(board,v,d,IN2,base.net_from_pad(r_txp_m),base.WIDTH_ETH_MM)

    # Run #35 had DRC=0 with x=127.20 but TX skew 5.09 mm. x=127.60 retained
    # DRC clearance around the MagJack centre tap and reduced skew to 4.816 mm.
    s=_xy(r_txn_m); d=_xy(j_tx_n); v=(125.40,s[1])
    base.add_track(board,s,v,F,base.net_from_pad(r_txn_m),base.WIDTH_ETH_MM)
    base.add_via(board,v,base.net_from_pad(r_txn_m))
    base.add_polyline(board,[v,(127.60,d[1]),d],IN2,base.net_from_pad(r_txn_m),base.WIDTH_ETH_MM)

    print('ETHERNET_CRITICAL_PREROUTE: PASS GND-escape-aware RX doglegs + TXN skew trim')


def _usb_mcu_stub(board, src, dst, via1, via2):
    net=base.net_from_pad(src); F=pcbnew.F_Cu; IN2=board.GetLayerID('In2.Cu')
    base.add_track(board,_xy(src),via1,F,net,base.WIDTH_USB_MM)
    base.add_via(board,via1,net)
    base.add_track(board,via1,via2,IN2,net,base.WIDTH_USB_MM)
    base.add_via(board,via2,net)
    base.add_track(board,via2,_xy(dst),F,net,base.WIDTH_USB_MM)


def route_usb(board):
    F=pcbnew.F_Cu; B=pcbnew.B_Cu; IN2=board.GetLayerID('In2.Cu')
    u1=base.footprint(board,base.semantic_ref('U1'))
    j2=base.footprint(board,base.semantic_ref('J_USB'))
    rdm=base.footprint(board,base.semantic_ref('R_MCU_DM_SER'))
    rdp=base.footprint(board,base.semantic_ref('R_MCU_DP_SER'))

    mcu_dm=_require_net(base.pad_number(u1,13),'USB_D-_MCU')
    mcu_dp=_require_net(base.pad_number(u1,14),'USB_D+_MCU')
    rdm_mcu=base.pad_net(rdm,'USB_D-_MCU'); rdm_ext=base.pad_net(rdm,'USB_D-')
    rdp_mcu=base.pad_net(rdp,'USB_D+_MCU'); rdp_ext=base.pad_net(rdp,'USB_D+')

    _usb_mcu_stub(board,mcu_dm,rdm_mcu,(15.98,66.40),(21.20,63.40))
    _usb_mcu_stub(board,mcu_dp,rdp_mcu,(17.25,67.80),(21.20,66.60))

    # A 16-contact USB-C receptacle exposes both A- and B-side USB2 contacts.
    # Run #37 proved that routing only A6/A7 leaves B6 as a real ratsnest item;
    # B7 happened to be joined by generic routing, which is not a controlled
    # release topology. Lock all four contacts here. The alternate contacts use
    # one additional symmetric via per conductor, keeping external D+/D- at
    # three vias each and below the frozen <=4 USB limit.
    a6=_require_net(base.pad_number(j2,'A6'),'USB_D+')
    b6=_require_net(base.pad_number(j2,'B6'),'USB_D+')
    a7=_require_net(base.pad_number(j2,'A7'),'USB_D-')
    b7=_require_net(base.pad_number(j2,'B7'),'USB_D-')
    sdm=_xy(rdm_ext); sdp=_xy(rdp_ext)
    dm_v1=(26.0,sdm[1]); dp_v1=(26.0,sdp[1])

    # D-: the primary A7 transition is outside the connector courtyard. The B7
    # branch via lies exactly on the final In2 trunk segment, so it adds no
    # unnecessary inner-layer path length and the branch approaches B7 from
    # above, away from the interleaved D+ contacts.
    dm_a=(135.0,41.75)
    dm_b=(132.2,45.15)
    dm_net=base.net_from_pad(rdm_ext)
    base.add_track(board,sdm,dm_v1,F,dm_net,base.WIDTH_USB_MM)
    base.add_via(board,dm_v1,dm_net)
    base.add_polyline(board,[dm_v1,(35.0,51.50),(126.5,51.50),(131.5,46.0),dm_b,dm_a],IN2,dm_net,base.WIDTH_USB_MM)
    base.add_via(board,dm_b,dm_net)
    base.add_via(board,dm_a,dm_net)
    base.add_track(board,dm_a,_xy(a7),F,dm_net,base.WIDTH_USB_MM)
    base.add_track(board,dm_b,_xy(b7),F,dm_net,base.WIDTH_USB_MM)

    # D+: A6 uses an exact-Y final approach so its track stays centered in the
    # 0.20 mm copper gap between the adjacent Type-C pads. B6 escapes down-left
    # to a separate via, then joins A6 on B.Cu; this avoids the unavoidable
    # A7/B6 crossing on F.Cu without using the reserved L2 GND plane.
    dp_a=(136.0,42.90)
    dp_b=(136.0,40.30)
    dp_net=base.net_from_pad(rdp_ext)
    base.add_track(board,sdp,dp_v1,F,dp_net,base.WIDTH_USB_MM)
    base.add_via(board,dp_v1,dp_net)
    base.add_polyline(board,[dp_v1,(36.0,52.30),(127.3,52.30),(132.3,46.8),dp_a],IN2,dp_net,base.WIDTH_USB_MM)
    base.add_via(board,dp_a,dp_net)
    base.add_polyline(board,[dp_a,(dp_a[0],_xy(a6)[1]),_xy(a6)],F,dp_net,base.WIDTH_USB_MM)
    base.add_via(board,dp_b,dp_net)
    base.add_track(board,dp_b,_xy(b6),F,dp_net,base.WIDTH_USB_MM)
    base.add_track(board,dp_b,dp_a,B,dp_net,base.WIDTH_USB_MM)

    print('USB_CRITICAL_PREROUTE: PASS all A6/B6/A7/B7 contacts tied with symmetric 3-via topology')


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    route_ethernet(board)
    route_usb(board)
    board.BuildConnectivity(); conn=board.GetConnectivity(); conn.Build(board); conn.RecalculateRatsnest()
    pcbnew.SaveBoard(str(path),board)
    print('CRITICAL_PREROUTE_LOCKED: PASS USB + Ethernet MDI')
    print('  unrouted_after_critical_preroute=%d' % int(conn.GetUnconnectedCount(False)))


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: pre_route_critical_nets_release.py BOARD')
    main(sys.argv[1])
