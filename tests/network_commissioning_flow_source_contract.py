from pathlib import Path

root = Path(__file__).resolve().parents[1]
js = (root / 'web' / 'network-commissioning-fix.js').read_text(encoding='utf-8')
server = (root / 'components' / 'web_server' / 'web_server.c').read_text(encoding='utf-8')
assets = (root / 'components' / 'web_server' / 'web_assets.c').read_text(encoding='utf-8')
cmake = (root / 'components' / 'web_server' / 'CMakeLists.txt').read_text(encoding='utf-8')

required = [
    "event.stopImmediatePropagation()",
    "'/api/wifi/config'",
    "'/api/system/restart'",
    'waitForController',
    'settings were saved',
    'recovery AP',
    "form.dataset.networkFlow = 'resilient'",
]
for token in required:
    assert token in js, f'missing Wi-Fi flow safeguard: {token}'

assert 'web_assets_network_commissioning_fix_js' in server
assert 'network_commissioning_fix_js_start' in assets
assert 'network-commissioning-fix.js' in cmake

# The module must not write control or inverter commands.
for forbidden in ['/api/control', '/api/inverter-command', '/api/config/import']:
    assert forbidden not in js, f'network flow must not call {forbidden}'

print('network commissioning flow contract: PASS')
