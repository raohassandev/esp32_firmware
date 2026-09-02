#!/usr/bin/env python3
"""Keep the obsolete Engineering session wrapper out of the firmware image."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
ASSETS_C = (ROOT / "components/web_server/web_assets.c").read_text(encoding="utf-8")
ASSETS_H = (ROOT / "components/web_server/include/web_assets.h").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
LEGACY = ROOT / "web/engineering-session-resilience.js"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# The legacy source may stay in git as history, but it must consume no firmware
# asset bytes and may never become a second authentication/runtime owner.
require(LEGACY.exists(), "legacy source history unexpectedly disappeared")
require("engineering-session-resilience.js" not in CMAKE,
        "legacy Engineering session wrapper must not be copied or embedded")
for source in (ASSETS_C, ASSETS_H, SERVER):
    require("engineering_session_resilience" not in source,
            "legacy Engineering session wrapper leaked back into firmware linkage")

require(SERVER.count("web_assets_product_mode_js") == 1,
        "product-mode.js must remain the single served Engineering auth owner")
require("web_assets_product_mode_js" in ASSETS_C and
        "web_assets_product_mode_js" in ASSETS_H,
        "active Engineering auth asset linkage is missing")

print("dead Engineering session asset exclusion contract passed")
