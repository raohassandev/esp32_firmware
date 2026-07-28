from pathlib import Path

root = Path(__file__).resolve().parents[1]
cmake = (root / 'components/web_server/CMakeLists.txt').read_text(encoding='utf-8')
assets_h = (root / 'components/web_server/include/web_assets.h').read_text(encoding='utf-8')
assets_c = (root / 'components/web_server/web_assets.c').read_text(encoding='utf-8')
server = (root / 'components/web_server/web_server.c').read_text(encoding='utf-8')
mode = (root / 'web/product-mode.js').read_text(encoding='utf-8')
resilience = (root / 'web/engineering-session-resilience.js').read_text(encoding='utf-8')

assert 'engineering-session-resilience.js' in cmake
assert 'web_assets_engineering_session_resilience_js' in assets_h
assert 'engineering_session_resilience_js' in assets_c
assert 'web_assets_engineering_session_resilience_js' in server
assert server.index('web_assets_engineering_session_resilience_js') < server.index('web_assets_product_mode_js')
assert "'/api/engineering/session'" in resilience
assert 'response.status === 401' in resilience
assert 'restored' in resilience
assert 'renewEngineeringSession' in mode
assert 'retryProtectedRequest' in mode
assert "openLogin('Engineering session expired. Sign in again.')" in mode
assert mode.index('renewEngineeringSession') < mode.index("openLogin('Engineering session expired. Sign in again.')")

print('engineering auth-loop prevention source contract: PASS')
