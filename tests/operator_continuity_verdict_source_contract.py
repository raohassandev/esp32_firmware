#!/usr/bin/env python3
"""Guard the current-baseline operator continuity/verdict presentation feature."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JS = (ROOT / "web/operator-continuity-verdict.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-continuity-verdict.css").read_text(encoding="utf-8")
ORDER = (ROOT / "web/app.js.order").read_text(encoding="utf-8").splitlines()
CSS_ORDER = (ROOT / "web/app.css.order").read_text(encoding="utf-8").splitlines()
EXPERIENCE = (ROOT / "web/product-experience-v2.js").read_text(encoding="utf-8")
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
SOURCE = (ROOT / "web/source-attribution.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for forbidden in ("fetch(", "XMLHttpRequest", "MutationObserver", "setInterval("):
    require(forbidden not in JS, f"presentation module gained forbidden runtime ownership: {forbidden}")

for token in (
    "window.AutomatrixEngineeringAccess?.onContentChange(scheduleRestore, { deep: true })",
    "window.addEventListener('amx-operator-view-rendered', scheduleRestore)",
    "document.addEventListener('focusin'",
    "document.addEventListener('toggle'",
    "target.focus({ preventScroll: true })",
    "record.open.has(detailKey(details))",
):
    require(token in JS, f"operator polling continuity safeguard missing: {token}")

for token in (
    "window.addEventListener('amx-controller-status'",
    "window.AutomatrixStatusCache?.payload",
    "window.AutomatrixSource?.attribution?.(latestStatus)",
    "ATTENTION REQUIRED",
    "PLANT STATUS UNKNOWN",
    "PLANT NORMAL",
    "source.known",
    "alarmsClear(alarms)",
    "document.documentElement.dataset.access === 'engineering'",
    "existing?.remove()",
):
    require(token in JS, f"truthful plant verdict safeguard missing: {token}")

for forbidden in ("grid_kw", "active_power_kw", "Math.sign", "attributed_to =", "source.attributed_to ="):
    require(forbidden not in JS, f"browser re-derived controller source authority: {forbidden}")

require("amx-controller-status', { detail: state.status }" in APP,
        "app.js no longer republishes authoritative status without another request")
require("window.AutomatrixSource = { attribution }" in SOURCE,
        "canonical source-attribution helper is unavailable")

# The current phase1 routes are newer than stale PR #17. This port must not roll
# them back while adding its missing presentation behaviors.
for token in ("generator:", "network:"):
    require(token in EXPERIENCE, f"current route semantics were rolled back: {token}")
require("wifi:" not in EXPERIENCE, "stale PR #17 wifi route was reintroduced")

js_entries = [line.strip() for line in ORDER if line.strip() and not line.lstrip().startswith("#")]
css_entries = [line.strip() for line in CSS_ORDER if line.strip() and not line.lstrip().startswith("#")]
require(js_entries.count("operator-continuity-verdict.js") == 1,
        "operator continuity/verdict JS is not bundled exactly once")
require(js_entries.index("operator-continuity-verdict.js") > js_entries.index("product-experience-v2.js"),
        "continuity/verdict module loads before the current product experience")
require(css_entries.count("operator-continuity-verdict.css") == 1,
        "operator continuity/verdict CSS is not bundled exactly once")

for token in (
    ".plant-verdict-rail",
    ".tone-good",
    ".tone-bad",
    "@media (max-width: 1180px)",
    "@media (max-width: 650px)",
    "var(--good)",
    "var(--bad)",
):
    require(token in CSS, f"responsive/theme verdict styling missing: {token}")
require("#" not in "\n".join(line for line in CSS.splitlines() if "var(" in line),
        "verdict CSS introduced a literal color fallback")

print("Operator continuity and truthful plant verdict contract passed")
