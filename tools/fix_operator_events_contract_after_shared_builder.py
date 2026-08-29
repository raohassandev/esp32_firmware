#!/usr/bin/env python3
from pathlib import Path

path = Path('tests/operator_history_events_source_contract.py')
text = path.read_text(encoding='utf-8')
old = 'events = API[API.index("static esp_err_t events_get"):API.index("esp_err_t operational_api_register")]'
new = '''events = API[
    API.index("cJSON *operational_api_build_events_json(void)"):
    API.index("static esp_err_t events_get")
]
events_http = API[
    API.index("static esp_err_t events_get"):
    API.index("static const char *alarm_code_id")
]
require("operational_api_build_events_json()" in events_http and
        "return send_json(request, root);" in events_http,
        "operator events HTTP handler must remain a thin wrapper over the shared builder")'''
if old not in text:
    raise SystemExit('expected pre-refactor event contract slice not found')
text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')
print('operator event contract updated for shared builder ownership')
