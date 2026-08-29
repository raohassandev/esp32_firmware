#!/usr/bin/env python3
from pathlib import Path

files = [
    Path('tests/alarm_journal_shelving_source_contract.py'),
    Path('tests/alarm_suppression_states_source_contract.py'),
    Path('tests/alarm_rate_and_priority_source_contract.py'),
]
old = 'alarms_get = function_body(API, "alarms_get")'
new = 'alarms_get = function_body(API, "operational_api_build_alarms_json")'

for path in files:
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{path}: expected exactly one legacy alarms_get reader, found {count}')
    text = text.replace(old, new, 1)
    path.write_text(text, encoding='utf-8')
    print(f'updated {path}')
