#!/usr/bin/env python3
from pathlib import Path

path = Path('tests/alarm_management_source_contract.py')
text = path.read_text(encoding='utf-8')
old = 'alarms_get = function_body("alarms_get")\nrequire(\'"caused_by"\' in alarms_get and \'"role"\' in alarms_get,\n        "cause attribution is not part of the alarm listing")'
new = '''alarms_get = function_body("operational_api_build_alarms_json")
alarms_http = function_body("alarms_get")
require("operational_api_build_alarms_json()" in alarms_http and
        "return send_json(request, root);" in alarms_http,
        "alarm HTTP handler must remain a thin wrapper over the shared authoritative builder")
require('"caused_by"' in alarms_get and '"role"' in alarms_get,
        "cause attribution is not part of the alarm listing")'''
if old not in text:
    raise SystemExit('expected pre-refactor alarms_get contract block not found')
text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')
print('alarm management contract updated for shared builder ownership')
