#!/usr/bin/env python3
"""Rev-A routed-board signal-integrity/layout contract gate.

This is a geometry/topology gate, not a field SI certification. It prevents a
DRC-clean but electrically careless autoroute from being released.
"""
from pathlib import Path
import sys
import pcbnew

ETH_MAX_MM=45.0
ETH_PAIR_SKEW_MM=5.0
USB_PAIR_SKEW_MM=5.0
USB_MAX_MM=190.0


def tracks_for_net(board,name):
    return [t for t in board.GetTracks() if t.GetNetname()==name]


def metrics(board,name):
    items=tracks_for_net(board,name)
    vias=[t for t in items if isinstance(t,pcbnew.PCB_VIA)]
    segs=[t for t in items if not isinstance(t,pcbnew.PCB_VIA)]
    length=sum(pcbnew.ToMM(t.GetLength()) for t in segs)
    layers=sorted({board.GetLayerName(t.GetLayer()) for t in segs})
    return {'length':length,'vias':len(vias),'layers':layers,'segments':len(segs)}


def zones(board):
    try: return list(board.Zones())
    except Exception:
        try: return [board.GetArea(i) for i in range(board.GetAreaCount())]
        except Exception: return []


def gnd_code(board):
    for fp in board.Footprints():
        for p in fp.Pads():
            if p.GetNetname()=='GND': return p.GetNetCode()
    raise RuntimeError('GND net missing')


def check_pair(report,a,b,max_len,skew,via_max=None,via_equal=True):
    ma=report[a]; mb=report[b]
    if ma['length']>max_len or mb['length']>max_len:
        raise SystemExit(f'{a}/{b} length limit FAIL: {ma["length"]:.2f}/{mb["length"]:.2f} mm > {max_len}')
    if abs(ma['length']-mb['length'])>skew:
        raise SystemExit(f'{a}/{b} skew FAIL: {abs(ma["length"]-mb["length"]):.2f} mm > {skew}')
    if via_max is not None and (ma['vias']>via_max or mb['vias']>via_max):
        raise SystemExit(f'{a}/{b} via limit FAIL: {ma["vias"]}/{mb["vias"]} > {via_max}')
    if via_equal and ma['vias']!=mb['vias']:
        raise SystemExit(f'{a}/{b} asymmetric via count FAIL: {ma["vias"]}/{mb["vias"]}')


def main(board_path, report_path=None):
    board=pcbnew.LoadBoard(str(board_path))
    if board is None: raise SystemExit(f'cannot load board: {board_path}')
    in1=board.GetLayerID('In1.Cu')
    in1_tracks=[t for t in board.GetTracks() if not isinstance(t,pcbnew.PCB_VIA) and t.GetLayer()==in1]
    if in1_tracks: raise SystemExit(f'L2 GND reference FAIL: {len(in1_tracks)} signal tracks on In1.Cu')

    gcode=gnd_code(board)
    z=[q for q in zones(board) if q.GetLayer()==in1 and q.GetNetCode()==gcode]
    if len(z)<1: raise SystemExit('L2 GND reference FAIL: no controlled GND zone found')
    unfilled=[]
    for q in z:
        try:
            if not q.HasFilledPolysForLayer(in1): unfilled.append(q)
        except Exception:
            pass
    if unfilled: raise SystemExit(f'L2 GND reference FAIL: {len(unfilled)} zone(s) not filled')

    names=['ETH_TXP','ETH_TXN','ETH_RXP','ETH_RXN','USB_D+','USB_D-','USB_D+_MCU','USB_D-_MCU']
    m={n:metrics(board,n) for n in names}
    for n in names:
        if m[n]['segments']==0: raise SystemExit(f'SI net has no routed segments: {n}')

    check_pair(m,'ETH_TXP','ETH_TXN',ETH_MAX_MM,ETH_PAIR_SKEW_MM,via_max=1,via_equal=True)
    check_pair(m,'ETH_RXP','ETH_RXN',ETH_MAX_MM,ETH_PAIR_SKEW_MM,via_max=0,via_equal=True)
    allowed_eth={'F.Cu','In2.Cu'}
    for n in ('ETH_TXP','ETH_TXN','ETH_RXP','ETH_RXN'):
        if not set(m[n]['layers']).issubset(allowed_eth):
            raise SystemExit(f'{n} layer policy FAIL: {m[n]["layers"]}')

    usb_p=m['USB_D+']['length']+m['USB_D+_MCU']['length']
    usb_m=m['USB_D-']['length']+m['USB_D-_MCU']['length']
    if max(usb_p,usb_m)>USB_MAX_MM: raise SystemExit(f'USB length FAIL: D+={usb_p:.2f} D-={usb_m:.2f} mm')
    if abs(usb_p-usb_m)>USB_PAIR_SKEW_MM: raise SystemExit(f'USB skew FAIL: {abs(usb_p-usb_m):.2f} mm > {USB_PAIR_SKEW_MM}')
    if abs(m['USB_D+']['vias']-m['USB_D-']['vias'])>1:
        raise SystemExit(f'USB via symmetry FAIL: D+={m["USB_D+"]["vias"]} D-={m["USB_D-"]["vias"]}')
    if m['USB_D+']['vias']>4 or m['USB_D-']['vias']>4:
        raise SystemExit(f'USB via count FAIL: D+={m["USB_D+"]["vias"]} D-={m["USB_D-"]["vias"]}')

    lines=['REV-A SIGNAL-INTEGRITY GEOMETRY GATE','L2_GND=PASS zones=%d In1_signal_tracks=0' % len(z)]
    for n in names:
        lines.append('%s length_mm=%.3f vias=%d layers=%s segments=%d' % (n,m[n]['length'],m[n]['vias'],','.join(m[n]['layers']),m[n]['segments']))
    lines.append('USB_TOTAL D+_mm=%.3f D-_mm=%.3f skew_mm=%.3f' % (usb_p,usb_m,abs(usb_p-usb_m)))
    lines.append('SIGNAL_INTEGRITY_GEOMETRY=PASS')
    text='\n'.join(lines)+'\n'
    print(text,end='')
    if report_path: Path(report_path).write_text(text,encoding='utf-8')


if __name__=='__main__':
    if len(sys.argv) not in (2,3): raise SystemExit('usage: validate_signal_integrity.py BOARD [REPORT]')
    main(sys.argv[1], sys.argv[2] if len(sys.argv)==3 else None)
