#!/usr/bin/env python3
"""KiCad <-> Freerouting bridge for Rev-A H2 routing.

Modes:
  export <board> <dsn>   Export the placed KiCad board to Specctra DSN.
  import <board> <ses>   Import a Freerouting SES onto the same board and save.
  audit  <board>         Rebuild ratsnest and fail if any unrouted connections remain.

The DSN/SES flow is intentionally isolated from schematic/component placement:
KiCad imports only routing from SES; footprints, nets and outline remain owned
by the validated Rev-A generator.
"""
from pathlib import Path
import sys
import pcbnew


def load(path):
    b = pcbnew.LoadBoard(str(path))
    if b is None:
        raise SystemExit(f'cannot load board: {path}')
    return b


def export_dsn(board_path, dsn_path):
    b = load(board_path)
    ok = pcbnew.ExportSpecctraDSN(b, str(dsn_path))
    if not ok or not Path(dsn_path).exists() or Path(dsn_path).stat().st_size == 0:
        raise SystemExit('Specctra DSN export failed')
    print(f'DSN_EXPORT_PASS bytes={Path(dsn_path).stat().st_size}')


def import_ses(board_path, ses_path):
    b = load(board_path)
    if not Path(ses_path).exists() or Path(ses_path).stat().st_size == 0:
        raise SystemExit('SES missing/empty')
    ok = pcbnew.ImportSpecctraSES(b, str(ses_path))
    if not ok:
        raise SystemExit('Specctra SES import failed')
    pcbnew.SaveBoard(str(board_path), b)
    print(f'SES_IMPORT_PASS tracks={len(list(b.GetTracks()))}')


def audit(board_path):
    b = load(board_path)
    conn = b.GetConnectivity()
    conn.Build(b)
    conn.RecalculateRatsnest()
    unconn = int(conn.GetUnconnectedCount(False))
    tracks = list(b.GetTracks())
    vias = sum(1 for t in tracks if isinstance(t, pcbnew.PCB_VIA))
    traces = len(tracks) - vias
    print(f'ROUTING_AUDIT unconnected={unconn} traces={traces} vias={vias} nets={b.GetNetCount()}')
    if unconn:
        raise SystemExit(f'H2 routing incomplete: {unconn} ratsnest connection(s) remain')
    print('H2_CONNECTIVITY_PASS: unconnected=0')


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: route_reva_freerouting.py export BOARD DSN | import BOARD SES | audit BOARD')
    mode = sys.argv[1]
    if mode == 'export' and len(sys.argv) == 4:
        export_dsn(sys.argv[2], sys.argv[3])
    elif mode == 'import' and len(sys.argv) == 4:
        import_ses(sys.argv[2], sys.argv[3])
    elif mode == 'audit' and len(sys.argv) == 3:
        audit(sys.argv[2])
    else:
        raise SystemExit('invalid arguments')

if __name__ == '__main__':
    main()
