#!/usr/bin/env python3
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
c=json.loads((ROOT/'HW_INTERFACE_CONTRACT.json').read_text())
pins=[]
def collect(obj,path=''):
    if isinstance(obj,dict):
        for k,v in obj.items(): collect(v,f'{path}.{k}' if path else k)
    elif isinstance(obj,int): pins.append((path,obj))
collect(c['mandatory']); collect(c['optional'])
by={}
for name,pin in pins: by.setdefault(pin,[]).append(name)
conf={pin:n for pin,n in by.items() if len(n)>1}
if conf: raise SystemExit(f'GPIO conflict: {conf}')
strap=set(c['constraints']['strapping_pins'])
bad=[(n,p) for n,p in pins if p in strap and n!='boot']
if bad: raise SystemExit(f'unapproved strapping-pin use: {bad}')
reserved=set(c['constraints']['reserved_flash_preference'])
bad=[(n,p) for n,p in pins if p in reserved]
if bad: raise SystemExit(f'flash-preferred GPIO used: {bad}')
required={'rs485_a','rs485_b','hmi_uart','ethernet_w5500','relay_outputs','usb'}
missing=required-set(c['mandatory'])
if missing: raise SystemExit(f'missing mandatory interfaces: {sorted(missing)}')
assert c['constraints']['rs485_ports_independent'] is True
assert c['constraints']['hmi_uart_not_shared_with_rs485'] is True
assert c['constraints']['all_relay_outputs_default_off'] is True
print(f'HW interface contract PASS: {len(pins)} unique GPIO assignments')
