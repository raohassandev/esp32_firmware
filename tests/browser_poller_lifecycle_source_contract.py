#!/usr/bin/env python3
"""Keep route-scoped browser pollers bounded and cancellable."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INVERTER = (ROOT / "web/inverter-telemetry.js").read_text(encoding="utf-8")
PRELAB = (ROOT / "web/prelab-readiness.js").read_text(encoding="utf-8")
SUITE = (ROOT / "web/operator-product-suite.js").read_text(encoding="utf-8")
DEVICES = (ROOT / "web/devices.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for name, source, route_token in (
    ("inverter telemetry", INVERTER, "#/inverters"),
    ("pre-lab readiness", PRELAB, "readiness"),
    ("operator product suite", SUITE, "PAYLOAD_ROUTES"),
):
    require("setInterval(" not in source,
            f"{name} must not use an unbounded interval")
    for token in (
        "AbortController",
        "controller.abort()",
        "window.setTimeout(() => controller.abort()",
        "window.clearTimeout(timeout)",
        "document.hidden",
        "visibilitychange",
        "hashchange",
        "beforeunload",
        "finally",
    ):
        require(token in source, f"{name} lifecycle safeguard missing: {token}")
    require(route_token in source, f"{name} is not scoped to its owning route set")

for name, source in (
    ("inverter telemetry", INVERTER),
    ("pre-lab readiness", PRELAB),
    ("operator product suite", SUITE),
):
    require("REQUEST_TIMEOUT_MS = 5000" in source,
            f"{name} request timeout changed without updating the lifecycle contract")

require("signal: controller.signal" in INVERTER,
        "inverter telemetry fetch must receive the abort signal")
require("api('/api/status', signal)" in PRELAB and
        "api('/api/engineering/session', signal)" in PRELAB,
        "all readiness fan-out requests must share the cancellable request signal")
require("api('/api/status', signal)" in SUITE and
        "api('/api/inverter-telemetry', signal)" in SUITE,
        "product-suite fan-out requests must share the cancellable request signal")
require("currentRoute() !== 'commissioning' || isEngineering()" in SUITE,
        "commissioning payload polling must remain Engineering-scoped")

# Device/dashboard diagnostics previously had an unconditional 5 s interval and
# a raw fetch with no timeout. It is a high-impact poller because /api/meters and
# /api/inverters can be used from the main product pages, so route/visibility
# cleanup and a bounded browser request are mandatory.
require("setInterval(" not in DEVICES,
        "device diagnostics must not keep an unconditional interval alive")
for token in (
    "const POLL_MS = 5000",
    "const REQUEST_TIMEOUT_MS = 5000",
    "DEVICE_ROUTES",
    "controllers: new Set()",
    "AbortController",
    "controller.abort()",
    "signal: controller.signal",
    "credentials: 'same-origin'",
    "window.clearTimeout(timeout)",
    "function lifecycleActive()",
    "document.hidden",
    "function cancelRequests()",
    "function cancelTimer()",
    "function schedulePoll(",
    "function reconcileLifecycle()",
    "visibilitychange",
    "hashchange",
    "beforeunload",
    "finally",
):
    require(token in DEVICES, f"device diagnostics lifecycle safeguard missing: {token}")
require("Controller request timed out" in DEVICES,
        "device diagnostics must surface request timeout distinctly")
require("Controller returned an incomplete response" in DEVICES,
        "truncated/non-JSON diagnostics must be treated as transport failure")
require("It does not cancel any ESP-side work already admitted" in DEVICES,
        "browser abort semantics must not claim ESP-side Modbus cancellation")
require("state.controllers.add(controller)" in DEVICES and
        "state.controllers.delete(controller)" in DEVICES,
        "every device request must be registered for lifecycle cancellation")
require("schedulePoll(0)" in DEVICES,
        "route/visibility re-entry must restart device polling without a stale interval")

print("Browser poller lifecycle contract passed")
