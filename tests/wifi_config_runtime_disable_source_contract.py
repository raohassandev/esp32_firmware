#!/usr/bin/env python3
"""Wi-Fi persistence must revoke live command authority before changing transport."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB_API = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
GUARD = (ROOT / "components/web_server/wifi_config_runtime_guard.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(text: str, earlier: str, later: str, message: str) -> None:
    require(earlier in text and later in text, message + " (token missing)")
    require(text.index(earlier) < text.index(later), message)


require("config->wifi = next;" in WEB_API and "config_manager_save(config);" in WEB_API,
        "Wi-Fi save call shape changed; review the runtime-disable route")
require('"wifi_config_runtime_guard.c"' in CMAKE,
        "Wi-Fi runtime-disable guard is not compiled")
require("config_manager_save=web_wifi_config_manager_save_guarded" in CMAKE,
        "web_api Wi-Fi save is not routed through the runtime-disable guard")

for token in (
    "app_config_t guarded = *config;",
    "guarded.control.enabled = false;",
    "control_engine_force_disable();",
    "config_manager_save(&guarded);",
):
    require(token in GUARD, f"Wi-Fi persistence safety guard missing: {token}")
require_before(GUARD, "guarded.control.enabled = false;", "control_engine_force_disable();",
               "persisted control must be made disabled before runtime authority is revoked")
require_before(GUARD, "control_engine_force_disable();", "config_manager_save(&guarded);",
               "Wi-Fi state may persist before live command authority is revoked")

# The HTTP-side guard must never perform physical inverter or Modbus work.
for forbidden in (
    "inverter_manager_set_total_power_kw",
    "modbus_tcp_",
    "meter_manager_read_registers",
    "vTaskDelay",
):
    require(forbidden not in GUARD, f"Wi-Fi HTTP guard gained blocking/physical I/O: {forbidden}")

for token in (
    "s_runtime_forced_disabled = true",
    "s_safe_zero_pending = true",
    "inverter_manager_set_total_power_kw(0.0f)",
):
    require(token in CONTROL, f"control-task safe-zero mechanism missing: {token}")

print("Wi-Fi configuration runtime-disable contract passed")
