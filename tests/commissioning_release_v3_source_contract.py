from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle

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

bundle.require_delivered("commissioning-release-v3.js")
bundle.require_delivered("commissioning-release-v3.css")
# Delivery is asserted above, once. It used to be asserted six times -- a
# getter in the header, a linker symbol in the source, a name in the server, for
# each of two files -- because under the old architecture all six had to line up.
# The bundle needs one fact, and six copies of a fact is six chances to disagree.

print('commissioning release V3 source contract: PASS')
