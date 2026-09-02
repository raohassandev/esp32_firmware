#!/usr/bin/env python3
"""Operator product view polling must be bounded to visible operator routes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("setInterval(" not in SOURCE,
        "operator view must not keep an unconditional interval alive")
for token in (
    "const REFRESH_MS = 5000",
    "const REQUEST_TIMEOUT_MS = 5000",
    "const ACTIVE_ROUTES = new Set(['dashboard', 'meters', 'inverters', 'control', 'system'])",
    "controllers: new Set()",
    "function pollingActive()",
    "!document.hidden",
    "ACTIVE_ROUTES.has(route())",
    "new AbortController()",
    "state.controllers.add(controller)",
    "window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS)",
    "signal: controller.signal",
    "state.controllers.delete(controller)",
    "function cancelRequests()",
    "controller.abort()",
    "function cancelTimer()",
    "window.clearTimeout(state.timer)",
    "function schedulePolling(",
    "function reconcilePolling(",
    "function manualRefresh()",
    "visibilitychange",
    "hashchange",
    "beforeunload",
):
    require(token in SOURCE, f"operator polling lifecycle safeguard missing: {token}")

require("if (!pollingActive() || state.busy) return;" in SOURCE,
        "operator fetch batch must be route/access/visibility scoped")
for endpoint in ("/api/status", "/api/meters", "/api/inverters", "/api/inverter-telemetry"):
    require(f"api('{endpoint}')" in SOURCE, f"operator payload endpoint regressed: {endpoint}")

# Preserve truthful rendering: unavailable quantities must continue to render as
# an em dash / not-monitored state rather than fabricated zero measurements.
require("if (!finite(value)) return '—';" in SOURCE,
        "operator view lost unknown-value rendering")
require("Production not monitored" in SOURCE and "Not measured" in SOURCE,
        "operator view lost measurement provenance wording")
require("buildPowerFlowModel" in SOURCE,
        "operator view must keep using the shared authoritative power-flow model")

print("operator view polling lifecycle source contract passed")
