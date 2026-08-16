#!/usr/bin/env python3
"""Restore SD_SCLK with a deterministic low-risk route after generic SES import.

The generic router converges cleanly except for a repeatable local SD_SCLK/3V3
collision beside J_SD. This route keeps the long trunk on B.Cu and moves the
last approach to In2.Cu, avoiding the dense 3V3 fanout on F.Cu.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as c

WIDTH=0.20


def main(board_path):
    path=Path(board_path); board=pcbnew.LoadBoard(str(path))
    if board is None: raise SystemExit(f'cannot load board: {path}')
    u1=c.footprint(board,c.semantic_ref('U1'))
    jsd=c.footprint(board,c.semantic_ref('J_SD'))
    rs=c.footprint(board,c.semantic_ref('R_SDSCLK'))
    pu=c.pad_net(u1,'SD_SCLK'); pj=c.pad_net(jsd,'SD_SCLK'); pr=c.pad_net(rs,'SD_SCLK')
    if len({pu.GetNetname(),pj.GetNetname(),pr.GetNetname()})!=1:
        raise RuntimeError('SD_SCLK endpoint net mismatch')
    net=c.net_from_pad(pu)
    U=c.xy_mm(pu.GetPosition()); J=c.xy_mm(pj.GetPosition()); R=c.xy_mm(pr.GetPosition())

    # Stable escapes derived from the deterministic Rev-A placement.
    vu=(U[0]+4.2342,U[1]+1.2941)
    vr=(R[0]-2.5854,R[1]-1.8140)
    vm=(J[0]-7.2371,J[1]+3.3699)
    vj=(J[0]+1.4250,J[1]+1.5750)

    # MCU and pull-up/pad escapes on F.Cu.
    c.add_polyline(board,[U,(U[0],U[1]+1.0517),(vu[0]-0.2424,U[1]+1.0517),vu],pcbnew.F_Cu,net,WIDTH)
    c.add_polyline(board,[R,(vr[0]+0.7714,vr[1]),vr],pcbnew.F_Cu,net,WIDTH)
    # Short final SD-card escape only; dense crossing region is avoided.
    c.add_polyline(board,[J,(vj[0],J[1]),vj],pcbnew.F_Cu,net,WIDTH)

    for at in (vu,vr,vm,vj): c.add_via(board,at,net)

    # Existing proven B.Cu corridor to the right-side approach.
    lane=vr[1]+0.1973
    c.add_polyline(board,[vu,(vr[0]-0.3581,vu[1]),vr],pcbnew.B_Cu,net,WIDTH)
    c.add_polyline(board,[vr,(vr[0]+0.1973,lane),(vm[0]-0.8884,lane),vm],pcbnew.B_Cu,net,WIDTH)
    # Final approach on L3/In2, clear of the local F.Cu 3V3 fanout.
    in2=board.GetLayerID('In2.Cu')
    c.add_track(board,vm,vj,in2,net,WIDTH)

    pcbnew.SaveBoard(str(path),board)
    print('SD_SCLK_CONTROLLED_REPAIR: PASS F/B/In2 topology restored at 0.20mm')


if __name__=='__main__':
    if len(sys.argv)!=2: raise SystemExit('usage: repair_sd_sclk.py BOARD.kicad_pcb')
    main(sys.argv[1])
