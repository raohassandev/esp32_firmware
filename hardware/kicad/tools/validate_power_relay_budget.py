#!/usr/bin/env python3
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
d=json.loads((ROOT/'POWER_RELAY_BUDGET.json').read_text())
five=d['five_volt_field']; load=sum(five['loads_a'].values()); rated=five['converter_rated_current_a']; cap=five['planning_load_cap_a']
if load>cap+1e-9: raise SystemExit(f'5V planning load {load:.3f}A exceeds cap {cap:.3f}A')
if rated-load<five['minimum_converter_headroom_a']: raise SystemExit('5V converter headroom below controlled minimum')
three=d['three_volt_three']
if three['converter_rated_current_a']-three['planning_load_a']<three['minimum_current_headroom_a']: raise SystemExit('3V3 headroom below minimum')
r=d['relay_system']; calc=r['nominal_coil_power_w_each']/r['coil_voltage_v']
if abs(calc-r['nominal_coil_current_a_each'])>0.001: raise SystemExit('relay coil current inconsistent')
if abs(calc*r['channels']-five['loads_a']['four_relay_coils'])>0.002: raise SystemExit('relay load budget inconsistent')
if r['pcb_system_resistive_contact_target_a']>5.0: raise SystemExit('Rev-A controlled relay target exceeds 5A')
if not r['hardware_default_off_required']: raise SystemExit('relay hardware default OFF invariant disabled')
print(f'Power/relay budget PASS: 5V load={load:.3f}A headroom={rated-load:.3f}A')
