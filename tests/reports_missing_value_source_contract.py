#!/usr/bin/env python3
"""Fail-closed source contract for controller-resident operational reports.

Missing controller evidence must stay missing. JavaScript Number(null) == 0 must
never turn an absent power/count/boolean field into an apparent measured zero.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
REPORTS = (ROOT / "web/reports.js").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    "function finiteValue(value)",
    "value === null || value === undefined || value === ''",
    "return number === null ? '--'",
    "const value = finiteValue(sample?.[key]);",
    "return value === null ? null : { index, value }",
    "function formatBoolean(value, yes, no)",
    "function formatFlags(value)",
    "function exportBoolean(value)",
    "typeof value === 'boolean' ? value : ''",
    "exportValue(sample.grid_kw)",
    "exportValue(sample.solar_kw)",
    "formatBoolean(sample.meter_online, 'Online', 'Unavailable')",
    "formatBoolean(sample.control_enabled, 'Enabled', 'Disabled')",
):
    require(token in REPORTS, f"reports missing-value contract missing: {token}")

require("const value = Number(sample?.[key]);" not in REPORTS,
        "chart path can still coerce null measurements to zero")
require(re.search(r"(?<![A-Za-z0-9_$])Boolean\(sample\.meter_online\)", REPORTS) is None,
        "CSV path can still directly coerce missing meter state to false")
require(re.search(r"(?<![A-Za-z0-9_$])Boolean\(sample\.control_enabled\)", REPORTS) is None,
        "CSV path can still directly coerce missing control state to false")
require("Number.isFinite(Number(history.sample_interval_ms))" not in REPORTS,
        "missing sample interval can still be rendered as zero seconds")

print("Reports missing-value source contract passed")
