#!/usr/bin/env python3
"""Print selected Rev-A manifest entries for CI/audit evidence."""
from pathlib import Path
import base64, json, zlib

p = Path(__file__).with_name('reva_manifest.zlib.b64')
m = json.loads(zlib.decompress(base64.b64decode(p.read_text().strip())))
refs = {'U1','U2','J_ETH','U3','U4','J_RS485A','J_RS485B','J_HMI','U7','K1','J_RLY1','J_DI','U_RTC','J_SD'}
for sym in ['ESP32S3','W5500','MAGJACK','THVD1410','CONN3','CONN4','CONN5','MAX3232','RELAY','PCF8563','SD']:
    print('DEF', sym, m['defs'].get(sym))
for c in m['comps']:
    if c['ref'] in refs:
        print('COMP', c['ref'], json.dumps(c, sort_keys=True))
