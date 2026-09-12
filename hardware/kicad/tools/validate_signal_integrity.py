#!/usr/bin/env python3
"""Rev-A routed-board signal-integrity/layout contract gate.

Geometry/topology gate, not field SI certification. The W5500 MDI path is split
by the four official-reference 0R damping footprints, so each conductor is
measured end-to-end across chip-side and MagJack-side nets.
"""
from pathlib import Path
import sys
import pcbnew

ETH_MAX_MM=45.0
ETH_PAIR_SKEW_MM=5.0
USB_PAIR_SKEW_MM=5.0
USB_MAX_MM=190.0


def tracks_for_net(board,name): return [t for t in board.GetTracks() if t.GetNetname()==name]
def metrics(board,name):
    items=tracks_for_net(board,name); vias=[t for t in items if isinstance(t,pcbnew.PCB_VIA)]; segs=[t for t in items if not isinstance(t,pcbnew.PCB_VIA)]
    return {'length':sum(pcbnew.ToMM(t.GetLength()) for t in segs),'vias':len(vias),'layers':sorted({board.GetLayerName(t.GetLayer()) for t in segs}),'segments':len(segs)}
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
def combined(m,*names):
    return {'length':sum(m[n]['length'] for n in names),'vias':sum(m[n]['vias'] for n in names),'layers':sorted(set().union(*(set(m[n]['layers']) for n in names))),'segments':sum(m[n]['segments'] for n in names)}
def check_pair(a,b,label,max_len,skew,via_max=None,via_equal=True):
    if a['length']>max_len or b['length']>max_len: raise SystemExit(f'{label} length FAIL {a["length"]:.2f}/{b["length"]:.2f}>{max_len}')
    if abs(a['length']-b['length'])>skew: raise SystemExit(f'{label} skew FAIL {abs(a["length"]-b["length"]):.2f}>{skew}')
    if via_max is not None and (a['vias']>via_max or b['vias']>via_max): raise SystemExit(f'{label} via limit FAIL {a["vias"]}/{b["vias"]}>{via_max}')
    if via_equal and a['vias']!=b['vias']: raise SystemExit(f'{label} asymmetric vias FAIL {a["vias"]}/{b["vias"]}')


def main(board_path,report_path=None):
    board=pcbnew.LoadBoard(str(board_path))
    if board is None: raise SystemExit(f'cannot load board: {board_path}')
    in1=board.GetLayerID('In1.Cu')
    in1_tracks=[t for t in board.GetTracks() if not isinstance(t,pcbnew.PCB_VIA) and t.GetLayer()==in1]
    if in1_tracks: raise SystemExit(f'L2 GND reference FAIL: {len(in1_tracks)} signal tracks on In1.Cu')
    gcode=gnd_code(board); z=[q for q in zones(board) if q.GetLayer()==in1 and q.GetNetCode()==gcode]
    if not z: raise SystemExit('L2 GND reference FAIL: no controlled GND zone found')
    for q in z:
        try:
            if not q.HasFilledPolysForLayer(in1): raise SystemExit('L2 GND reference FAIL: unfilled zone')
        except AttributeError: pass

    names=['ETH_TXP','ETH_TXP_MAG','ETH_TXN','ETH_TXN_MAG','ETH_RXP','ETH_RXP_MAG','ETH_RXN','ETH_RXN_MAG','USB_D+','USB_D-','USB_D+_MCU','USB_D-_MCU']
    m={n:metrics(board,n) for n in names}
    for n in names:
        if m[n]['segments']==0: raise SystemExit(f'SI net has no routed segments: {n}')

    txp=combined(m,'ETH_TXP','ETH_TXP_MAG'); txn=combined(m,'ETH_TXN','ETH_TXN_MAG')
    rxp=combined(m,'ETH_RXP','ETH_RXP_MAG'); rxn=combined(m,'ETH_RXN','ETH_RXN_MAG')
    check_pair(txp,txn,'ETH_TX',ETH_MAX_MM,ETH_PAIR_SKEW_MM,via_max=1)
    check_pair(rxp,rxn,'ETH_RX',ETH_MAX_MM,ETH_PAIR_SKEW_MM,via_max=1)
    allowed_eth={'F.Cu','In2.Cu'}
    for label,metric in [('TXP',txp),('TXN',txn),('RXP',rxp),('RXN',rxn)]:
        if not set(metric['layers']).issubset(allowed_eth): raise SystemExit(f'ETH_{label} layer policy FAIL: {metric["layers"]}')

    usb_p=m['USB_D+']['length']+m['USB_D+_MCU']['length']; usb_m=m['USB_D-']['length']+m['USB_D-_MCU']['length']
    if max(usb_p,usb_m)>USB_MAX_MM: raise SystemExit(f'USB length FAIL: D+={usb_p:.2f} D-={usb_m:.2f}')
    if abs(usb_p-usb_m)>USB_PAIR_SKEW_MM: raise SystemExit(f'USB skew FAIL {abs(usb_p-usb_m):.2f}>{USB_PAIR_SKEW_MM}')
    if abs(m['USB_D+']['vias']-m['USB_D-']['vias'])>1: raise SystemExit('USB via symmetry FAIL')
    if m['USB_D+']['vias']>4 or m['USB_D-']['vias']>4: raise SystemExit('USB via count FAIL')

    lines=['REV-A SIGNAL-INTEGRITY GEOMETRY GATE',f'L2_GND=PASS zones={len(z)} In1_signal_tracks=0']
    for n in names: lines.append('%s length_mm=%.3f vias=%d layers=%s segments=%d'%(n,m[n]['length'],m[n]['vias'],','.join(m[n]['layers']),m[n]['segments']))
    lines += [f'ETH_TOTAL TXP={txp["length"]:.3f} TXN={txn["length"]:.3f} RXP={rxp["length"]:.3f} RXN={rxn["length"]:.3f}',f'USB_TOTAL D+_mm={usb_p:.3f} D-_mm={usb_m:.3f} skew_mm={abs(usb_p-usb_m):.3f}','SIGNAL_INTEGRITY_GEOMETRY=PASS']
    text='\n'.join(lines)+'\n'; print(text,end='')
    if report_path: Path(report_path).write_text(text,encoding='utf-8')

if __name__=='__main__':
    if len(sys.argv) not in (2,3): raise SystemExit('usage: validate_signal_integrity.py BOARD [REPORT]')
    main(sys.argv[1],sys.argv[2] if len(sys.argv)==3 else None)
