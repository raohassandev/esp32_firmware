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
    "readiness",
    "activateRoute",
    "industrialTargetRoute",
    "enhanceEquipmentAccess",
    ".op-equipment-bar, .op-inverter-row",
    "industrial-state-offline",
    "industrial-state-stale",
):
    assert token in JS, f"missing industrial shell behavior: {token}"

# Task-based Operator information architecture is owned by this final layer.
# Keep legacy route IDs for compatibility, but never expose the old technical
# Dashboard/Meters/Inverters wording as the primary navigation model.
for token in (
    "OPERATOR_NAV_LABELS",
    "dashboard: 'Overview'",
    "meters: 'Grid'",
    "inverters: 'Solar'",
    "alarms: 'Alarms'",
    "readiness: 'Readiness'",
    "normalizeOperatorNavigationLabels",
    "root.querySelectorAll(`[data-route=\"${name}\"]`)",
    "link.setAttribute('aria-label', label)",
):
    assert token in JS, f"missing task-based operator navigation contract: {token}"

assert "dashboard: 'Overview', meters: 'Grid', inverters: 'Solar'" in JS
assert "if (label) label.textContent = engineering ? 'Control' : 'Readiness';" in JS
assert "slot.setAttribute('aria-label', engineering ? 'PV-DG control' : 'Readiness');" in JS

assert "engineering ? 'control' : 'readiness'" in JS
assert "engineering ? 'wifi' : 'readiness'" in JS
assert "plantTone === 'good' ? 'readiness' : 'alarms'" in JS

# Engineering is a task workspace, not a flat collection of configuration tiles.
for token in (
    "ENGINEERING_GROUPS",
    "Commission",
    "Configure",
    "Primary guided workflow",
    "Expert setup tools",
    "Backup, security and controller maintenance",
    "industrialEngineeringShell",
    "industrialEngineeringStatus",
    "industrial-engineering-group",
    "data-group-grid",
    "Automatic control remains locked during commissioning",
    "Review readiness",
    "Configuration and service changes remain subject",
    "composeEngineeringWorkspace",
):
    assert token in JS, f"missing industrial engineering UX contract: {token}"

# Existing tiles are reused by route; the industrial layer must not clone a
# second set of functional controls or invent new mutation paths.
for route_name in ("commissioning", "wifi", "meters", "inverters", "control", "system"):
    assert route_name in JS, f"engineering task route missing: {route_name}"
assert "target.append(tile)" in JS, "existing engineering tiles must be moved, not duplicated"
assert "sourceGrid.hidden = true" in JS, "legacy flat grid must leave the visual workflow"
assert "min-height:var(--industrial-touch)" in JS, "engineering task controls must preserve touch sizing"
assert "@media (max-width:900px),(max-height:560px)" in JS, "engineering workspace must include 800x480 layout"

# This layer remains presentation/navigation only. Authentication, configuration
# writes, polling and safety state continue to be owned by existing modules.
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
