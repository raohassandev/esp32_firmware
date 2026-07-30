#!/usr/bin/env python3
import re
from pathlib import Path


def code(text: str) -> str:
    """Executable source only; ownership comments name identifiers on purpose."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"(?m)^\s*//.*$", "", text)


ROOT = Path(__file__).resolve().parents[1]
js = (ROOT / 'web/product-experience-v2.js').read_text(encoding='utf-8')
css = (ROOT / 'web/product-experience-v2.css').read_text(encoding='utf-8')
cmake = (ROOT / 'components/web_server/CMakeLists.txt').read_text(encoding='utf-8')
server = (ROOT / 'components/web_server/web_server.c').read_text(encoding='utf-8')
header = (ROOT / 'components/web_server/include/web_assets.h').read_text(encoding='utf-8')
assets = (ROOT / 'components/web_server/web_assets.c').read_text(encoding='utf-8')
for route in ('dashboard','meters','inverters','control','alarms','readiness','engineering','commissioning','wifi','system'):
    assert f'{route}:' in js, f'missing page model for {route}'
for phrase in ('What is happening','what requires attention','safest next action'):
    pass
assert 'experience-masthead' in js and 'experience-masthead' in css
assert 'Operator scope' in js and 'Engineering scope' in js
# Navigation has one owner and it is product-shell-v2.js. This module used to
# insert the section labels while the shell reordered the items beneath them.
# The label styling still lives in this module's stylesheet, so the class must
# remain here; the DOM writing must not.
assert 'experience-nav-label' in css, 'navigation section labels lose their styling'
assert 'nav-list' not in code(js), 'product-experience-v2 must not touch the navigation list'
assert 'groupNavigation' not in code(js), 'navigation grouping belongs to product-shell-v2.js'
# One MutationObserver watches #mainContent for the whole application, owned by
# product-mode.js. This module subscribes to it.
assert 'new MutationObserver' not in code(js), 'must not install a second #mainContent observer'
assert 'onContentChange(' in js, 'must subscribe to the shared content notifier'
# Routing, titles and breadcrumbs belong to app.js.
assert 'pageTitle' not in code(js) and 'breadcrumbCurrent' not in code(js), 'route titles belong to app.js'
assert 'experience-legacy-intro' in js and 'display: none' in css
assert '@media (max-width: 650px)' in css
assert 'grid-template-columns: minmax(0, 1fr) !important' in css
for name in ('product-experience-v2.js','product-experience-v2.css'):
    assert name in cmake
assert 'web_assets_product_experience_v2_js' in header
assert 'web_assets_product_experience_v2_css' in header
assert 'product_experience_v2_js' in assets
assert 'product_experience_v2_css' in assets
assert 'web_assets_product_experience_v2_js' in server
assert 'web_assets_product_experience_v2_css' in server
print('product experience v2 source contract: PASS')
