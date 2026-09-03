#!/usr/bin/env python3
"""Guard the current-dev operator continuity, verdict and shell repair slice."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JS = (ROOT / "web/operator-continuity-verdict.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-continuity-verdict.css").read_text(encoding="utf-8")
SHELL_JS = (ROOT / "web/shell-current-fixes.js").read_text(encoding="utf-8")
SHELL_CSS = (ROOT / "web/shell-current-fixes.css").read_text(encoding="utf-8")
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
OPERATOR = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")
THEME = (ROOT / "web/theme.js").read_text(encoding="utf-8")
SHELL = (ROOT / "web/product-shell-v2.js").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for forbidden in ("fetch(", "XMLHttpRequest", "MutationObserver", "setInterval("):
    require(forbidden not in JS, f"continuity/verdict module gained runtime ownership: {forbidden}")
    require(forbidden not in SHELL_JS, f"shell repair gained runtime ownership: {forbidden}")

for token in (
    "window.addEventListener('amx-operator-view-rendered', scheduleRestore)",
    "document.addEventListener('focusin'",
    "document.addEventListener('toggle'",
    "target.focus({ preventScroll: true })",
    "record.open.has(detailKey(details))",
    "ATTENTION REQUIRED",
    "PLANT STATUS UNKNOWN",
    "PLANT NORMAL",
    "document.documentElement.dataset.access === 'engineering'",
    "existing?.remove()",
):
    require(token in JS, f"operator continuity/verdict safeguard missing: {token}")

for status_id in (
    "statusController", "statusNetwork", "statusMeter", "statusControl", "statusAlarms", "statusUpdated"
):
    require(status_id in JS, f"verdict no longer consumes existing status text: {status_id}")

for forbidden in ("grid_power_kw", "active_power_kw", "Math.sign", "/api/"):
    require(forbidden not in JS, f"presentation module re-derived authority or acquired data: {forbidden}")

require("amx-controller-status" in APP and "CustomEvent" in APP,
        "app.js must republish its existing status result without another request")
require("amx-operator-view-rendered" in OPERATOR and "CustomEvent" in OPERATOR,
        "operator-view must announce DOM replacement so continuity can restore state")

require("themeToggleButton" in THEME, "current theme control id changed unexpectedly")
require("clickExisting('themeToggle')" in SHELL,
        "stale shell Theme bridge disappeared; reevaluate whether the compatibility repair is still needed")
for token in (
    "label !== 'Theme'",
    "document.getElementById('themeToggleButton')?.click()",
    "button.textContent !== 'More'",
    "Open controller actions",
):
    require(token in SHELL_JS, f"current shell interaction repair missing: {token}")
for token in ("#themeToggleButton", "@media (max-width: 1180px)", ".shell-overflow-button"):
    require(token in SHELL_CSS, f"narrow shell consolidation missing: {token}")

for token in (
    ".plant-verdict-rail", ".tone-good", ".tone-bad",
    "@media (max-width: 1180px)", "@media (max-width: 650px)",
    "var(--good)", "var(--bad)",
):
    require(token in CSS, f"responsive/theme verdict styling missing: {token}")

for filename in (
    "operator-continuity-verdict.js", "operator-continuity-verdict.css",
    "shell-current-fixes.js", "shell-current-fixes.css",
):
    require(filename in CMAKE, f"asset is not copied/embedded by CMake: {filename}")

for getter in (
    "web_assets_operator_continuity_verdict_js",
    "web_assets_operator_continuity_verdict_css",
    "web_assets_shell_current_fixes_js",
    "web_assets_shell_current_fixes_css",
):
    require(getter in ASSETS_H, f"asset getter declaration missing: {getter}")
    require(getter in ASSETS_C, f"asset getter implementation missing: {getter}")
    require(getter in SERVER, f"asset is not served in the composite bundle: {getter}")

require(SERVER.index("web_assets_shell_current_fixes_js") > SERVER.index("web_assets_product_shell_v2_js"),
        "shell repair must execute after product-shell-v2")
require(SERVER.index("web_assets_operator_continuity_verdict_js") > SERVER.index("web_assets_product_experience_v2_js"),
        "continuity/verdict must execute after product-experience-v2")
require(SERVER.index("web_assets_shell_current_fixes_css") > SERVER.index("web_assets_product_shell_v2_css"),
        "shell repair CSS must follow product-shell-v2 CSS")
require(SERVER.index("web_assets_operator_continuity_verdict_css") > SERVER.index("web_assets_product_experience_v2_css"),
        "verdict CSS must follow product-experience-v2 CSS")

print("Current-dev operator continuity, truthful verdict, and shell repair contract passed")
