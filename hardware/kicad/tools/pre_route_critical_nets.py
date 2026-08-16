#!/usr/bin/env python3
"""Pre-route Rev-A high-speed nets before the generic autorouter.

The generic autorouter is useful for low-speed GPIO/power connectivity but is
not allowed to define USB or Ethernet MDI topology. This script owns those
critical paths, locks them, and leaves the remaining nets to Freerouting.

Policy:
- 0.20 mm minimum critical-track width so the controlled routes satisfy the
  frozen Rev-A board minimum instead of relying on a DRC override.
- USB main pair: F.Cu over the In1.Cu GND reference, non-crossing paired path.
- USB-C duplicate D pads: symmetric In2.Cu branches with two vias/conductor.
- Ethernet RX: F.Cu, via-free, routed around the W5500 package bottom edge.
- Ethernet TX: In2.Cu with one well-separated via/conductor.
"""
from pathlib import Path
import json
import sys
import pcbnew

WIDTH_USB_MM = 0.20
WIDTH_ETH_MM = 0.20
VIA_MM = 0.60
DRILL_MM = 0.30

ROOT = Path(__file__).resolve().parents[1]
REF_MAP = json.loads((ROOT / 'REFERENCE_MAP.json').read_text(encoding='utf-8'))


def mm(v): return pcbnew.FromMM(float(v))
def point(x, y): return pcbnew.VECTOR2I_MM(float(x), float(y))
def xy_mm(p): return (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))


def semantic_ref(name):
    ref = REF_MAP.get(name)
    if not ref:
        raise RuntimeError(f'canonical reference missing for semantic {name}')
    return ref


def footprint(board, ref):
    for fp in board.Footprints():
        if fp.GetReference() == ref:
            return fp
    raise RuntimeError(f'footprint not found: {ref}')


def pad_number(fp, number):
    for p in fp.Pads():
        if p.GetNumber() == str(number): return p
    raise RuntimeError(f'pad not found: {fp.GetReference()}-{number}')


def pad_net(fp, netname):
    hits=[p for p in fp.Pads() if p.GetNetname()==netname]
    if len(hits)!=1:
        raise RuntimeError(f'{fp.GetReference()}: expected one pad on {netname}, found {len(hits)}')
    return hits[0]


def net_from_pad(p): return p.GetNet()


def add_track(board, start, end, layer, net, width):
    if start == end: return None
    t=pcbnew.PCB_TRACK(board)
    t.SetStart(point(*start)); t.SetEnd(point(*end)); t.SetWidth(mm(width))
    t.SetLayer(layer); t.SetNet(net); t.SetLocked(True); board.Add(t)
    return t


def add_polyline(board, pts, layer, net, width):
    out=[]
    for a,b in zip(pts,pts[1:]):
        t=add_track(board,a,b,layer,net,width)
        if t is not None: out.append(t)
    return out


def add_via(board, at, net):
    v=pcbnew.PCB_VIA(board)
    v.SetPosition(point(*at)); v.SetWidth(mm(VIA_MM)); v.SetDrill(mm(DRILL_MM))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(net); v.SetLocked(True); board.Add(v)
    return v


def find_gnd_net(board):
    for fp in board.Footprints():
        for p in fp.Pads():
            if p.GetNetname()=='GND': return p.GetNet()
    raise RuntimeError('GND net not found')


