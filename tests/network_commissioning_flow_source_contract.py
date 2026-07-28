from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
js = (root / 'web' / 'network-commissioning-fix.js').read_text(encoding='utf-8')
server = (root / 'components' / 'web_server' / 'web_server.c').read_text(encoding='utf-8')
assets = (root / 'components' / 'web_server' / 'web_assets.c').read_text(encoding='utf-8')
cmake = (root / 'components' / 'web_server' / 'CMakeLists.txt').read_text(encoding='utf-8')
network_cmake = (root / 'components' / 'network_manager' / 'CMakeLists.txt').read_text(encoding='utf-8')
copy_source = (root / 'components' / 'network_manager' / 'network_wifi_copy.c').read_text(encoding='utf-8')

required = [
    "event.stopImmediatePropagation()",
    "'/api/engineering/session'",
    'ensureEngineeringSession',
    "'/api/wifi/config'",
    "'/api/system/restart'",
    'waitForController',
    'Engineering access renewed',
    'settings were saved',
    'recovery AP',
    "form.dataset.networkFlow = 'resilient'",
]
for token in required:
    assert token in js, f'missing Wi-Fi flow safeguard: {token}'

assert js.count('await ensureEngineeringSession()') >= 3, 'session must be established before save, baseline load and after restart'
assert 'credentials: \'same-origin\'' in js, 'session cookie must be included'
assert 'web_assets_network_commissioning_fix_js' in server
assert 'network_commissioning_fix_js_start' in assets
assert 'network-commissioning-fix.js' in cmake

for token in [
    'network_wifi_copy.c',
    'strlcpy=network_manager_wifi_strlcpy',
    '-include;network_wifi_copy.h',
]:
    assert token in network_cmake, f'maximum-length Wi-Fi copy integration missing: {token}'
assert 'destination_size == 32U || destination_size == 64U' in copy_source
assert 'source_length == destination_size' in copy_source
assert 'memcpy(destination, source, destination_size)' in copy_source

with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / 'network_wifi_copy_test'
    subprocess.run([
        'gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-I', str(root / 'components/network_manager/include'),
        str(root / 'tests/network_wifi_copy_test.c'),
        str(root / 'components/network_manager/network_wifi_copy.c'),
        '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

# The module must not write control or inverter commands.
for forbidden in ['/api/control', '/api/inverter-command', '/api/config/import']:
    assert forbidden not in js, f'network flow must not call {forbidden}'

print('network commissioning and maximum-length Wi-Fi field tests: PASS')
