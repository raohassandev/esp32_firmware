#!/usr/bin/env python3
"""Safety alarm publication must not expose a transient all-clear snapshot."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAFETY = (ROOT / "components/safety_manager/safety_manager.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    "static portMUX_TYPE s_alarm_lock = portMUX_INITIALIZER_UNLOCKED;",
    "uint32_t alarm_flags = 0;",
    "alarm_flags |= SAFETY_ALARM_METER_OFFLINE",
    "alarm_flags |= SAFETY_ALARM_METER_STALE",
    "portENTER_CRITICAL(&s_alarm_lock);",
    "s_alarm_flags = alarm_flags;",
    "portEXIT_CRITICAL(&s_alarm_lock);",
    "uint32_t alarm_flags = s_alarm_flags;",
):
    require(token in SAFETY, f"atomic safety-alarm snapshot contract missing: {token}")

limit_start = SAFETY.index("float safety_manager_limit_target_kw")
getter_start = SAFETY.index("uint32_t safety_manager_get_alarm_flags")
limit = SAFETY[limit_start:getter_start]
getter = SAFETY[getter_start:]
require("s_alarm_flags = 0;" not in limit,
        "limit path must not publish a transient shared all-clear before evaluating alarms")
require(limit.index("uint32_t alarm_flags = 0;") < limit.index("s_alarm_flags = alarm_flags;"),
        "complete local alarm snapshot must be built before publication")
require(getter.index("portENTER_CRITICAL(&s_alarm_lock);") < getter.index("s_alarm_flags") <
        getter.index("portEXIT_CRITICAL(&s_alarm_lock);"),
        "alarm getter must read the published snapshot under the same lock")

print("safety alarm atomic snapshot contract passed")
