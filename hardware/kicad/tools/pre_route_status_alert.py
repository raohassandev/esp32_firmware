#!/usr/bin/env python3
"""Lock the proven Rev-A STATUS_ALERT_CTL route before generic autorouting.

Freerouting can plateau with this one three-pad low-speed control net split into
separate islands.  The geometry below comes from the clean KiCad-10 candidate in
PR validation run 32522110531 (DRC=0, UNCONNECTED=0).  Assert all three pad
coordinates before adding copper so a placement change fails closed instead of
silently applying stale geometry.  In1.Cu is intentionally untouched.
"""
from pathlib import Path
import sys
import pcbnew
import pre_route_critical_nets as base

NET = "STATUS_ALERT_CTL"
WIDTH_MM = 0.20
EXPECTED_PADS = {
    (50.8625, 46.7000),  # U_LEDLOGIC/U14 pin 13
    (22.1750, 44.0000),  # R_STATUS_ALERT_PD/R69 pin 1
    (18.5000, 52.8250),  # ESP32/U1 GPIO21 pad
}
TRACKS = (
    ("F.Cu", (18.5000, 52.8250), (17.4483, 52.8250)),
    ("F.Cu", (17.4483, 52.8250), (17.2336, 53.0397)),
    ("F.Cu", (17.2336, 53.0397), (17.2336, 53.0868)),
    ("In2.Cu", (17.2336, 53.0868), (19.9820, 50.3384)),
    ("In2.Cu", (19.9820, 50.3384), (19.9820, 44.0000)),
    ("F.Cu", (19.9820, 44.0000), (22.1750, 44.0000)),
    ("F.Cu", (22.1750, 44.0000), (23.0025, 43.1725)),
    ("F.Cu", (23.0025, 43.1725), (24.9958, 43.1725)),
    ("F.Cu", (24.9958, 43.1725), (25.7699, 43.9466)),
    ("F.Cu", (25.7699, 43.9466), (45.8474, 43.9466)),
    ("F.Cu", (45.8474, 43.9466), (46.2476, 44.3468)),
    ("F.Cu", (46.2476, 44.3468), (50.5162, 44.3468)),
    ("F.Cu", (50.5162, 44.3468), (51.9175, 45.7481)),
    ("F.Cu", (51.9175, 45.7481), (51.9175, 46.3080)),
    ("F.Cu", (51.9175, 46.3080), (51.5255, 46.7000)),
    ("F.Cu", (51.5255, 46.7000), (50.8625, 46.7000)),
)
VIAS = ((17.2336, 53.0868), (19.9820, 44.0000))


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
