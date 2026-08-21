#!/usr/bin/env python3
"""KiCad <-> Freerouting bridge for Rev-A H2 routing.

Modes:
  export <board> <dsn>   Export the placed KiCad board to Specctra DSN.
  import <board> <ses>   Import a Freerouting SES onto the same board and save.
  audit  <board>         Rebuild ratsnest and fail if any unrouted connections remain.

Rev-A stack-up policy reserves In1.Cu as the continuous GND reference layer.
The DSN is therefore post-processed to mark In1.Cu as a power layer so the
generic autorouter cannot place signal traces on it.
"""
from pathlib import Path
import re
import sys
import pcbnew


# These routes are authored and locked by pre_route_critical_nets_release.py.
# Freerouting may echo redundant unlocked wiring for already-complete nets in
# its SES. Remove only those unlocked additions after import; the locked source
# topology and the frozen final SI validator remain authoritative.
PROTECTED_CRITICAL_NETS = (
    "ETH_TXP", "ETH_TXP_MAG", "ETH_TXN", "ETH_TXN_MAG",
    "ETH_RXP", "ETH_RXP_MAG", "ETH_RXN", "ETH_RXN_MAG",
    "USB_D+", "USB_D-", "USB_D+_MCU", "USB_D-_MCU",
)


def locked_critical_counts(board):
    counts = {name: 0 for name in PROTECTED_CRITICAL_NETS}
    for item in board.GetTracks():
        name = item.GetNetname()
        if name in counts and item.IsLocked():
            counts[name] += 1
    return counts


def strip_unlocked_critical_additions(board, baseline):
    missing = [name for name, count in baseline.items() if count == 0]
    if missing:
        raise RuntimeError(f"protected critical pre-route missing locked items: {missing}")

    removed = {name: 0 for name in PROTECTED_CRITICAL_NETS}
    for item in list(board.GetTracks()):
        name = item.GetNetname()
        if name in removed and not item.IsLocked():
            board.Remove(item)
            removed[name] += 1

    restored = locked_critical_counts(board)
    if restored != baseline:
        raise RuntimeError(f"protected critical route changed across SES import: before={baseline} after={restored}")
    leaked = [item.GetNetname() for item in board.GetTracks()
              if item.GetNetname() in removed and not item.IsLocked()]
    if leaked:
        raise RuntimeError(f"unlocked protected critical SES items remain: {sorted(set(leaked))}")

    for name in PROTECTED_CRITICAL_NETS:
        print(f"CRITICAL_ROUTE_RESTORE: net={name} locked_items={restored[name]} removed_unlocked={removed[name]}")
    print(f"CRITICAL_ROUTE_RESTORE_PASS: protected={len(PROTECTED_CRITICAL_NETS)} removed_unlocked={sum(removed.values())}")


def load(path):
    b = pcbnew.LoadBoard(str(path))
    if b is None:
        raise SystemExit(f'cannot load board: {path}')
    return b


def export_dsn(board_path, dsn_path):
    b = load(board_path)
    ok = pcbnew.ExportSpecctraDSN(b, str(dsn_path))
    path = Path(dsn_path)
    if not ok or not path.exists() or path.stat().st_size == 0:
        raise SystemExit('Specctra DSN export failed')

    text = path.read_text(encoding='utf-8')
    pattern = r'(\(layer\s+"?In1\.Cu"?\s*\n\s*)\(type\s+signal\)'
    text, count = re.subn(pattern, r'\1(type power)', text, count=1)
    if count != 1:
        raise SystemExit('failed to reserve In1.Cu as DSN power/non-signal layer')
    path.write_text(text, encoding='utf-8')
    if re.search(pattern, text):
        raise SystemExit('In1.Cu still exported as signal layer')
    print(f'DSN_EXPORT_PASS bytes={path.stat().st_size} In1.Cu=POWER_RESERVED')


def import_ses(board_path, ses_path):
    b = load(board_path)
    baseline = locked_critical_counts(b)
    if not Path(ses_path).exists() or Path(ses_path).stat().st_size == 0:
        raise SystemExit('SES missing/empty')
    ok = pcbnew.ImportSpecctraSES(b, str(ses_path))
    if not ok:
        raise SystemExit('Specctra SES import failed')
    strip_unlocked_critical_additions(b, baseline)
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
    in1 = b.GetLayerID('In1.Cu')
    in1_signals = sum(1 for t in tracks if not isinstance(t, pcbnew.PCB_VIA) and t.GetLayer() == in1)
    print(f'ROUTING_AUDIT unconnected={unconn} traces={traces} vias={vias} nets={b.GetNetCount()} in1_signal_tracks={in1_signals}')
    if in1_signals:
        raise SystemExit(f'L2 reference-plane violation: {in1_signals} signal tracks on In1.Cu')
    if unconn:
        raise SystemExit(f'H2 routing incomplete: {unconn} ratsnest connection(s) remain')
    print('H2_CONNECTIVITY_PASS: unconnected=0 In1.Cu_signal_tracks=0')


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
