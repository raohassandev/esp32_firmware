#!/usr/bin/env python3
"""Lock the Rev-A STATUS_ALERT_CTL route before generic autorouting.

Freerouting can plateau with this three-pad low-speed control net split into
separate islands. The current deterministic placement moved the ESP32 endpoint
+0.50 mm in X and places R_STATUS_ALERT_PD at (23.175, 46.000).

The historical long F.Cu segment at y~=43.2 is no longer valid: current R70 and
SW1 occupy that corridor. Keep the proven U1 left escape, then move the long
middle trunk off F.Cu. A short F.Cu branch serves R69, a B.Cu trunk crosses the
logic area, and a short F.Cu branch serves U14. The three-pad placement
assertion remains fail-closed so any later placement change requires explicit
route regeneration. In1.Cu is intentionally untouched.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as base

NET = "STATUS_ALERT_CTL"
WIDTH_MM = 0.20
EXPECTED_PADS = {
    (50.8625, 46.7000),  # U_LEDLOGIC/U14 pin 13
    (23.1750, 46.0000),  # R_STATUS_ALERT_PD/R69 pin 1
    (19.0000, 52.8250),  # ESP32/U1 GPIO21 after +0.50 mm edge repair
}

# Controlled topology:
#   U1 F.Cu escape -> In2 descent -> via/trunk junction at (19.982,46.0)
#   -> short F.Cu branch to R69
#   -> long B.Cu trunk to (49.0,46.7) -> short F.Cu branch to U14.
# This deliberately avoids the obsolete R70/SW1 F.Cu corridor around y=43.
TRACKS = (
    ("F.Cu", (19.0000, 52.8250), (17.4483, 52.8250)),
    ("F.Cu", (17.4483, 52.8250), (17.2336, 53.0397)),
    ("F.Cu", (17.2336, 53.0397), (17.2336, 53.0868)),
    ("In2.Cu", (17.2336, 53.0868), (19.9820, 50.3384)),
    ("In2.Cu", (19.9820, 50.3384), (19.9820, 46.0000)),
    ("F.Cu", (19.9820, 46.0000), (23.1750, 46.0000)),
    ("B.Cu", (19.9820, 46.0000), (49.0000, 46.7000)),
    ("F.Cu", (49.0000, 46.7000), (50.8625, 46.7000)),
)
VIAS = (
    (17.2336, 53.0868),
    (19.9820, 46.0000),
    (49.0000, 46.7000),
)


def close(a, b, tol=0.0002):
    return abs(a[0] - b[0]) <= tol and abs(a[1] - b[1]) <= tol


def route(board):
    net = board.FindNet(NET)
    if net is None:
        raise RuntimeError(f"missing net: {NET}")
    pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() == NET:
                pads.append(base.xy_mm(pad.GetPosition()))
    if len(pads) != 3:
        raise RuntimeError(f"{NET}: expected 3 pads, found {len(pads)} at {pads}")
    for expected in EXPECTED_PADS:
        if not any(close(expected, actual) for actual in pads):
            raise RuntimeError(
                f"{NET}: expected pad {expected} missing; placement changed, regenerate locked route; actual={pads}"
            )
    for layer, a, b in TRACKS:
        base.add_track(board, a, b, board.GetLayerID(layer), net, WIDTH_MM)
    for xy in VIAS:
        base.add_via(board, xy, net)
    print(f"STATUS_ALERT_ROUTE_LOCKED: PASS pads=3 tracks={len(TRACKS)} vias={len(VIAS)}")


def main(board_path):
    path = Path(board_path)
    board = pcbnew.LoadBoard(str(path))
    if board is None:
        raise SystemExit(f"cannot load board: {path}")
    route(board)
    pcbnew.SaveBoard(str(path), board)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: pre_route_status_alert.py BOARD.kicad_pcb")
    main(sys.argv[1])
