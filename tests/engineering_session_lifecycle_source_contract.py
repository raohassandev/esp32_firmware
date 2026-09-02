#!/usr/bin/env python3
"""Engineering session renewal must be bounded, visible-only and cancellable."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "web/engineering-session-resilience.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("setInterval(" not in SOURCE,
        "engineering session renewal must not use an unconditional interval")
for token in (
    "const RENEW_MS = 5 * 60 * 1000",
    "const REQUEST_TIMEOUT_MS = 5000",
    "new AbortController()",
    "window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS)",
    "signal: controller.signal",
    "function cancelRenewal()",
    "window.clearTimeout(renewTimer)",
    "function cancelSessionRequest()",
    "sessionController?.abort()",
    "function scheduleRenewal(",
    "document.visibilityState !== 'visible'",
    "visibilitychange",
    "window.addEventListener('focus', renew)",
    "window.addEventListener('online', renew)",
    "beforeunload",
):
    require(token in SOURCE, f"engineering session lifecycle safeguard missing: {token}")

for token in (
    "credentials: 'same-origin'",
    "response.status === 401",
    "isApi(url) && !isAuthEndpoint(url)",
    "const restored = await establishSession(true)",
    "amx-engineering-session-ready",
):
    require(token in SOURCE, f"engineering session restore semantics regressed: {token}")

require("nativeFetch('/api/engineering/session'" in SOURCE,
        "session bootstrap endpoint changed unexpectedly")
require("if (bootstrapPromise) return bootstrapPromise;" in SOURCE,
        "concurrent API calls must continue sharing one session bootstrap")

print("engineering session lifecycle source contract passed")
