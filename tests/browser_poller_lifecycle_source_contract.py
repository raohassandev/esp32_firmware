#!/usr/bin/env python3
"""Keep route-scoped browser pollers bounded and cancellable."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INVERTER = (ROOT / "web/inverter-telemetry.js").read_text(encoding="utf-8")
PRELAB = (ROOT / "web/prelab-readiness.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for name, source, route_token in (
    ("inverter telemetry", INVERTER, "#/inverters"),
    ("pre-lab readiness", PRELAB, "readiness"),
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
    require(route_token in source, f"{name} is not scoped to its route")

require("REQUEST_TIMEOUT_MS = 5000" in INVERTER,
        "inverter telemetry request timeout changed without updating the lifecycle contract")
require("REQUEST_TIMEOUT_MS = 5000" in PRELAB,
        "readiness request timeout changed without updating the lifecycle contract")
require("signal: controller.signal" in INVERTER,
        "inverter telemetry fetch must receive the abort signal")
require("api('/api/status', signal)" in PRELAB and
        "api('/api/engineering/session', signal)" in PRELAB,
        "all readiness fan-out requests must share the cancellable request signal")

print("Browser poller lifecycle contract passed")