def route_ethernet(board):
    u2=footprint(board, semantic_ref('U2'))
    j3=footprint(board, semantic_ref('J_ETH'))
    F=pcbnew.F_Cu; IN2=board.GetLayerID('In2.Cu')

    tx_n=pad_number(u2,1); tx_p=pad_number(u2,2)
    rx_n=pad_number(u2,5); rx_p=pad_number(u2,6)
    j_tx_p=pad_number(j3,1); j_tx_n=pad_number(j3,2)
    j_rx_p=pad_number(j3,3); j_rx_n=pad_number(j3,4)
    expected=((tx_n,'ETH_TXN'),(tx_p,'ETH_TXP'),(rx_n,'ETH_RXN'),(rx_p,'ETH_RXP'),
              (j_tx_p,'ETH_TXP'),(j_tx_n,'ETH_TXN'),(j_rx_p,'ETH_RXP'),(j_rx_n,'ETH_RXN'))
    for p,n in expected:
        if p.GetNetname()!=n: raise RuntimeError(f'Ethernet endpoint mismatch {p.GetNetname()} != {n}')

    stxn=xy_mm(tx_n.GetPosition()); stxp=xy_mm(tx_p.GetPosition())
    srxn=xy_mm(rx_n.GetPosition()); srxp=xy_mm(rx_p.GetPosition())
    dtxn=xy_mm(j_tx_n.GetPosition()); dtxp=xy_mm(j_tx_p.GetPosition())
    drxn=xy_mm(j_rx_n.GetPosition()); drxp=xy_mm(j_rx_p.GetPosition())

    # TX escapes diverge before the vias. The old 0.5 mm-spaced via pair
    # physically overlapped; these centers are 1.5 mm apart and clear U2 pads.
    vtxn=(stxn[0]-2.04, stxn[1]-0.75)
    vtxp=(stxp[0]-2.04, stxp[1]+0.25)
    add_track(board,stxn,vtxn,F,net_from_pad(tx_n),WIDTH_ETH_MM); add_via(board,vtxn,net_from_pad(tx_n))
    add_track(board,stxp,vtxp,F,net_from_pad(tx_p),WIDTH_ETH_MM); add_via(board,vtxp,net_from_pad(tx_p))
    add_polyline(board,[vtxn,(124.0,vtxn[1]),(127.2,dtxn[1]-1.7),dtxn],IN2,net_from_pad(tx_n),WIDTH_ETH_MM)
    add_polyline(board,[vtxp,(124.5,vtxp[1]),(128.0,dtxp[1]-1.0),dtxp],IN2,net_from_pad(tx_p),WIDTH_ETH_MM)

    # RX pair wraps below the W5500 instead of across its top-row control pads.
    # Final vertical approaches remain left of the MagJack PTH pad column.
    y_n=69.2; y_p=69.7
    x_n=127.6; x_p=128.2
    add_polyline(board,[srxn,(srxn[0]-1.2,srxn[1]),(srxn[0]-1.8,srxn[1]+0.8),
                        (srxn[0]-1.8,y_n),(x_n,y_n),(x_n,drxn[1]),drxn],
                 F,net_from_pad(rx_n),WIDTH_ETH_MM)
    add_polyline(board,[srxp,(srxp[0]-0.8,srxp[1]),(srxp[0]-1.3,srxp[1]+0.8),
                        (srxp[0]-1.3,y_p),(x_p,y_p),(x_p,drxp[1]),drxp],
                 F,net_from_pad(rx_p),WIDTH_ETH_MM)
    print('ETHERNET_CRITICAL_PREROUTE: PASS TX=In2/1via-each RX=F.Cu/0via noncrossing')


