#!/usr/bin/env python3
"""Profile assignment must preserve disable ordering without a whole app config on task stack."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STORE = (ROOT / "components/inverter_manager/inverter_profile_store.c").read_text(encoding="utf-8")
WEB_GUARD = (ROOT / "components/web_server/inverter_profile_store_guard.c").read_text(encoding="utf-8")
WEB_CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


setter = STORE.split("esp_err_t inverter_profile_store_set", 1)[1]

# app_config_t is ~2.5 kB and this setter is normally reached from the shared
# HTTP task. The snapshot must live on the heap and all exits must release it.
require("app_config_t config;" not in setter,
        "profile assignment still places the full app configuration on task stack")
for token in (
    "app_config_t *config = malloc(sizeof(*config))",
    "if (!config) return ESP_ERR_NO_MEM",
    "config_manager_get_snapshot(config)",
    "free(config)",
):
    require(token in setter, f"heap-safe profile configuration snapshot missing: {token}")
require(setter.count("free(config);") >= 2,
        "profile assignment must release its config snapshot on failure and success paths")

# Persisted automatic control must still be disabled before the new profile map
# is committed; a stack-headroom fix must not weaken the established safety order.
for token in (
    "if (config->control.enabled)",
    "config->control.enabled = false",
    "config_manager_save(config)",
    "persist(&next)",
):
    require(token in setter, f"profile assignment safety ordering missing: {token}")
require(setter.index("config->control.enabled = false") < setter.index("config_manager_save(config)"),
        "control flag must be cleared before disabled configuration is saved")
require(setter.index("config_manager_save(config)") < setter.index("persist(&next)"),
        "profile map must not persist before automatic control is persistently disabled")

# The web bridge still removes live command authority before entering this
# persistence path, so heap conversion cannot become a runtime-interlock bypass.
require("control_engine_force_disable();" in WEB_GUARD,
        "profile web guard no longer force-disables live command authority")
require("inverter_profile_store_set(inverter_index, profile_id);" in WEB_GUARD,
        "profile web guard no longer delegates to the persistent setter")
require("inverter_profile_store_set=inverter_profile_store_set_guarded" in WEB_CMAKE,
        "profile HTTP API is no longer routed through the runtime-disable guard")

print("heap-safe inverter profile assignment and disable ordering contract passed")
