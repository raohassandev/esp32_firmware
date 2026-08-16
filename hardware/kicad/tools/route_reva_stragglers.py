#!/usr/bin/env python3
"""Deterministically route the small stable remainder Freerouting leaves behind.

These nets were identical across independent routing strategies. They are routed
before DSN export so the autorouter respects their corridors, and restored after
SES import. In1.Cu remains untouched.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as c

WIDTH = 0.20


def via_lane(board, p1, p2, layer, lane_y, esc1, esc2):
    if p1.GetNetname() != p2.GetNetname():
        raise RuntimeError(f'net mismatch {p1.GetNetname()} != {p2.GetNetname()}')
    s1=c.xy_mm(p1.GetPosition()); s2=c.xy_mm(p2.GetPosition())
    v1=(s1[0]+esc1[0], s1[1]+esc1[1]); v2=(s2[0]+esc2[0], s2[1]+esc2[1])
    net=c.net_from_pad(p1)
    c.add_track(board,s1,v1,pcbnew.F_Cu,net,WIDTH); c.add_via(board,v1,net)
    c.add_polyline(board,[v1,(v1[0],lane_y),(v2[0],lane_y),v2],layer,net,WIDTH)
    c.add_via(board,v2,net); c.add_track(board,v2,s2,pcbnew.F_Cu,net,WIDTH)


def route(board):
    F=pcbnew.F_Cu; B=pcbnew.B_Cu; IN2=board.GetLayerID('In2.Cu')
    u1=c.footprint(board,c.semantic_ref('U1')); u2=c.footprint(board,c.semantic_ref('U2'))

    # Long MCU<->W5500 lines. Separate layers/corridors avoid ordering crosses.
    via_lane(board,c.pad_net(u1,'ETH_MISO'),c.pad_net(u2,'ETH_MISO'),B,50.0,(1.5,0),(1.5,0))
    via_lane(board,c.pad_net(u1,'ETH_MOSI'),c.pad_net(u2,'ETH_MOSI'),IN2,52.0,(2.0,0),(2.0,0))
    via_lane(board,c.pad_net(u1,'ETH_RST'),c.pad_net(u2,'ETH_RST'),B,76.0,(1.3,0),(0,-1.3))

    # W5500 PMODE straps. PM0/PM2 stay on top with distinct escape heights;
    # PM1 uses In2 so the three local routes cannot cross each other.
    r14=c.footprint(board,c.semantic_ref('R_PM2'))
    r15=c.footprint(board,c.semantic_ref('R_PM1'))
    r16=c.footprint(board,c.semantic_ref('R_PM0'))

    p2=c.pad_net(u2,'ETH_PMODE2'); d2=c.pad_net(r14,'ETH_PMODE2')
    s=c.xy_mm(p2.GetPosition()); d=c.xy_mm(d2.GetPosition())
    c.add_polyline(board,[s,(s[0],58.0),(100.0,58.0),(100.0,d[1]),d],F,c.net_from_pad(p2),WIDTH)

    via_lane(board,c.pad_net(u2,'ETH_PMODE1'),c.pad_net(r15,'ETH_PMODE1'),IN2,54.0,(0,-1.3),(1.2,0))

    p0=c.pad_net(u2,'ETH_PMODE0'); d0=c.pad_net(r16,'ETH_PMODE0')
    s=c.xy_mm(p0.GetPosition()); d=c.xy_mm(d0.GetPosition())
    c.add_polyline(board,[s,(s[0],57.0),(d[0],57.0),d],F,c.net_from_pad(p0),WIDTH)

    # USB CC1 to Rd, on B.Cu to stay clear of the top USB differential pair.
    j2=c.footprint(board,c.semantic_ref('J_USB')); rcc1=c.footprint(board,c.semantic_ref('R_CC1'))
    via_lane(board,c.pad_number(j2,'A5'),c.pad_net(rcc1,'USB_CC1'),B,37.0,(-1.2,0),(-1.2,0))

    # Protected-input local connection.
    dz=c.footprint(board,c.semantic_ref('DZREV')); cin2=c.footprint(board,c.semantic_ref('CIN2'))
    pz=c.pad_net(dz,'VIN_PROTECTED'); pc=c.pad_net(cin2,'VIN_PROTECTED')
    s=c.xy_mm(pz.GetPosition()); d=c.xy_mm(pc.GetPosition())
    c.add_polyline(board,[s,(28.0,s[1]),(28.0,d[1]),d],F,c.net_from_pad(pz),WIDTH)

    # MagJack LED supply pins are THT and can be joined directly on B.Cu.
    j3=c.footprint(board,c.semantic_ref('J_ETH')); p10=c.pad_number(j3,10); p11=c.pad_number(j3,11)
    if p10.GetNetname()!='3V3' or p11.GetNetname()!='3V3':
        raise RuntimeError('MagJack 3V3 pad mapping changed')
    a=c.xy_mm(p10.GetPosition()); b=c.xy_mm(p11.GetPosition())
    c.add_polyline(board,[a,(a[0],b[1]),b],B,c.net_from_pad(p10),WIDTH)

    print('ROUTER_STRAGGLERS_LOCKED: PASS MISO MOSI RST PMODE0-2 USB_CC1 VIN_PROTECTED J3_3V3')


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    route(board)
    pcbnew.SaveBoard(str(path),board)


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: route_reva_stragglers.py BOARD.kicad_pcb')
    main(sys.argv[1])
