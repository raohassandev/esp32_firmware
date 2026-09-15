#!/usr/bin/env python3
"""Guard current-dev operator continuity, truthful verdict and consolidated shell ownership."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JS = (ROOT / "web/operator-continuity-verdict.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-continuity-verdict.css").read_text(encoding="utf-8")
THEME = (ROOT / "web/theme.js").read_text(encoding="utf-8")
SHELL = (ROOT / "web/product-shell-v2.js").read_text(encoding="utf-8")
SHELL_CSS = (ROOT / "web/product-shell-v2.css").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for forbidden in ("fetch(", "XMLHttpRequest", "setInterval("):
    require(forbidden not in JS, f"continuity/verdict module gained runtime acquisition/polling ownership: {forbidden}")
    require(forbidden not in SHELL, f"product shell gained runtime acquisition/polling ownership: {forbidden}")

require(JS.count("new MutationObserver(") == 1,
        "continuity/verdict must use exactly one scoped DOM observer")
for token in (
    "document.querySelectorAll(ROOTS).forEach((root)",
    "observer.observe(root, { childList: true, subtree: true })",
    "observer.observe(statusStrip, { childList: true, subtree: true, characterData: true })",
    "record.target.closest?.(ROOTS)",
    "record.target.closest?.('.status-strip')",
):
    require(token in JS, f"scoped observer contract missing: {token}")
require("observer.observe(document" not in JS and "observer.observe(document.body" not in JS,
        "continuity observer broadened to the whole document/body")

for token in (
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

require("themeToggleButton" in THEME, "current theme control id changed unexpectedly")
require("clickExisting('themeToggleButton')" in SHELL,
        "consolidated shell Theme action no longer delegates to the authoritative theme control")
for token in (
    "node('button', 'shell-overflow-button', 'More')",
    "Open controller actions",
    "Operational reports",
    "Navigation ordering/grouping is owned by Industrial UI v1",
):
    require(token in SHELL, f"consolidated shell interaction/ownership contract missing: {token}")
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
    "product-shell-v2.js", "product-shell-v2.css",
):
    require(filename in CMAKE, f"asset is not copied/embedded by CMake: {filename}")

for getter in (
    "web_assets_operator_continuity_verdict_js",
    "web_assets_operator_continuity_verdict_css",
    "web_assets_product_shell_v2_js",
    "web_assets_product_shell_v2_css",
):
    require(getter in ASSETS_H, f"asset getter declaration missing: {getter}")
    require(getter in ASSETS_C, f"asset getter implementation missing: {getter}")
    require(getter in SERVER, f"asset is not served in the composite bundle: {getter}")

for retired in ("shell-current-fixes.js", "shell-current-fixes.css", "shell_current_fixes"):
    require(retired not in CMAKE and retired not in ASSETS_C and retired not in ASSETS_H and retired not in SERVER,
            f"retired shell repair layer returned to the embedded bundle: {retired}")
require(not (ROOT / "web/shell-current-fixes.js").exists(), "retired shell-current-fixes.js returned")
require(not (ROOT / "web/shell-current-fixes.css").exists(), "retired shell-current-fixes.css returned")

require(SERVER.index("web_assets_operator_continuity_verdict_js") > SERVER.index("web_assets_product_experience_v2_js"),
        "continuity/verdict must execute after product-experience-v2")
require(SERVER.index("web_assets_operator_continuity_verdict_css") > SERVER.index("web_assets_product_experience_v2_css"),
        "verdict CSS must follow product-experience-v2 CSS")

print("Current-dev operator continuity, truthful verdict, and consolidated shell contract passed")