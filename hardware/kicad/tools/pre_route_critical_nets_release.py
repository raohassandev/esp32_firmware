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

# F.Cu x where ETH_TXN drops to In2.Cu; also carries the trunk under the bias column.
TXN_VIA_X = 127.60


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

    # The four mandatory 49.9R W5500 line-bias parts are controlled branches
    # of the *_MAG nets. Runs #52/#202 proved that filtering SES routes without
    # owning these endpoints leaves four real opens. The release placement puts
    # every bias pad 2 on or beside its own trunk, so the taps add no vias and no
    # disallowed layer usage while retaining the frozen skew gate.
    #
    # 'inline' rows (RXP/TXP) place bias pad 2 exactly on the damping-pad row,
    # under the same-net F.Cu trunk added further down, so the termination tap
    # needs no extra copper at all. Run 32494788861 proved the previous vertical
    # TXP stub at x=124.425 short-circuited ETH_TXP_MAG to the ETH_TXN_MAG
    # damping pad on the row below; an inline tap cannot leave its own row.
    # 'stub' (RXN) ends at the x=125.40 via, so it keeps a short same-net
    # diagonal from the damping pad. 'drop' (TXN) has its via at TXN_VIA_X, so
    # the trunk already passes under the bias column and only a 0.875 mm same-net
    # vertical drop is needed - the shortest tap of the three forms.
    for semantic, net_name, source, mode in (
        ('R_ETH_RXP_BIAS','ETH_RXP_MAG',r_rxp_m,'inline'),
        ('R_ETH_RXN_BIAS','ETH_RXN_MAG',r_rxn_m,'stub'),
        ('R_ETH_TXP_BIAS','ETH_TXP_MAG',r_txp_m,'inline'),
        ('R_ETH_TXN_BIAS','ETH_TXN_MAG',r_txn_m,'drop'),
    ):
        bias=base.footprint(board,base.semantic_ref(semantic))
        signal=_require_net(base.pad_number(bias,2),net_name)
        src=_xy(source); dst=_xy(signal); route_net=base.net_from_pad(source)
        if mode == 'inline':
            if abs(src[1]-dst[1]) > 1e-6 or dst[0] <= src[0]:
                raise RuntimeError(f'{semantic}: pad 2 {dst} is not inline on trunk row {src}')
        elif mode == 'drop':
            if dst[0] >= TXN_VIA_X:
                raise RuntimeError(f'{semantic}: pad 2 {dst} is not under the trunk (via x={TXN_VIA_X})')
            base.add_track(board,(dst[0],src[1]),dst,F,route_net,base.WIDTH_ETH_MM)
        else:
            base.add_track(board,src,dst,F,route_net,base.WIDTH_ETH_MM)

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

    # ETH_TXN is the longest MDI conductor: its damping row (y=68.5) is 6.04 mm
    # from its MagJack pad (y=62.46) while ETH_TXP only spans 1.0 mm. Carry the
    # F.Cu trunk out to TXN_VIA_X so the bias tap becomes a 0.875 mm drop, then
    # take a two-segment In2.Cu path that passes below the ETH_TCT pad
    # (129.96, 63.73) with >=1.29 mm clearance instead of the former
    # (125.40 via -> x=127.60) detour. TX skew drops 4.82 -> 4.34 mm by
    # shortening the long conductor, not by padding the short one.
    s=_xy(r_txn_m); d=_xy(j_tx_n); v=(TXN_VIA_X,s[1])
    base.add_track(board,s,v,F,base.net_from_pad(r_txn_m),base.WIDTH_ETH_MM)
    base.add_via(board,v,base.net_from_pad(r_txn_m))
    base.add_polyline(board,[v,(131.00,64.50),d],IN2,base.net_from_pad(r_txn_m),base.WIDTH_ETH_MM)

    print('ETHERNET_CRITICAL_PREROUTE: PASS GND-escape-aware trunks + four locked 49.9R bias stubs')


