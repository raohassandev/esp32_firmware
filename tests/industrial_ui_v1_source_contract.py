#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSS = (ROOT / "web/industrial-ui-v1.css").read_text(encoding="utf-8")
JS = (ROOT / "web/industrial-ui-v1.js").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")

for name in ("industrial-ui-v1.css", "industrial-ui-v1.js"):
    assert name in CMAKE, f"{name} must be copied and embedded"

for token in (
    "web_assets_industrial_ui_v1_css",
    "web_assets_industrial_ui_v1_js",
):
    assert token in ASSETS_H, f"missing web asset declaration: {token}"
    assert token in SERVER, f"missing web server asset: {token}"

for token in ("industrial_ui_v1_css", "industrial_ui_v1_js"):
    assert token in ASSETS_C, f"missing embedded asset implementation: {token}"

css_last = SERVER.index("web_assets_industrial_ui_v1_css")
ota_css = SERVER.index("web_assets_ota_css")
assert css_last > ota_css, "industrial CSS must be the final authoritative presentation layer"

js_last = SERVER.index("web_assets_industrial_ui_v1_js")
ota_js = SERVER.index("web_assets_ota_js")
assert js_last > ota_js, "industrial shell JS must run after compatibility/product modules"

for token in (
    "--industrial-touch: 44px",
    "industrial-command-bar",
    "industrial-nav-section",
    "industrial-role-badge",
    "industrial-alarm-button",
    "industrial-freshness",
    "@media (max-width: 900px), (max-height: 560px)",
    "grid-template-columns: repeat(4, minmax(0, 1fr))",
    "prefers-reduced-motion: reduce",
):
    assert token in CSS, f"missing industrial HMI presentation contract: {token}"

for token in (
    "NAV_GROUPS",
    "Operate",
    "Engineer",
    "Service",
    "installDashboardCommandBar",
    "installRoleBadge",
    "installAlarmControl",
    "installFreshness",
    "normalizeMobileNavigation",
    "experience-nav-label",
    "data-industrial-control-slot",
    "readiness",
    "activateRoute",
    "industrialTargetRoute",
    "enhanceEquipmentAccess",
    ".op-equipment-bar, .op-inverter-row",
    "industrial-state-offline",
    "industrial-state-stale",
):
    assert token in JS, f"missing industrial shell behavior: {token}"

assert "engineering ? 'control' : 'readiness'" in JS
assert "engineering ? 'wifi' : 'readiness'" in JS
assert "plantTone === 'good' ? 'readiness' : 'alarms'" in JS

for forbidden in (
    "fetch(",
    "XMLHttpRequest",
    "method: 'POST'",
    'method: "POST"',
    "method: 'PUT'",
    "method: 'DELETE'",
    "/api/",
):
    assert forbidden not in JS, f"industrial shell must stay DOM/route-only: {forbidden}"

assert "new MutationObserver(updateAllStatus).observe(strip" in JS
assert "attributeFilter: ['data-access']" in JS

print("industrial UI v1 source contract: PASS")
