#!/usr/bin/env python3
"""Remove impossible near-duplicate same-net GND vias after island stitching.

Pre-route reserved GND escapes and the post-route surface-island stitcher can
occasionally select essentially the same through-hole position after a zone
refill. KiCad's final upgrade/refill correctly reports those as hole-to-hole
violations even when the earlier route-stage DRC did not. A pair of GND vias
whose centres are less than 0.20 mm apart cannot be a meaningful independent
through-via pair on this Rev-A board. Prefer the via that terminates an explicit
GND track (the reserved escape) and remove the redundant untracked stitch via.
Final KiCad DRC remains the release authority.
"""
from pathlib import Path
import math
import sys
import pcbnew

NEAR_DUP_MM = 0.20
ENDPOINT_EPS_MM = 0.02


def xy(p):
    return pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def gnd_netcode(board):
    for fp in board.Footprints():
        for pad in fp.Pads():
            if pad.GetNetname() == "GND":
                return pad.GetNetCode()
    raise RuntimeError("GND net missing")


def track_endpoint_count(board, via, gcode):
    vp = xy(via.GetPosition())
    count = 0
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_VIA) or item.GetNetCode() != gcode:
            continue
        try:
            a = xy(item.GetStart())
            b = xy(item.GetEnd())
        except Exception:
            continue
        if dist(vp, a) <= ENDPOINT_EPS_MM:
            count += 1
        if dist(vp, b) <= ENDPOINT_EPS_MM:
            count += 1
    return count


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f"cannot load board: {path}")

    gcode = gnd_netcode(board)
    vias = [item for item in board.GetTracks() if isinstance(item, pcbnew.PCB_VIA) and item.GetNetCode() == gcode]
    removed = set()
    decisions = []

    for i, a in enumerate(vias):
        if id(a) in removed:
            continue
        pa = xy(a.GetPosition())
        for b in vias[i + 1:]:
            if id(b) in removed:
                continue
            pb = xy(b.GetPosition())
            d = dist(pa, pb)
            if d >= NEAR_DUP_MM:
                continue

            sa = track_endpoint_count(board, a, gcode)
            sb = track_endpoint_count(board, b, gcode)
            # Keep the explicit track-connected/reserved escape. On a tie keep
            # the earlier board item for deterministic source stability.
            if sb > sa:
                keep, drop, kp, dp, ks, ds = b, a, pb, pa, sb, sa
            else:
                keep, drop, kp, dp, ks, ds = a, b, pa, pb, sa, sb
            board.Remove(drop)
            removed.add(id(drop))
            decisions.append((kp, dp, d, ks, ds))
            if drop is a:
                break

    pcbnew.SaveBoard(str(path), board)
    for kp, dp, d, ks, ds in decisions:
        print(
            "GND_VIA_DEDUPE: keep=(%.4f,%.4f) endpoints=%d drop=(%.4f,%.4f) endpoints=%d distance=%.4fmm"
            % (kp[0], kp[1], ks, dp[0], dp[1], ds, d)
        )
    print(f"GND_VIA_DEDUPE_PASS: removed={len(decisions)} threshold={NEAR_DUP_MM:.2f}mm")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: dedupe_gnd_vias.py BOARD.kicad_pcb")
    main(sys.argv[1])
