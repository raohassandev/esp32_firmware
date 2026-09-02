#!/usr/bin/env python3
"""Operator history/alarm polling must stop outside its useful browser lifecycle."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("setInterval(" not in SOURCE,
        "operator operations must not keep an unconditional interval alive")
for token in (
    "const REFRESH_MS = 10000",
    "const REQUEST_TIMEOUT_MS = 6000",
    "controllers: new Set()",
    "OPERATOR_ROUTES",
    "document.hidden",
    "function historyActive()",
    "function alarmsActive()",
    "function cancelRequests()",
    "controller.abort()",
    "function cancelTimer()",
    "window.clearTimeout(state.timer)",
    "function schedulePoll()",
    "window.setTimeout",
    "function reconcileLifecycle()",
    "visibilitychange",
    "hashchange",
    "beforeunload",
):
    require(token in SOURCE, f"operator operations lifecycle safeguard missing: {token}")

require("state.controllers.add(controller)" in SOURCE and
        "state.controllers.delete(controller)" in SOURCE,
        "every operator request must be registered for route/visibility cancellation")
require("if (!historyActive() || state.busy) return;" in SOURCE,
        "history/events polling must be route/access/visibility scoped")
require("if (!alarmsActive()) return;" in SOURCE,
        "alarm polling must be route/access/visibility scoped")

# Preserve the operationally important acknowledgement behavior while changing polling.
for token in (
    "'/api/operator/alarms/ack'",
    "method: 'POST'",
    "authenticated engineering session",
    "rtn_unacknowledged",
    "Nothing was changed.",
):
    require(token in SOURCE, f"alarm acknowledgement contract regressed: {token}")

print("operator operations lifecycle source contract passed")
