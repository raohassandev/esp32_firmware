from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
auth = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
guard = (ROOT / "components/web_server/engineering_guard.c").read_text(encoding="utf-8")
product = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
operational = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")

# Production authentication must be real, persistent and fail closed.
required_auth = [
    'AUTH_NAMESPACE "eng_auth"',
    'AUTH_RECORD_MAGIC',
    'AUTH_PBKDF2_ITERATIONS',
    'derive_password_hash',
    'mbedtls_md_hmac',
    'esp_fill_random',
    'HttpOnly; SameSite=Strict',
    'AUTH_SESSION_TIMEOUT_MS',
    'AUTH_MAX_FAILURES',
    'AUTH_LOCKOUT_MS',
    'engineering_login_temporarily_locked',
    'One-time Engineering setup code',
    'engineering_authentication_required',
    'engineering_password_setup_required',
    'temporary_field_bypass", false',
    'development_auto_unlock", false',
    'nvs_set_blob',
    'nvs_commit',
    'constant_time_equal',
    'json_depth_valid',
    'AUTH_BODY_DEADLINE_MS',
]
for token in required_auth:
    assert token in auth, f"production Engineering authentication missing: {token}"

for forbidden in [
    'AUTH_TEMPORARY_FIELD_BYPASS 1',
    'TEMPORARY FIELD-DEVELOPMENT BYPASS',
    'temporary_field_bypass", true',
    'Engineering authentication disabled for field development',
    'Password management is disabled while field-development bypass is active',
]:
    assert forbidden not in auth, f"development authentication bypass remains: {forbidden}"

assert 'return configured && session_cookie_valid(request, true);' in auth
assert 'return send_json(request, "401 Unauthorized", root);' in auth
assert 'return send_json(request, "429 Too Many Requests", root);' in auth
assert 'clear_session_cookie(request);' in auth
assert 'invalidate_session();' in auth

assert "state.authenticated))" not in product, "stale auth state must not redirect unrelated operator requests"
assert "|| state.authenticated" not in product
assert "PROTECTED_ROUTES.has(currentRoute())" in product
assert "const protectedRoute" in product

# Operator data sources must exist independently of Engineering authentication.
assert '"/api/operator/history"' in operational
assert '"/api/operator/events"' in operational
assert 'GATEWAY_MODE_SAFE_METERS' in guard
assert 'GATEWAY_MODE_SAFE_INVERTERS' in guard
assert 'GATEWAY_MODE_SAFE_INVERTER_TELEMETRY' in guard

# Raw/register-oriented APIs remain Engineering-only.
operator_files = [
    ROOT / "web/operator-view.js",
    ROOT / "web/operator-operations.js",
    ROOT / "web/operator-product-suite.js",
    ROOT / "web/product-shell-v2.js",
    ROOT / "web/product-experience-v2.js",
]
for path in operator_files:
    source = path.read_text(encoding="utf-8")
    assert "/api/meters/em500/snapshot" not in source, f"{path.name} must use sanitized operator APIs"
    assert "/api/meters/em500/history" not in source, f"{path.name} must use controller-resident operator history"
    assert "/api/meters/em500/settings" not in source, f"{path.name} must not read Engineering setup data"

assert "#em500Workspace" in product
assert "#meterConfigurationEditor" in product
assert "#inverterConfigurationEditor" in product

print("production Engineering access policy source contract passed")