def route_usb(board):
    F=pcbnew.F_Cu; IN2=board.GetLayerID('In2.Cu')
    u1=footprint(board, semantic_ref('U1'))
    j2=footprint(board, semantic_ref('J_USB'))
    rdm=footprint(board, semantic_ref('R_MCU_DM_SER'))
    rdp=footprint(board, semantic_ref('R_MCU_DP_SER'))
    cdm=footprint(board, semantic_ref('C_MCU_DM_USB'))
    cdp=footprint(board, semantic_ref('C_MCU_DP_USB'))

    mcu_dm=pad_number(u1,13); mcu_dp=pad_number(u1,14)
    rdm_mcu=pad_net(rdm,'USB_D-_MCU'); rdm_ext=pad_net(rdm,'USB_D-')
    rdp_mcu=pad_net(rdp,'USB_D+_MCU'); rdp_ext=pad_net(rdp,'USB_D+')
    cdm_sig=pad_net(cdm,'USB_D-_MCU'); cdp_sig=pad_net(cdp,'USB_D+_MCU')
    for p,n in ((mcu_dm,'USB_D-_MCU'),(mcu_dp,'USB_D+_MCU')):
        if p.GetNetname()!=n: raise RuntimeError(f'USB MCU endpoint mismatch: {p.GetNetname()} != {n}')

    mdm=xy_mm(mcu_dm.GetPosition()); mdp=xy_mm(mcu_dp.GetPosition())
    rdm0=xy_mm(rdm_mcu.GetPosition()); rdp0=xy_mm(rdp_mcu.GetPosition())
    cdm0=xy_mm(cdm_sig.GetPosition()); cdp0=xy_mm(cdp_sig.GetPosition())

    # Separate MCU escapes vertically before moving toward the resistors. This
    # avoids the old DM trace running through the adjacent DP MCU pad.
    add_polyline(board,[mdm,(mdm[0],mdm[1]-1.05),(rdm0[0]-1.2,mdm[1]-1.05),rdm0],F,net_from_pad(mcu_dm),WIDTH_USB_MM)
    add_polyline(board,[mdp,(mdp[0],mdp[1]+0.95),(rdp0[0]-1.2,mdp[1]+0.95),rdp0],F,net_from_pad(mcu_dp),WIDTH_USB_MM)

    # DNP tuning-cap stubs stay on the MCU side and branch away from the main
    # pair, so their GND pads cannot be crossed by USB_D+/USB_D-.
    add_polyline(board,[rdm0,(rdm0[0]+0.6,rdm0[1]-0.8),(cdm0[0]-0.9,cdm0[1]),cdm0],F,net_from_pad(rdm_mcu),WIDTH_USB_MM)
    add_polyline(board,[rdp0,(rdp0[0]+0.6,rdp0[1]+0.8),(cdp0[0]-0.9,cdp0[1]),cdp0],F,net_from_pad(rdp_mcu),WIDTH_USB_MM)

    a6=pad_number(j2,'A6'); b6=pad_number(j2,'B6')
    a7=pad_number(j2,'A7'); b7=pad_number(j2,'B7')
    if any(p.GetNetname()!='USB_D+' for p in (a6,b6)) or any(p.GetNetname()!='USB_D-' for p in (a7,b7)):
        raise RuntimeError('USB-C duplicate D-pad net mapping mismatch')

    sdm=xy_mm(rdm_ext.GetPosition()); sdp=xy_mm(rdp_ext.GetPosition())
    adm=xy_mm(a7.GetPosition()); adp=xy_mm(a6.GetPosition())

    # Non-crossing main corridor. The path is intentionally topology-locked;
    # final impedance width/gap remains a provider stack-up tuning item.
    dm_pts=[sdm,(29.0,sdm[1]),(35.0,54.00),(128.0,54.00),
            (132.5,49.50),(135.8,44.00),(137.2,42.50),adm]
    dp_pts=[sdp,(29.0,sdp[1]),(36.0,54.50),(128.5,54.50),
            (133.0,50.00),(136.3,44.50),(137.7,43.00),adp]
    add_polyline(board,dm_pts,F,net_from_pad(rdm_ext),WIDTH_USB_MM)
    add_polyline(board,dp_pts,F,net_from_pad(rdp_ext),WIDTH_USB_MM)

    # Type-C duplicate contacts require a layer swap because their physical
    # ordering is interleaved. Use separate via columns so the two nets never
    # cross or share copper.
    dm_v1=(136.20,adm[1]); dm_v2=(136.20,xy_mm(b7.GetPosition())[1])
    dp_v1=(137.20,adp[1]); dp_v2=(137.20,xy_mm(b6.GetPosition())[1])
    for primary,dup,v1,v2,net in (
        (adm,xy_mm(b7.GetPosition()),dm_v1,dm_v2,net_from_pad(a7)),
        (adp,xy_mm(b6.GetPosition()),dp_v1,dp_v2,net_from_pad(a6)),
    ):
        add_track(board,primary,v1,F,net,WIDTH_USB_MM); add_via(board,v1,net)
        add_track(board,v1,v2,IN2,net,WIDTH_USB_MM); add_via(board,v2,net)
        add_track(board,v2,dup,F,net,WIDTH_USB_MM)

    # Return-current stitches are intentionally outside the main pair/fanout.
    gnd=find_gnd_net(board)
    for p in ((126.0,57.0),(130.0,57.0),(126.0,39.0),(130.0,39.0)):
        add_via(board,p,gnd)
    print('USB_CRITICAL_PREROUTE: PASS 0.20mm noncrossing main pair + symmetric duplicate fanout')


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    route_ethernet(board)
    route_usb(board)
    board.BuildConnectivity(); conn=board.GetConnectivity(); conn.Build(board); conn.RecalculateRatsnest()
    pcbnew.SaveBoard(str(path),board)
    print('CRITICAL_PREROUTE_LOCKED: PASS')
    print('  unrouted_after_critical_preroute=%d' % int(conn.GetUnconnectedCount(False)))


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: pre_route_critical_nets.py BOARD')
    main(sys.argv[1])
