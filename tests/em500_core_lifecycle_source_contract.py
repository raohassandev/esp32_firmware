#!/usr/bin/env python3
"""EM500 live polling must be route/scope/visibility bounded."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = (ROOT / "web/em500-core.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("setInterval(" not in CORE,
        "EM500 core must not retain an unconditional live interval")
require("clearInterval(" not in CORE,
        "EM500 core polling should use one timeout lifecycle")
for token in (
    "const LIVE_REFRESH_MS = 5000",
    "function livePollingActive()",
    "!document.hidden",
    "currentRoute() === 'meters'",
    "state.activeTab === 'live'",
    "meterScopeAllowed()",
    "function cancelPolling()",
    "window.clearTimeout(state.pollTimer)",
    "function schedulePolling(",
    "window.setTimeout(async () =>",
    "await refreshActive(true)",
    "schedulePolling();",
    "function resetPollingAfter(action)",
    "visibilitychange",
    "hashchange",
    "beforeunload",
    "state.requestController?.abort()",
):
    require(token in CORE, f"EM500 polling lifecycle safeguard missing: {token}")

for token in (
    "new AbortController()",
    "window.setTimeout(() => controller.abort(), timeoutMs)",
    "signal: controller.signal",
    "Meter request timed out",
):
    require(token in CORE, f"EM500 request deadline safeguard regressed: {token}")

# Non-live tabs are manual/event driven; the recurring timer must only exist for
# the visible, authorized Live measurements view.
require("state.activeTab === 'live' && meterScopeAllowed()" in CORE,
        "recurring EM500 live refresh is not access/tab scoped")
require("await enterWorkspace();\n        schedulePolling();" in CORE,
        "startup must schedule only after the initial workspace refresh")

print("EM500 core polling lifecycle source contract passed")
