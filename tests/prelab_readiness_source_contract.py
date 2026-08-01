#!/usr/bin/env python3
import re
from pathlib import Path
import sys as _sys, pathlib as _pathlib
_sys.path.insert(0, str(_pathlib.Path(__file__).resolve().parent))
import bundle_membership as bundle

ROOT = Path(__file__).resolve().parents[1]
js = (ROOT / "web/prelab-readiness.js").read_text(encoding="utf-8")
css = (ROOT / "web/prelab-readiness.css").read_text(encoding="utf-8")
cmake = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
assets_h = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
assets_c = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
server = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
auth = (ROOT / "components/web_server/engineering_auth.c").read_text(encoding="utf-8")

required_apis = (
    "/api/status",
    "/api/meters",
    "/api/inverters",
    "/api/operator/history?range=15m",
    "/api/operator/events",
    "/api/engineering/session",
)
for endpoint in required_apis:
    assert endpoint in js, f"readiness workspace must query {endpoint}"

for check_id in (
    "controller", "network", "meter", "solar", "history",
    "alarms", "control", "writes", "engineering", "development",
):
    assert f"'{check_id}'" in js, f"missing readiness check: {check_id}"

assert "status.control_enabled ? 'block' : 'pass'" in js, "enabled control must block pre-lab readiness"
assert "commandable_rated_kw" in js, "write eligibility must be surfaced"
assert "Automatic control and physical inverter writes" not in js, "readiness UI must not claim approval"
assert "Export snapshot" in js and "JSON.stringify(report" in js, "diagnostic export is required"
assert "setInterval(refreshAll, 15000)" in js, "readiness must refresh periodically"
# Audit S4: the poll must be scoped to its own route. Six requests every 15 s
# from every screen -- including the ~26 KB /api/operator/history that S3
# measured returning 500 under browser load -- fetched data nothing displayed.
assert "route() !== 'readiness'" in js, "the readiness poll must be scoped to its own route"
assert js.index("route() !== 'readiness'") < js.index("api('/api/status')"), \
    "the route check must precede the requests, not filter them afterwards"
# It must still load on arrival, or the page shows nothing for 15 s.
assert "refreshAll(); render();" in js, "navigating to readiness must refresh immediately"
# Routing, titles and breadcrumbs belong to app.js alone.
code = re.sub(r"(?m)^\s*//.*$", "", re.sub(r"/\*.*?\*/", "", js, flags=re.S))
assert "pageTitle" not in code and "breadcrumbCurrent" not in code, "route titles belong to app.js"
assert "document.title" not in code, "the document title belongs to app.js"
assert "temporary_field_bypass" in js, "unexpected bypass state must remain visible in readiness"
assert "AUTH_TEMPORARY_FIELD_BYPASS" not in auth, "production authentication must not contain a bypass switch"
assert '"temporary_field_bypass", false' in auth, "session API must report that bypass is disabled"
assert "One-time Engineering setup code" in auth, "secure first-password setup must use a serial-only code"
assert "AUTH_SESSION_TIMEOUT_MS" in auth and "AUTH_LOCKOUT_MS" in auth

assert bundle.delivered("prelab-readiness.js") and bundle.delivered("prelab-readiness.css")
assert bundle.delivered("prelab-readiness.js")
assert bundle.delivered("prelab-readiness.css")
assert "prelab_readiness_js_start" in assets_c
assert "prelab_readiness_css_start" in assets_c
assert bundle.delivered("prelab-readiness.js")
assert bundle.delivered("prelab-readiness.css")
assert ".prelab-grid" in css and ".prelab-check" in css

# Safety: the readiness workspace is observational and must never invoke a write API.
for forbidden in (
    "fetch('/api/control", "fetch('/api/inverter-command", "method: 'POST'",
    'method: "POST"', "/api/system/restart", "/api/config",
):
    assert forbidden not in js, f"readiness workspace must remain read-only: {forbidden}"

print("pre-lab readiness source contract: PASS")
