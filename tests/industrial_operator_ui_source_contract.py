#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
js = (ROOT / 'web/industrial-operator-ui.js').read_text(encoding='utf-8')
css = (ROOT / 'web/industrial-operator-ui.css').read_text(encoding='utf-8')
cmake = (ROOT / 'components/web_server/CMakeLists.txt').read_text(encoding='utf-8')
server = (ROOT / 'components/web_server/web_server.c').read_text(encoding='utf-8')
header = (ROOT / 'components/web_server/include/web_assets.h').read_text(encoding='utf-8')
assets = (ROOT / 'components/web_server/web_assets.c').read_text(encoding='utf-8')

# This layer must remain presentation-only. It may rearrange and hide browser
# content but must not add API traffic or any command path.
assert '/api/' not in js
assert 'fetch(' not in js
assert 'XMLHttpRequest' not in js

for phrase in (
    'OPERATOR VIEW',
    'ENGINEERING VIEW',
    'Show advanced service tools',
    'Same Engineering session; no additional authority is granted.',
    'Advanced communication settings',
    'View technical evidence',
):
    assert phrase in js, f'missing industrial UI behaviour: {phrase}'

for selector in (
    'body[data-audience="operator"] .industrial-internal-control-card',
    '.industrial-service-panel',
    '.industrial-evidence-block.is-collapsed',
    '.industrial-audience-badge',
):
    assert selector in css, f'missing industrial UI style: {selector}'

for name in ('industrial-operator-ui.js', 'industrial-operator-ui.css'):
    assert name in cmake, f'{name} is not embedded'

assert 'web_assets_industrial_operator_ui_js' in header
assert 'web_assets_industrial_operator_ui_css' in header
assert 'industrial_operator_ui_js' in assets
assert 'industrial_operator_ui_css' in assets
assert 'web_assets_industrial_operator_ui_js' in server
assert 'web_assets_industrial_operator_ui_css' in server

print('industrial operator UI source contract: PASS')
