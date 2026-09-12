#!/usr/bin/env python3
"""Reserve the U13:3 GND escape before generic autorouting.

Authoritative parent H2 run 34700172988 proved that Freerouting can isolate the
small F.Cu GND island containing U13 pad 3. Post-route stitching then has no
DRC-safe path to L2 because the signal router has already occupied the escape
corridor. Reserve the same locked 0.60/0.30 mm via + 0.20 mm F.Cu spoke pattern
used by post_route_finalize.py before Specctra export so the router must route
around this return-path access.

This is geometry reservation only. It does not relax any DRC/manufacturing rule.
Immediate critical-preroute KiCad DRC remains authoritative.
"""
from pathlib import Path
import sys
import pcbnew
import post_route_finalize as base

REF = "U13"
PAD = "3"
DX = -0.90
DY = 0.00


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f"cannot load board: {path}")

    gnet = base.gnd_net(board)
    gcode = gnet.GetNetCode()
    pad = base.find_pad(board, REF, PAD)
    if pad.GetNetCode() != gcode:
        raise RuntimeError(f"U13 GND escape contract mismatch: {REF}:{PAD} net={pad.GetNetname()}")

    # This hook runs immediately after post_route_finalize.py and before any
    # signal route is added. Fail closed if that ordering ever changes.
    non_gnd = [item for item in board.GetTracks() if item.GetNetCode() != gcode]
    if non_gnd:
        raise RuntimeError(
            f"U13 GND escape must run before signal routing; found {len(non_gnd)} non-GND track/via item(s)"
        )

    px, py = base.xy(pad.GetPosition())
    vx, vy = px + DX, py + DY
    base.assert_escape_clear(board, pad, (vx, vy), gcode)

    via = pcbnew.PCB_VIA(board)
    via.SetPosition(base.pt(vx, vy))
    via.SetWidth(pcbnew.FromMM(base.VIA_D))
    via.SetDrill(pcbnew.FromMM(base.VIA_DRILL))
    via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    via.SetNet(gnet)
    via.SetLocked(True)
    board.Add(via)

    track = pcbnew.PCB_TRACK(board)
    track.SetStart(pad.GetPosition())
    track.SetEnd(base.pt(vx, vy))
    track.SetWidth(pcbnew.FromMM(base.TRACK_W))
    track.SetLayer(pcbnew.F_Cu)
    track.SetNet(gnet)
    track.SetLocked(True)
    board.Add(track)

    pcbnew.SaveBoard(str(path), board)
    print(
        f"PRE_ROUTE_U13_GND_ESCAPE: PASS {REF}:{PAD} "
        f"pad=({px:.3f},{py:.3f}) via=({vx:.3f},{vy:.3f})"
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: pre_route_u13_gnd_escape.py BOARD.kicad_pcb")
    main(sys.argv[1])
