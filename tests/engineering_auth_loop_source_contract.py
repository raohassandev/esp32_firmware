from pathlib import Path

root = Path(__file__).resolve().parents[1]
server = (root / 'components/web_server/web_server.c').read_text(encoding='utf-8')
mode = (root / 'web/product-mode.js').read_text(encoding='utf-8')
auth = (root / 'components/web_server/engineering_auth.c').read_text(encoding='utf-8')

# One runtime owner: the legacy resilience wrapper may remain as source history,
# but it must not be concatenated into the firmware application.
assert 'web_assets_engineering_session_resilience_js' not in server
assert server.count('web_assets_product_mode_js') == 1

# Production releases cannot silently authenticate Engineering access.
assert '#define AUTH_DEVELOPMENT_AUTO_UNLOCK 0' in auth

# Background API responses may update auth state, but they must never route the app.
fetch_start = mode.index('window.fetch = async')
fetch_end = mode.index('function node(', fetch_start)
fetch_block = mode[fetch_start:fetch_end]
assert 'openLogin(' not in fetch_block
assert "location.hash = '#/engineering'" not in fetch_block
assert 'markSessionExpired' in fetch_block
assert 'retryProtectedRequest' in fetch_block

# Only route enforcement may open the sign-in route.
enforce_start = mode.index('async function enforceRoute()')
enforce_end = mode.index("document.addEventListener('DOMContentLoaded'", enforce_start)
enforce_block = mode[enforce_start:enforce_end]
assert "openLogin('Sign in to open this engineering page.')" in enforce_block

# Operator routes must remain outside the protected route set.
protected_line = next(line for line in mode.splitlines() if 'const PROTECTED_ROUTES' in line)
for operator_route in ('dashboard', 'grid', 'meters', 'inverters', 'alarms', 'readiness'):
    assert f"'{operator_route}'" not in protected_line

print('engineering auth-loop prevention source contract: PASS')
