#!/usr/bin/env python3
"""Source-detection persistence must revoke automatic command authority first."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/source_detection_api.c").read_text(encoding="utf-8")
GUARD = (ROOT / "components/web_server/source_detection_runtime_guard.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(text: str, earlier: str, later: str, message: str) -> None:
    require(earlier in text and later in text, message + " (token missing)")
    require(text.index(earlier) < text.index(later), message)


require("source_detection_config_save(&config);" in API,
        "source-detection save call shape changed; review guarded persistence")
require('"source_detection_runtime_guard.c"' in CMAKE,
        "source-detection runtime guard is not compiled")
require("source_detection_config_save=web_source_detection_config_save_guarded" in CMAKE,
        "source-detection API is not source-locally routed through the guard")

for token in (
    "app_config_t *application = malloc(sizeof(*application));",
    "config_manager_get_snapshot(application)",
    "application->control.enabled = false;",
    "config_manager_save(application)",
    "free(application);",
    "control_engine_force_disable();",
    "source_detection_config_save(source_config);",
):
    require(token in GUARD, f"source-detection safety guard missing: {token}")

require("app_config_t application;" not in GUARD,
        "source-detection HTTP guard must not place the full app config on its task stack")
require_before(GUARD, "application->control.enabled = false;",
               "config_manager_save(application)",
               "persisted automatic control is not disabled before source-model persistence")
require_before(GUARD, "config_manager_save(application)",
               "control_engine_force_disable();",
               "live control may be revoked without first persisting the fail-closed state")
require_before(GUARD, "control_engine_force_disable();",
               "source_detection_config_save(source_config);",
               "source-detection model may change before live command authority is revoked")

for forbidden in (
    "inverter_manager_set_total_power_kw",
    "modbus_tcp_",
    "meter_manager_read_registers",
    "vTaskDelay",
):
    require(forbidden not in GUARD,
            f"source-detection HTTP guard gained blocking/physical I/O: {forbidden}")

for token in (
    "s_runtime_forced_disabled = true",
    "s_safe_zero_pending = true",
    "inverter_manager_set_total_power_kw(0.0f)",
):
    require(token in CONTROL, f"control-task safe-zero mechanism missing: {token}")

print("source-detection configuration runtime-disable contract passed")
