#!/usr/bin/env python3
"""Legacy config migration OOM must preserve commissioned NVS and stop startup."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "components/config_manager/CMakeLists.txt").read_text(encoding="utf-8")
WRAPPER = (ROOT / "components/config_manager/config_manager_v6.c").read_text(encoding="utf-8")
GUARD = (ROOT / "components/config_manager/config_manager_core_guard.c").read_text(encoding="utf-8")
CORE = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")
APP = (ROOT / "components/app_core/app_core.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for token in (
    '"config_manager_core_guard.c"',
    'malloc=config_manager_core_malloc',
    'nvs_set_blob=config_manager_guarded_nvs_set_blob',
):
    require(token in CMAKE, f"core OOM/NVS guard wiring missing: {token}")

for token in (
    "config_manager_core_allocation_guard_begin();",
    "config_manager_init_core();",
    "config_manager_core_allocation_guard_end();",
    "return ESP_ERR_NO_MEM;",
):
    require(token in WRAPPER, f"public init fail-closed guard missing: {token}")
require(WRAPPER.index("config_manager_core_allocation_guard_begin();") <
        WRAPPER.index("config_manager_init_core();") <
        WRAPPER.index("config_manager_core_allocation_guard_end();"),
        "allocation guard must surround the legacy core initialization call")

for token in (
    "s_init_guard_active",
    "s_core_allocation_failed",
    "void *config_manager_core_malloc(size_t size)",
    "if (!allocation && s_init_guard_active)",
    "esp_err_t config_manager_guarded_nvs_set_blob",
    "s_init_guard_active && s_core_allocation_failed",
    "return ESP_ERR_NO_MEM;",
    "return nvs_set_blob(handle, key, value, length);",
):
    require(token in GUARD, f"legacy migration preservation guard missing: {token}")

# Lock the original hazardous shape into the regression: legacy schema branches
# still allocate dynamically and the old core still falls through to defaults
# when no valid config was produced. The wrapper/guard must therefore remain in
# place unless that core logic itself is rewritten safely in the future.
require("legacy_app_config_v4_t *legacy = malloc(sizeof(*legacy));" in CORE,
        "schema-4 migration allocation shape changed; review this contract")
require("if (!have_valid_config) {" in CORE and "defaults(loaded);" in CORE,
        "legacy fallback shape changed; review OOM preservation contract")
require("ESP_RETURN_ON_ERROR(config_manager_init(), TAG, \"configuration init failed\");" in APP,
        "app_core must propagate config init failure and stop dependent startup")

print("configuration migration OOM preservation contract passed")
