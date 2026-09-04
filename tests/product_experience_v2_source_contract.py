#!/usr/bin/env python3
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
js = (ROOT / 'web/product-experience-v2.js').read_text(encoding='utf-8')
css = (ROOT / 'web/product-experience-v2.css').read_text(encoding='utf-8')
cmake = (ROOT / 'components/web_server/CMakeLists.txt').read_text(encoding='utf-8')
server = (ROOT / 'components/web_server/web_server.c').read_text(encoding='utf-8')
header = (ROOT / 'components/web_server/include/web_assets.h').read_text(encoding='utf-8')
assets = (ROOT / 'components/web_server/web_assets.c').read_text(encoding='utf-8')
for route in ('dashboard','meters','inverters','control','alarms','readiness','engineering','commissioning','wifi','system'):
    assert f'{route}:' in js, f'missing page model for {route}'
assert 'experience-masthead' in js and 'experience-masthead' in css
assert 'Operator scope' in js and 'Engineering scope' in js
assert 'experience-legacy-intro' in js and 'display: none' in css
assert '@media (max-width: 650px)' in css
assert 'grid-template-columns: minmax(0, 1fr) !important' in css

# Industrial UI now owns sidebar information architecture. Product Experience V2
# remains responsible for route context/mastheads only and must not inject a
# second Operate / Commission & service hierarchy during startup.
assert 'function groupNavigation' not in js
assert 'experience-nav-label' not in js
assert 'experience-nav-label' not in css
assert 'Commission & service' not in js
assert 'Navigation hierarchy is owned by the final Industrial UI layer' in js

for name in ('product-experience-v2.js','product-experience-v2.css'):
    assert name in cmake
assert 'web_assets_product_experience_v2_js' in header
assert 'web_assets_product_experience_v2_css' in header
assert 'product_experience_v2_js' in assets
assert 'product_experience_v2_css' in assets
assert 'web_assets_product_experience_v2_js' in server
assert 'web_assets_product_experience_v2_css' in server
print('product experience v2 source contract: PASS')
