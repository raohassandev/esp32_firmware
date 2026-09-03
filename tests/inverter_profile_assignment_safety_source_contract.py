#!/usr/bin/env python3
"""Profile changes must disable running and persisted control before map changes."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STORE = (ROOT / "components/inverter_manager/inverter_profile_store.c").read_text(encoding="utf-8")
GUARD = (ROOT / "components/web_server/inverter_profile_store_guard.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


set_start = STORE.index("esp_err_t inverter_profile_store_set")
set_body = STORE[set_start:]
for token in (
    "config_manager_get_snapshot(&config)",
    "config.control.enabled = false",
    "config_manager_save(&config)",
    "persist(&next)",
):
    require(token in set_body, f"profile persistence safety step missing: {token}")

require(set_body.index("config_manager_get_snapshot(&config)") < set_body.index("persist(&next)"),
        "configuration must be inspected before profile persistence")
require(set_body.index("config.control.enabled = false") < set_body.index("persist(&next)"),
        "automatic control must be disabled before profile persistence")
require(set_body.index("config_manager_save(&config)") < set_body.index("persist(&next)"),
        "disabled control must be committed before profile persistence")
require(set_body.index("persist(&next)") < set_body.index("s_store = next"),
        "in-memory assignment must not advance before NVS commit")

for token in (
    "control_engine_force_disable();",
    "return inverter_profile_store_set(inverter_index, profile_id);",
):
    require(token in GUARD, f"running-control profile guard missing: {token}")
require(GUARD.index("control_engine_force_disable();") <
        GUARD.index("inverter_profile_store_set(inverter_index, profile_id)"),
        "running control must be latched off before persistent profile work")

require('"inverter_profile_store_guard.c"' in CMAKE,
        "profile runtime-disable guard is not compiled")
require('inverter_profile_store_set=inverter_profile_store_set_guarded' in CMAKE,
        "profile assignment API is not routed through runtime-disable guard")
require("inverter_profile_store_set(inverter_index, profile->id)" in API,
        "expected profile assignment call disappeared; review guard integration")
require('"automatic_control_disabled", true' in API,
        "API must continue to report the postcondition it actually enforces")
require('"restart_required", true' in API,
        "profile-map changes must continue to require restart/reinitialization")

print("inverter profile assignment runtime/persistence safety contract passed")
