#!/usr/bin/env python3
"""Public schema-6 config paths must not place app_config_t on task stacks."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SAFE = (ROOT / "components/config_manager/config_manager_stack_safe.c").read_text(encoding="utf-8")
LEGACY = (ROOT / "components/config_manager/config_manager_v6.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/config_manager/CMakeLists.txt").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# The old implementation remains available only under internal compatibility
# names; the heap-backed adapter owns the public symbols consumed by app_main and
# the shared HTTP server task.
for token in (
    '"config_manager_stack_safe.c"',
    "config_manager_init=config_manager_init_v6_stack_legacy",
    "config_manager_import_json=config_manager_import_json_v6_stack_legacy",
    "config_manager_export_json=config_manager_export_json_v6_stack_legacy",
):
    require(token in CMAKE, f"schema-6 stack-safe routing missing: {token}")

for signature in (
    "esp_err_t config_manager_init(void)",
    "esp_err_t config_manager_import_json(const char *json_text)",
    "esp_err_t config_manager_export_json(char **out_json)",
):
    require(signature in SAFE, f"public heap-backed entry point missing: {signature}")

# A direct whole app_config_t automatic variable is the exact defect class that
# previously overflowed this project's main task. The public adapter must use
# pointers/heap storage only.
stack_decl = re.compile(r"\bapp_config_t\s+[A-Za-z_]\w*\s*;")
require(not stack_decl.search(SAFE),
        "public schema-6 adapter reintroduced a whole app_config_t stack frame")
require(SAFE.count("malloc(sizeof(*snapshot))") >= 3,
        "init/import/export must use heap-backed configuration snapshots")
require("app_config_t *migrated = (app_config_t *)blob" in SAFE,
        "schema-5 migration should reuse the already heap-backed NVS blob")

# Import must reuse one allocation before/after the core import rather than hold
# two full snapshots simultaneously, and every error/success branch must release it.
import_body = SAFE.split("esp_err_t config_manager_import_json", 1)[1].split(
    "static void add_mode_to_array", 1
)[0]
require(import_body.count("malloc(sizeof(*snapshot))") == 1,
        "config import must reuse exactly one full configuration allocation")
require(import_body.count("free(snapshot);") >= 5,
        "config import must free its heap snapshot on every exit class")
require("before_meter_count" in import_body and "before_inverter_count" in import_body,
        "config import must preserve pre-import counts before reusing its snapshot")

export_body = SAFE.split("esp_err_t config_manager_export_json", 1)[1]
require("free(snapshot);" in export_body,
        "config export must release its heap snapshot")
require("config_manager_guarded_cjson_parse(core_json)" in export_body,
        "stack-safe export must retain the bounded JSON parser")

# The established schema-6 implementation is intentionally still compiled under
# aliases so its save and destructive-erase interception behavior stays intact.
require("esp_err_t config_manager_save(const app_config_t *config)" in LEGACY,
        "schema-6 save validation must remain in the established wrapper")
require("config_manager_forbidden_nvs_erase" in LEGACY,
        "destructive NVS erase interception must remain intact")

print("schema-6 public config stack-headroom contract passed")
