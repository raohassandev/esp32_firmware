#!/usr/bin/env python3
"""Commissioning/config writes must disable the already-running controller before persistence.

This is the consolidated inventory for write surfaces that can invalidate live
meter, inverter, transport or source-evidence assumptions. Source-local helper
contracts may add deeper checks, but any new safety-relevant mutation surface
must also be represented here so it cannot quietly bypass runtime disable.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
METER = (ROOT / "components/web_server/meter_config_api.c").read_text(encoding="utf-8")
INVERTER = (ROOT / "components/web_server/inverter_config_api.c").read_text(encoding="utf-8")
PROFILE_API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
PROFILE_GUARD = (ROOT / "components/web_server/inverter_profile_store_guard.c").read_text(encoding="utf-8")
IMPORT_GUARD = (ROOT / "components/web_server/config_import_runtime_guard.c").read_text(encoding="utf-8")
WIFI_GUARD = (ROOT / "components/web_server/wifi_config_runtime_guard.c").read_text(encoding="utf-8")
SOURCE_API = (ROOT / "components/web_server/source_detection_api.c").read_text(encoding="utf-8")
SOURCE_GUARD = (ROOT / "components/web_server/source_detection_runtime_guard.c").read_text(encoding="utf-8")
SOLAR_GRID = (ROOT / "components/web_server/solar_grid_api.c").read_text(encoding="utf-8")
WEB_API = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(text: str, earlier: str, later: str, message: str) -> None:
    require(earlier in text and later in text, message + " (token missing)")
    require(text.index(earlier) < text.index(later), message)


# Direct meter/inverter mapping writes build a disabled candidate, latch the live
# controller off, then persist the changed map.
for name, source in (("meter", METER), ("inverter", INVERTER)):
    require('#include "control_engine.h"' in source,
            f"{name} mapping API does not import the runtime interlock")
    require("control_engine_force_disable();" in source,
            f"{name} mapping API does not latch the running controller disabled")
    require_before(source, "control.enabled = false", "control_engine_force_disable();",
                   f"{name} mapping must first build a persistently disabled configuration")
    require_before(source, "control_engine_force_disable();", "config_manager_save(config);",
                   f"{name} mapping may persist before runtime command authority is removed")

# Generic /api/config import already refuses to persist control.enabled=true.
# Its HTTP path must also latch the running cached controller off before import.
require('"config_import_runtime_guard.c"' in CMAKE,
        "generic config runtime-disable guard is not compiled")
require("config_manager_import_json=web_config_manager_import_json_guarded" in CMAKE,
        "web config import is not routed through the runtime-disable guard")
require("config_manager_import_json(body);" in WEB_API,
        "web config import call shape changed; review runtime interlock routing")
require("control_engine_force_disable();" in IMPORT_GUARD,
        "generic config import guard does not latch the live controller disabled")
require_before(IMPORT_GUARD, "control_engine_force_disable();",
               "config_manager_import_json(json_text);",
               "generic config may persist before runtime command authority is removed")

# Wi-Fi changes can invalidate the transport used by meters/inverters. The
# source-local alias only affects the Wi-Fi save call in web_api.c.
require('"wifi_config_runtime_guard.c"' in CMAKE,
        "Wi-Fi runtime-disable guard is not compiled")
require("config_manager_save=web_wifi_config_manager_save_guarded" in CMAKE,
        "Wi-Fi save is not routed through the source-local runtime-disable guard")
require("config->wifi = next;" in WEB_API and "config_manager_save(config);" in WEB_API,
        "Wi-Fi config call shape changed; review guarded persistence")
for token in ("guarded.control.enabled = false;", "control_engine_force_disable();",
              "config_manager_save(&guarded);"):
    require(token in WIFI_GUARD, f"Wi-Fi runtime-disable guard missing: {token}")
require_before(WIFI_GUARD, "guarded.control.enabled = false;",
               "control_engine_force_disable();",
               "Wi-Fi guard must build a persistently disabled candidate first")
require_before(WIFI_GUARD, "control_engine_force_disable();",
               "config_manager_save(&guarded);",
               "Wi-Fi configuration may persist before live command authority is removed")

# Profile assignment uses a source-local guarded bridge to avoid a component cycle.
require('"inverter_profile_store_guard.c"' in CMAKE,
        "profile runtime-disable guard is not compiled")
require("inverter_profile_store_set=inverter_profile_store_set_guarded" in CMAKE,
        "profile assignment is not routed through the runtime-disable guard")
require("control_engine_force_disable();" in PROFILE_GUARD,
        "profile assignment guard does not latch the live controller disabled")
require_before(PROFILE_GUARD, "control_engine_force_disable();",
               "inverter_profile_store_set(inverter_index, profile_id);",
               "profile assignment may persist before runtime command authority is removed")
require("inverter_profile_store_set(inverter_index, profile->id);" in PROFILE_API,
        "profile API shape changed; review guarded assignment contract")

# Source-detection topology/register/threshold changes alter source evidence that
# the control engine consumes when strong commissioned contacts are unavailable.
require('"source_detection_runtime_guard.c"' in CMAKE,
        "source-detection runtime-disable guard is not compiled")
require("source_detection_config_save=web_source_detection_config_save_guarded" in CMAKE,
        "source-detection save is not routed through the runtime-disable guard")
require("source_detection_config_save(&config);" in SOURCE_API,
        "source-detection API shape changed; review guarded persistence")
for token in ("application.control.enabled = false;", "config_manager_save(&application)",
              "control_engine_force_disable();", "source_detection_config_save(source_config);"):
    require(token in SOURCE_GUARD, f"source-detection runtime-disable guard missing: {token}")
require_before(SOURCE_GUARD, "application.control.enabled = false;",
               "config_manager_save(&application)",
               "source-detection guard must persist the fail-closed app state first")
require_before(SOURCE_GUARD, "config_manager_save(&application)",
               "control_engine_force_disable();",
               "source-detection guard may revoke only RAM state without persistent disable")
require_before(SOURCE_GUARD, "control_engine_force_disable();",
               "source_detection_config_save(source_config);",
               "source-detection model may change before runtime command authority is removed")

# Solar-Grid commissioning already owns an explicit in-handler interlock because
# it persists two stores. Keep that path represented in the consolidated inventory.
require("application->control.enabled = false" in SOLAR_GRID,
        "Solar-Grid configuration does not build a persistently disabled app config")
require("control_engine_force_disable();" in SOLAR_GRID,
        "Solar-Grid configuration does not latch the live controller disabled")
require_before(SOLAR_GRID, "application->control.enabled = false",
               "solar_grid_config_save(&next)",
               "Solar-Grid source model may persist before persistent control disable")
require_before(SOLAR_GRID, "control_engine_force_disable();",
               "solar_grid_config_save(&next)",
               "Solar-Grid source model may persist before runtime command authority is removed")

# HTTP-side guards must remain non-blocking: the control task owns the physical
# zero write after the disable latch is set.
for name, source in (("import", IMPORT_GUARD), ("wifi", WIFI_GUARD),
                     ("profile", PROFILE_GUARD), ("source-detection", SOURCE_GUARD)):
    for forbidden in ("inverter_manager_set_total_power_kw", "meter_manager_read_registers",
                      "modbus_tcp_", "vTaskDelay"):
        require(forbidden not in source,
                f"{name} HTTP-side interlock gained blocking/physical I/O: {forbidden}")

# Runtime disable must request a confirmed safe-zero transition on the control task.
for token in ("s_runtime_forced_disabled = true", "s_safe_zero_pending = true",
              "inverter_manager_set_total_power_kw(0.0f)"):
    require(token in CONTROL, f"runtime disable safe-zero mechanism missing: {token}")

print("commissioning mutation runtime-disable inventory passed")
