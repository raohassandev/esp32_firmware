from pathlib import Path

root = Path(__file__).resolve().parents[1]
js = (root / 'web/commissioning-release-v3.js').read_text(encoding='utf-8')
css = (root / 'web/commissioning-release-v3.css').read_text(encoding='utf-8')
cmake = (root / 'components/web_server/CMakeLists.txt').read_text(encoding='utf-8')
assets_h = (root / 'components/web_server/include/web_assets.h').read_text(encoding='utf-8')
assets_c = (root / 'components/web_server/web_assets.c').read_text(encoding='utf-8')
server = (root / 'components/web_server/web_server.c').read_text(encoding='utf-8')

for token in [
    'Site','Devices','Channel','Modbus tuning','Connection test','Controller health','Review',
    'normal_ms','high_ms','low_ms','intercall_ms','response_delay_ms','retry_interval_ms',
    'detect_attempts','failure_ceiling','reconnect_ceiling','address_base','block_length',
    'byte_order','stale_ms','timingEstimate','validateTuning','/api/system/resources',
    'minimum_internal_heap_bytes','internal_fragmentation_ratio','temperature_available',
    'Running repeated-read qualification','localStorage','Export report','Blob',
    'RTU runtime is not available','automatic_control',
]:
    assert token in js, f'missing release commissioning behavior: {token}'

for forbidden in ['/api/control', '/api/inverter-command']:
    assert forbidden not in js, f'commissioning wizard must not call {forbidden}'

for token in ['@media(max-width:600px)', 'min-height:48px', '.cr-progress', '.cr-health-grid']:
    assert token in css, f'missing responsive commissioning style: {token}'

assert 'commissioning-release-v3.js' in cmake
assert 'commissioning-release-v3.css' in cmake
assert 'web_assets_commissioning_release_v3_js' in assets_h
assert 'web_assets_commissioning_release_v3_css' in assets_h
assert 'commissioning_release_v3_js_start' in assets_c
assert 'commissioning_release_v3_css_start' in assets_c
assert 'web_assets_commissioning_release_v3_js' in server
assert 'web_assets_commissioning_release_v3_css' in server

print('commissioning release V3 source contract: PASS')
