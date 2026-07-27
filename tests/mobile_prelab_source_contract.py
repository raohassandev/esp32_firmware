from pathlib import Path

root = Path(__file__).resolve().parents[1]
css = (root / 'web' / 'mobile-prelab-fixes.css').read_text(encoding='utf-8')
server = (root / 'components' / 'web_server' / 'web_server.c').read_text(encoding='utf-8')
assets = (root / 'components' / 'web_server' / 'web_assets.c').read_text(encoding='utf-8')
cmake = (root / 'components' / 'web_server' / 'CMakeLists.txt').read_text(encoding='utf-8')

required = [
    '@media (max-width: 650px)',
    '@media (max-width: 380px)',
    'overflow-x: hidden',
    'min-height: 48px',
    'font-size: 16px',
    '.wifi-network-row',
    '.wifi-network-actions',
    '.engineering-grid',
    'env(safe-area-inset-bottom)',
]
for token in required:
    assert token in css, f'missing mobile safeguard: {token}'

assert 'web_assets_mobile_prelab_fixes_css' in server
assert 'mobile_prelab_fixes_css_start' in assets
assert 'mobile-prelab-fixes.css' in cmake

print('mobile pre-lab layout contract: PASS')
