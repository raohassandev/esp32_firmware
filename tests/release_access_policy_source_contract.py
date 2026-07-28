from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
auth = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")
guard = (ROOT / "components/web_server/engineering_guard.c").read_text(encoding="utf-8")
product = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
operational = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")

assert "#define AUTH_DEVELOPMENT_AUTO_UNLOCK 0" in auth, "production build must not auto-unlock engineering"
assert "#define AUTH_DEVELOPMENT_AUTO_UNLOCK 1" not in auth

assert "state.authenticated))" not in product, "stale auth state must not redirect unrelated operator requests"
assert "|| state.authenticated" not in product
assert "PROTECTED_ROUTES.has(currentRoute())" in product
assert "const protectedRoute" in product

# Operator data sources must exist independently of engineering authentication.
assert '"/api/operator/history"' in operational
assert '"/api/operator/events"' in operational
assert 'GATEWAY_MODE_SAFE_METERS' in guard
assert 'GATEWAY_MODE_SAFE_INVERTERS' in guard
assert 'GATEWAY_MODE_SAFE_INVERTER_TELEMETRY' in guard

# Raw/register-oriented EM500 APIs are engineering workspaces, not operator dashboard feeds.
operator_files = [
    ROOT / "web/operator-view.js",
    ROOT / "web/operator-operations.js",
    ROOT / "web/operator-product-suite.js",
    ROOT / "web/prelab-readiness.js",
    ROOT / "web/product-shell-v2.js",
    ROOT / "web/product-experience-v2.js",
]
for path in operator_files:
    source = path.read_text(encoding="utf-8")
    assert "/api/meters/em500/snapshot" not in source, f"{path.name} must use sanitized operator APIs"
    assert "/api/meters/em500/history" not in source, f"{path.name} must use controller-resident operator history"
    assert "/api/meters/em500/settings" not in source, f"{path.name} must not read engineering setup data"

# Detailed meter and inverter workspaces remain hidden from operator sessions.
assert "#em500Workspace" in product
assert "#meterConfigurationEditor" in product
assert "#inverterConfigurationEditor" in product

print("release access policy source contract passed")
