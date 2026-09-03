#!/usr/bin/env python3
"""Commissioning mapping writes must disable the already-running controller before persistence."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
METER = (ROOT / "components/web_server/meter_config_api.c").read_text(encoding="utf-8")
INVERTER = (ROOT / "components/web_server/inverter_config_api.c").read_text(encoding="utf-8")
PROFILE_API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
PROFILE_GUARD = (ROOT / "components/web_server/inverter_profile_store_guard.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_before(text: str, earlier: str, later: str, message: str) -> None:
    require(earlier in text and later in text, message + " (token missing)")
    require(text.index(earlier) < text.index(later), message)


for name, source in (("meter", METER), ("inverter", INVERTER)):
    require('#include "control_engine.h"' in source,
            f"{name} mapping API does not import the runtime interlock")
    require("control_engine_force_disable();" in source,
            f"{name} mapping API does not latch the running controller disabled")
    require_before(source, "control_engine_force_disable();", "config_manager_save(config);",
                   f"{name} mapping may persist before runtime command authority is removed")
    require_before(source, "control.enabled = false", "control_engine_force_disable();",
                   f"{name} mapping must first build a persistently disabled configuration")

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

# Runtime disable must request a confirmed safe-zero transition on the control task.
for token in ("s_runtime_forced_disabled = true", "s_safe_zero_pending = true",
              "inverter_manager_set_total_power_kw(0.0f)"):
    require(token in CONTROL, f"runtime disable safe-zero mechanism missing: {token}")

print("runtime configuration disable interlock contract passed")
