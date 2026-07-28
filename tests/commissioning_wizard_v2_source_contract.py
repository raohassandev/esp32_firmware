#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
js = (root / 'web' / 'commissioning-wizard-v2.js').read_text(encoding='utf-8')
css = (root / 'web' / 'commissioning-wizard-v2.css').read_text(encoding='utf-8')
cmake = (root / 'components' / 'web_server' / 'CMakeLists.txt').read_text(encoding='utf-8')
assets_h = (root / 'components' / 'web_server' / 'include' / 'web_assets.h').read_text(encoding='utf-8')
assets_c = (root / 'components' / 'web_server' / 'web_assets.c').read_text(encoding='utf-8')
server = (root / 'components' / 'web_server' / 'web_server.c').read_text(encoding='utf-8')

for label in ('Site details', 'Select devices', 'Communication channel', 'Connection status'):
    assert label in js, f'missing commissioning stage: {label}'

for device in ('EM500', 'Carlo Gavazzi', 'WM15', 'Circutor'):
    assert device in js, f'missing meter catalogue entry: {device}'

for token in ('Modbus TCP', 'Modbus RTU', 'IP address or hostname', 'TCP port', 'Unit ID',
              'RS-485 port', 'Baud rate', 'Parity', 'Data bits', 'Stop bits', 'Slave / Unit ID'):
    assert token in js, f'missing communication field: {token}'

assert '/api/meters/config' in js, 'meter TCP commissioning must save through the dedicated API'
assert '/api/inverter-probe' in js, 'inverter test must use the read-only probe'
assert 'RTU driver is not available' in js, 'unsupported RTU runtime must fail closed'
assert 'Automatic control remains disabled' in js, 'commissioning must not imply control approval'
assert "method: 'POST'" in js

for token in ('@media (max-width: 650px)', '@media (max-width: 360px)', 'min-height: 50px',
              'env(safe-area-inset-bottom)', '.cw-communication-layout', '.cw-actions'):
    assert token in css, f'missing mobile commissioning safeguard: {token}'

assert 'commissioning-wizard-v2.js' in cmake and 'commissioning-wizard-v2.css' in cmake
assert 'web_assets_commissioning_wizard_v2_js' in assets_h
assert 'web_assets_commissioning_wizard_v2_css' in assets_h
assert 'commissioning_wizard_v2_js_start' in assets_c
assert 'commissioning_wizard_v2_css_start' in assets_c
assert 'web_assets_commissioning_wizard_v2_js' in server
assert 'web_assets_commissioning_wizard_v2_css' in server

print('commissioning wizard V2 source contract: PASS')