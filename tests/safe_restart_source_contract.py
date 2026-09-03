#!/usr/bin/env python3
"""Engineering web restart must never reboot over an unconfirmed positive PV command."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB_API = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
SAFE = (ROOT / "components/web_server/safe_restart.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/web_server/include/safe_restart.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(text: str, earlier: str, later: str, message: str) -> None:
    require(earlier in text and later in text, message + " (token missing)")
    require(text.index(earlier) < text.index(later), message)


require('"/api/system/restart"' in WEB_API and "restart_task" in WEB_API,
        "Engineering restart endpoint/task shape changed")
require("esp_restart();" in WEB_API,
        "web restart task no longer reaches the source-local restart bridge")
require('"safe_restart.c"' in CMAKE,
        "safe restart implementation is not compiled")
require('"-include;safe_restart.h"' in CMAKE and "void web_safe_restart(void);" in HEADER,
        "web_api does not receive the safe restart declaration")
require("esp_restart=web_safe_restart" in CMAKE,
        "web_api esp_restart call is not source-locally routed through the safe restart bridge")

for token in (
    "control_engine_force_disable();",
    "control_engine_get_status(&status);",
    "!status.enabled",
    "isfinite(status.applied_pv_kw)",
    "status.applied_pv_kw <= 0.0f",
    "SAFE_RESTART_TIMEOUT_MS 30000U",
    "esp_restart();",
    "vTaskDelete(NULL);",
):
    require(token in SAFE, f"safe restart guard missing: {token}")

require_before(SAFE, "control_engine_force_disable();", "control_engine_get_status(&status);",
               "restart guard must revoke command authority before checking safe-zero")
require_before(SAFE, "status.applied_pv_kw <= 0.0f", "esp_restart();",
               "restart may occur before confirmed applied PV target reaches zero")
require_before(SAFE, "esp_timer_get_time() < deadline_us", "vTaskDelete(NULL);",
               "restart timeout must abort the task rather than bypassing safe-zero")

for forbidden in (
    "inverter_manager_set_total_power_kw",
    "meter_manager_read_registers",
    "modbus_tcp_",
):
    require(forbidden not in SAFE,
            f"web restart bridge must not own physical/Modbus I/O: {forbidden}")

# The actual zero remains owned and confirmed by the existing control task.
for token in (
    "s_safe_zero_pending = true",
    "inverter_manager_set_total_power_kw(0.0f)",
    "clear_safe_zero_pending();",
):
    require(token in CONTROL, f"control-task safe-zero confirmation missing: {token}")

print("safe-zero-before-web-restart source contract passed")
