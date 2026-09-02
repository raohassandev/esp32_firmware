#!/usr/bin/env python3
"""Lock the main app status owner to one bounded, visibility-aware poller."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("const STATUS_REFRESH_MS = 2000;" in APP,
        "main status cadence must remain 2 seconds while the page is visible")
require("const STATUS_REQUEST_TIMEOUT_MS = 5000;" in APP,
        "main status request deadline must remain bounded at 5 seconds")
require("setInterval(" not in APP,
        "app.js must not own an unbounded interval")

for token in (
    "statusTimer",
    "statusController",
    "AbortController",
    "controller.abort()",
    "window.setTimeout",
    "window.clearTimeout",
    "document.hidden",
    "visibilitychange",
    "beforeunload",
    "scheduleStatusRefresh",
    "reconcileStatusLifecycle",
    "manualRefreshStatus",
    "finally",
):
    require(token in APP, f"main status lifecycle safeguard missing: {token}")

require("api('/api/status', { signal: controller.signal })" in APP,
        "only the status request must receive the bounded abort signal")
require("byId('refreshButton').addEventListener('click', manualRefreshStatus)" in APP,
        "manual Refresh must reconcile the single status schedule")
require("cancelStatusTimer();\n            cancelStatusRequest();" in APP,
        "unload/startup failure must cancel scheduled and in-flight status work")
require("await Promise.allSettled([loadConfig(), refreshStatus(true)]);" in APP and
        "scheduleStatusRefresh();" in APP,
        "startup must perform one initial status refresh then enter the bounded schedule")

# Guard against accidentally changing write/config semantics while hardening the
# read-only status owner. These calls must continue through the existing generic
# API path rather than inheriting the status deadline implicitly.
require("async function api(path, options = {})" in APP,
        "generic API wrapper contract changed unexpectedly")
require("const result = await api('/api/config', {" in APP,
        "configuration save path was removed or rewritten")
require("await api('/api/system/restart', { method: 'POST' });" in APP,
        "controller restart path was removed or rewritten")

print("Main app status poller lifecycle contract passed")