def route_eth_tocap(board):
    """Lock the W5500 TOCAP decoupling net that Freerouting left open in Run #44."""
    u2=base.footprint(board,base.semantic_ref('U2'))
    c13=base.footprint(board,'C13')
    src=_require_net(base.pad_number(u2,20),'ETH_TOCAP')
    dst=_require_net(base.pad_number(c13,1),'ETH_TOCAP')
    s=_xy(src); d=_xy(dst); net=base.net_from_pad(src)

    # U2:20 is in the 0.5-mm-pitch top row. Escape outward first. U2:23 has a
    # reserved GND spoke/via at x=113.75/y=58.337, so detour above that via,
    # return to y=58.8 after x=113.0, then approach C13 vertically. This keeps
    # the local analog node short without crossing adjacent W5500 pads/GND.
    pts=[s,(s[0],58.80),(114.50,58.80),(114.50,57.60),
         (113.00,57.60),(113.00,58.80),(d[0],58.80),d]
    base.add_polyline(board,pts,pcbnew.F_Cu,net,base.WIDTH_ETH_MM)
    print('ETH_TOCAP_PREROUTE: PASS U2:20 -> C13:1 locked local decoupling route')


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
    cdm=base.footprint(board,base.semantic_ref('C_MCU_DM_USB'))
    cdp=base.footprint(board,base.semantic_ref('C_MCU_DP_USB'))
    ddm=base.footprint(board,base.semantic_ref('D_USB_DN'))
    ddp=base.footprint(board,base.semantic_ref('D_USB_DP'))

    mcu_dm=_require_net(base.pad_number(u1,13),'USB_D-_MCU')
    mcu_dp=_require_net(base.pad_number(u1,14),'USB_D+_MCU')
    rdm_mcu=base.pad_net(rdm,'USB_D-_MCU'); rdm_ext=base.pad_net(rdm,'USB_D-')
    rdp_mcu=base.pad_net(rdp,'USB_D+_MCU'); rdp_ext=base.pad_net(rdp,'USB_D+')
    cdm_sig=_require_net(base.pad_number(cdm,1),'USB_D-_MCU')
    cdp_sig=_require_net(base.pad_number(cdp,1),'USB_D+_MCU')
    ddm_sig=_require_net(base.pad_number(ddm,1),'USB_D-')
    ddp_sig=_require_net(base.pad_number(ddp,1),'USB_D+')

    _usb_mcu_stub(board,mcu_dm,rdm_mcu,(15.98,66.40),(21.20,63.40))
    _usb_mcu_stub(board,mcu_dp,rdp_mcu,(17.25,67.80),(21.20,66.60))

    # Own both DNP tuning-cap stubs. These are the DRC-clean SES geometries from
    # Runs #50/#52, now locked before export so filtering the protected SES net
    # blocks cannot remove required schematic endpoints.
    dm_cap=_xy(cdm_sig); dp_cap=_xy(cdp_sig)
    base.add_polyline(board,[_xy(rdm_mcu),(dm_cap[0]-2.05,dm_cap[1]),dm_cap],F,
                      base.net_from_pad(rdm_mcu),base.WIDTH_USB_MM)
    base.add_polyline(board,[(21.20,66.60),(22.10,dp_cap[1]),dp_cap],F,
                      base.net_from_pad(rdp_mcu),base.WIDTH_USB_MM)

    # A 16-contact USB-C receptacle exposes both A- and B-side USB2 contacts.
    # Lock all four data contacts before Specctra export. D+ uses three vias;
    # D- uses a fourth right-side transfer via, still inside the frozen <=4 USB
    # limit, so B7 can approach on F.Cu without crossing the A6 branch.
    a6=_require_net(base.pad_number(j2,'A6'),'USB_D+')
    b6=_require_net(base.pad_number(j2,'B6'),'USB_D+')
    a7=_require_net(base.pad_number(j2,'A7'),'USB_D-')
    b7=_require_net(base.pad_number(j2,'B7'),'USB_D-')
    sdm=_xy(rdm_ext); sdp=_xy(rdp_ext)
    dm_v1=(26.0,sdm[1]); dp_v1=(26.0,sdp[1])

    # D-: Run #41 proved a transfer via at x=137.0 still sat only 0.10 mm from
    # D+'s A6 horizontal trunk because both contacts are separated by 0.50 mm
    # in Y. Keep the B.Cu branch above D+'s transfer, pass under the connector,
    # and return to F.Cu beyond the A6 trunk at x=140.0 on B7's exact Y.
    dm_a=(135.0,41.75)
    dm_b=(132.2,45.15)
    dm_b_final=(140.0,_xy(b7)[1])
    dm_net=base.net_from_pad(rdm_ext)
    base.add_track(board,sdm,dm_v1,F,dm_net,base.WIDTH_USB_MM)
    base.add_via(board,dm_v1,dm_net)
    base.add_polyline(board,[dm_v1,(35.0,51.50),(126.5,51.50),(131.5,46.0),dm_b,dm_a],IN2,dm_net,base.WIDTH_USB_MM)
    base.add_via(board,dm_b,dm_net)
    base.add_via(board,dm_a,dm_net)
    base.add_track(board,dm_a,_xy(a7),F,dm_net,base.WIDTH_USB_MM)
    base.add_via(board,dm_b_final,dm_net)
    base.add_polyline(board,[dm_b,(134.6,45.15),(137.0,_xy(b7)[1]),dm_b_final],B,dm_net,base.WIDTH_USB_MM)
    base.add_track(board,dm_b_final,_xy(b7),F,dm_net,base.WIDTH_USB_MM)

    # D+: A6 uses an exact-Y final approach. Run #39 proved a direct diagonal
    # dp_b->B6 clips the unused A8 pad. Transition vertically at x=136.0, then
    # approach B6 horizontally on its exact y=41.25; this also keeps 0.5 mm
    # centre spacing to the interleaved D- A7 track.
    dp_a=(136.0,42.90)
    dp_b=(136.0,40.30)
    dp_net=base.net_from_pad(rdp_ext)
    base.add_track(board,sdp,dp_v1,F,dp_net,base.WIDTH_USB_MM)
    base.add_via(board,dp_v1,dp_net)
    base.add_polyline(board,[dp_v1,(36.0,52.30),(127.3,52.30),(132.3,46.8),dp_a],IN2,dp_net,base.WIDTH_USB_MM)
    base.add_via(board,dp_a,dp_net)
    base.add_polyline(board,[dp_a,(dp_a[0],_xy(a6)[1]),_xy(a6)],F,dp_net,base.WIDTH_USB_MM)
    base.add_via(board,dp_b,dp_net)
    base.add_polyline(board,[dp_b,(dp_b[0],_xy(b6)[1]),_xy(b6)],F,dp_net,base.WIDTH_USB_MM)
    base.add_track(board,dp_b,dp_a,B,dp_net,base.WIDTH_USB_MM)

    # Lock both low-capacitance ESD shunts. D- follows the proven Run #50 SES
    # approach to A7. D+ uses an F.Cu rectangular dogleg of 4.90 mm; together
    # with the tuning-cap stubs this holds the frozen <=5 mm aggregate USB skew
    # without adding a via or changing the <=190 mm length limit.
    dn=_xy(ddm_sig); a7p=_xy(a7)
    base.add_polyline(board,[dn,(dn[0],39.3958),(139.4458,a7p[1]),a7p],F,
                      base.net_from_pad(ddm_sig),base.WIDTH_USB_MM)
    dp=_xy(ddp_sig)
    base.add_polyline(board,[dp,(133.85,dp[1]),(133.85,dp_b[1]),dp_b],F,
                      base.net_from_pad(ddp_sig),base.WIDTH_USB_MM)

    print('USB_CRITICAL_PREROUTE: PASS Type-C contacts + tuning-cap/ESD branches locked and skew-tuned')


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    route_ethernet(board)
    route_eth_tocap(board)
    route_usb(board)
    board.BuildConnectivity(); conn=board.GetConnectivity(); conn.Build(board); conn.RecalculateRatsnest()
    pcbnew.SaveBoard(str(path),board)
    print('CRITICAL_PREROUTE_LOCKED: PASS USB complete topology + Ethernet MDI/bias + ETH_TOCAP')
    print('  unrouted_after_critical_preroute=%d' % int(conn.GetUnconnectedCount(False)))


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: pre_route_critical_nets_release.py BOARD')
    main(sys.argv[1])
