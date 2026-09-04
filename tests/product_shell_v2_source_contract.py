#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
js = (ROOT / "web/product-shell-v2.js").read_text(encoding="utf-8")
css = (ROOT / "web/product-shell-v2.css").read_text(encoding="utf-8")
cmake = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
assets_h = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
assets_c = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
server = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")

for token in (
    "shell-health-button", "shell-overflow-button", "shell-page-context",
    "System health", "Controller menu", "Refresh data", "Engineering workspace",
):
    assert token in js or token in css, f"missing product shell behavior: {token}"

assert ".status-strip { display: none; }" in css, "legacy global status strip must be removed from normal page flow"
assert "#controllerPill { display: none; }" in css, "legacy controller pill must not compete with health control"
assert ".product-tool-button" in css and "display: none" in css, "secondary display tools must leave the permanent header"
assert "max-width: 650px" in css and "place-items: end stretch" in css, "mobile overflow sheet is required"

# Industrial UI is now the sole navigation-order/group owner. Product Shell V2
# must retain health/menu/context behavior without mutating sidebar order.
assert "function groupNavigation" not in js
assert "dataset.shellGrouped" not in js
assert "Navigation ordering/grouping is owned by Industrial UI v1" in js
assert "removeDuplicateIntros" in js, "duplicate page introductions must be suppressed"
assert ".observe(main, { childList: true })" in js, "shell may observe only direct page additions"
assert "subtree: true" not in js[js.find("function start()"):], "shell must not rescan the full live telemetry subtree"

for name in ("product-shell-v2.css", "product-shell-v2.js"):
    assert name in cmake, f"{name} must be embedded"
assert "web_assets_product_shell_v2_css" in assets_h
assert "web_assets_product_shell_v2_js" in assets_h
assert "product_shell_v2_css" in assets_c
assert "product_shell_v2_js" in assets_c
assert "web_assets_product_shell_v2_css" in server
assert "web_assets_product_shell_v2_js" in server

for forbidden in ("/api/control", "/api/inverter-command", "method: 'POST'", 'method: "POST"'):
    assert forbidden not in js, f"product shell must remain navigation/presentation only: {forbidden}"

print("product shell V2 source contract: PASS")
