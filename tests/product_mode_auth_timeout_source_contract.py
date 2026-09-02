#!/usr/bin/env python3
"""The active Engineering auth owner must bound controller requests."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# product-mode.js is the one active auth owner in the firmware application.
require(SERVER.count("web_assets_product_mode_js") == 1,
        "product-mode.js must remain the single active Engineering auth owner")
require("web_assets_engineering_session_resilience_js" not in SERVER,
        "legacy engineering-session wrapper must remain outside the runtime bundle")

for token in (
    "const AUTH_REQUEST_TIMEOUT_MS = 5000",
    "async function engineeringFetch(",
    "new AbortController()",
    "window.setTimeout(() => controller.abort(), AUTH_REQUEST_TIMEOUT_MS)",
    "signal: controller.signal",
    "window.clearTimeout(timer)",
    "Engineering request timed out",
    "engineeringFetch('/api/engineering/session'",
    "engineeringFetch('/api/engineering/login'",
    "engineeringFetch('/api/engineering/logout'",
    "engineeringFetch('/api/engineering/password'",
):
    require(token in MODE, f"active Engineering auth deadline safeguard missing: {token}")

require("if (renewalPromise) return renewalPromise;" in MODE,
        "concurrent session renewal must remain single-flight")
require("credentials: 'same-origin'" in MODE,
        "Engineering auth requests lost same-origin credentials")
require("markSessionExpired" in MODE and "retryProtectedRequest" in MODE,
        "401 restoration semantics regressed")

# A background session timeout may not reroute the operator interface.
renew_start = MODE.index("async function renewEngineeringSession()")
renew_end = MODE.index("async function retryProtectedRequest", renew_start)
renew_block = MODE[renew_start:renew_end]
require("openLogin(" not in renew_block and "location.hash" not in renew_block,
        "background session timeout must not force route navigation")

print("active Engineering auth timeout source contract passed")
