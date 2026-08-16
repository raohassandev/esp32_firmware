#!/usr/bin/env python3
"""Pre-route Rev-A high-speed nets before the generic autorouter.

The generic autorouter is useful for low-speed GPIO/power connectivity but is
not allowed to define USB or Ethernet MDI topology. This script owns those
critical paths, locks them, and leaves the remaining nets to Freerouting.

Policy:
- USB main pair: F.Cu over reserved In1.Cu GND reference, paired geometry.
- USB-C duplicate D pads: symmetric short In2.Cu branches with equal via count.
- Ethernet RX pair: F.Cu, short and via-free.
- Ethernet TX pair: In2.Cu with one matched via per conductor; In1.Cu is the
  adjacent GND reference. This is a controlled prototype deviation from the
  WIZnet preferred no-via topology and is audited explicitly.
"""
from pathlib import Path
import json
import math
import sys
import pcbnew

WIDTH_USB_MM = 0.18
WIDTH_ETH_MM = 0.18
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

    # TX: matched F.Cu escapes into paired through vias, then short In2 routes.
    # The pair maintains the same ordering from vias to the THT MagJack pads.
    vtxn=(stxn[0]-0.95, stxn[1]); vtxp=(stxp[0]-0.95, stxp[1])
    add_track(board,stxn,vtxn,F,net_from_pad(tx_n),WIDTH_ETH_MM); add_via(board,vtxn,net_from_pad(tx_n))
    add_track(board,stxp,vtxp,F,net_from_pad(tx_p),WIDTH_ETH_MM); add_via(board,vtxp,net_from_pad(tx_p))
    add_polyline(board,[vtxn,(125.8,vtxn[1]),dtxn],IN2,net_from_pad(tx_n),WIDTH_ETH_MM)
    add_polyline(board,[vtxp,(126.3,vtxp[1]),dtxp],IN2,net_from_pad(tx_p),WIDTH_ETH_MM)

    # RX: wrap above the W5500 package on F.Cu. Both conductors use parallel
    # 45-degree escapes and remain short enough for the Rev-A MDI target.
    top_n=59.55; top_p=59.05
    add_polyline(board,[srxn,(srxn[0]-0.95,srxn[1]),(srxn[0]-1.95,top_n),(drxn[0]-2.3,top_n),drxn],F,net_from_pad(rx_n),WIDTH_ETH_MM)
    add_polyline(board,[srxp,(srxp[0]-1.35,srxp[1]),(srxp[0]-2.45,top_p),(drxp[0]-3.0,top_p),drxp],F,net_from_pad(rx_p),WIDTH_ETH_MM)
    print('ETHERNET_CRITICAL_PREROUTE: PASS TX=In2/1via-each RX=F.Cu/0via')


def route_usb(board):
    F=pcbnew.F_Cu; IN2=board.GetLayerID('In2.Cu')
    u1=footprint(board, semantic_ref('U1'))
    j2=footprint(board, semantic_ref('J_USB'))
    rdm=footprint(board, semantic_ref('R_MCU_DM_SER'))
    rdp=footprint(board, semantic_ref('R_MCU_DP_SER'))

    mcu_dm=pad_number(u1,13); mcu_dp=pad_number(u1,14)
    rdm_mcu=pad_net(rdm,'USB_D-_MCU'); rdm_ext=pad_net(rdm,'USB_D-')
    rdp_mcu=pad_net(rdp,'USB_D+_MCU'); rdp_ext=pad_net(rdp,'USB_D+')
    for p,n in ((mcu_dm,'USB_D-_MCU'),(mcu_dp,'USB_D+_MCU')):
        if p.GetNetname()!=n: raise RuntimeError(f'USB MCU endpoint mismatch: {p.GetNetname()} != {n}')

    # Short MCU-side connections into the close series resistors.
    add_polyline(board,[xy_mm(mcu_dm.GetPosition()),xy_mm(rdm_mcu.GetPosition())],F,net_from_pad(mcu_dm),WIDTH_USB_MM)
    add_polyline(board,[xy_mm(mcu_dp.GetPosition()),xy_mm(rdp_mcu.GetPosition())],F,net_from_pad(mcu_dp),WIDTH_USB_MM)

    a6=pad_number(j2,'A6'); b6=pad_number(j2,'B6')
    a7=pad_number(j2,'A7'); b7=pad_number(j2,'B7')
    if any(p.GetNetname()!='USB_D+' for p in (a6,b6)) or any(p.GetNetname()!='USB_D-' for p in (a7,b7)):
        raise RuntimeError('USB-C duplicate D-pad net mapping mismatch')

    sdm=xy_mm(rdm_ext.GetPosition()); sdp=xy_mm(rdp_ext.GetPosition())
    adm=xy_mm(a7.GetPosition()); adp=xy_mm(a6.GetPosition())

    # Parallel main corridor. Nominal width/gap is a routing placeholder; the
    # provider must tune the final width/gap to 90-ohm differential impedance
    # using the frozen production stack-up without changing topology/length.
    dm_pts=[sdm,(24.0,sdm[1]),(35.0,53.00),(128.0,53.00),(136.50,44.50),adm]
    dp_pts=[sdp,(24.4,sdp[1]),(36.4,53.45),(128.45,53.45),(136.95,44.95),adp]
    add_polyline(board,dm_pts,F,net_from_pad(rdm_ext),WIDTH_USB_MM)
    add_polyline(board,dp_pts,F,net_from_pad(rdp_ext),WIDTH_USB_MM)

    # USB-C has duplicated D contacts. Connect each duplicate with the same
    # two-via topology on In2 so both polarities have identical transition count.
    dm_v1=(135.50,adm[1]); dm_v2=(135.50,xy_mm(b7.GetPosition())[1])
    dp_v1=(136.50,adp[1]); dp_v2=(136.50,xy_mm(b6.GetPosition())[1])
    for primary,dup,v1,v2,net in (
        (adm,xy_mm(b7.GetPosition()),dm_v1,dm_v2,net_from_pad(a7)),
        (adp,xy_mm(b6.GetPosition()),dp_v1,dp_v2,net_from_pad(a6)),
    ):
        add_track(board,primary,v1,F,net,WIDTH_USB_MM); add_via(board,v1,net)
        add_track(board,v1,v2,IN2,net,WIDTH_USB_MM); add_via(board,v2,net)
        add_track(board,v2,dup,F,net,WIDTH_USB_MM)

    # Local return-current stitching around the USB transition region.
    gnd=find_gnd_net(board)
    for p in ((134.0,48.0),(137.5,48.0),(134.0,39.5),(137.5,39.5)):
        add_via(board,p,gnd)
    print('USB_CRITICAL_PREROUTE: PASS main=F.Cu duplicate-fanout=In2 symmetric-vias')


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
