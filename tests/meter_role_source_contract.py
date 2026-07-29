#!/usr/bin/env python3
"""Meter roles must select the control input, and the schema-4 upgrade must not
cost a commissioned controller its configuration."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TYPES = (ROOT / "components/config_manager/include/config_types.h").read_text(encoding="utf-8")
CONFIG = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
METER_API = (ROOT / "components/web_server/meter_config_api.c").read_text(encoding="utf-8")


def require(condition, message):
    assert condition, message


# The role must exist as real configuration, not a display label.
require("APP_CONFIG_VERSION 4u" in TYPES, "meter roles require config schema 4")
for token in ("METER_ROLE_UNASSIGNED", "METER_ROLE_GRID", "METER_ROLE_GENERATOR",
              "METER_ROLE_LOAD", "METER_ROLE_PV", "METER_GENERATOR_INDEX_NONE"):
    require(token in TYPES, f"meter role vocabulary missing {token}")
require("uint8_t role;" in TYPES, "meter_config_t must carry a role")
require("uint8_t generator_index;" in TYPES, "meter_config_t must carry a generator slot")

# Older layouts must be frozen snapshots. If they referenced the live
# meter_config_t, appending a field would change their sizes, no stored blob
# would match any schema, and a commissioned unit would fall back to defaults
# and lose its Wi-Fi credentials.
require("legacy_meter_config_v3_t" in CONFIG, "schema 3 meter layout must be frozen")
for legacy in ("legacy_app_config_v1_t", "legacy_app_config_v2_t", "legacy_app_config_v3_t"):
    block = CONFIG[CONFIG.index(f"}} {legacy};") - 700:CONFIG.index(f"}} {legacy};")]
    require("legacy_meter_config_v3_t meters" in block,
            f"{legacy} must use the frozen meter layout, not the live one")
require("_Static_assert(sizeof(app_config_t) > sizeof(legacy_app_config_v3_t)" in CONFIG,
        "schema 4 must stay distinguishable from schema 3 by blob size")

# The upgrade must preserve, never reset.
require("Migrated configuration schema 3 to schema" in CONFIG, "schema 3 upgrade path missing")
require("loaded->wifi_provision_id = legacy->wifi_provision_id;" in CONFIG,
        "schema 3 upgrade must preserve the provisioning generation, not replay build credentials")
require("upgrade_meters_from_v3" in CONFIG, "meters must be converted field by field on upgrade")
require("nvs_erase" not in CONFIG.replace("nvs_erase_key_if_present", ""),
        "configuration upgrade must never erase NVS")

# An ambiguous assignment must fail closed, not discard the configuration.
require("config_manager_role_assignment" in CONFIG, "role resolution must exist")
assignment = CONFIG[CONFIG.index("meter_role_assignment_t config_manager_role_assignment"):]
assignment = assignment[:assignment.index("\n}")]
require("out.valid = out.grid_count == 1U" in assignment,
        "exactly one enabled grid meter is required for a valid assignment")
require("duplicate_generator" in assignment, "duplicate generator slots must be rejected")
valid_fn = CONFIG[CONFIG.index("static bool valid("):]
valid_fn = valid_fn[:valid_fn.index("\n}")]
require("role_assignment" not in valid_fn and "grid_count" not in valid_fn,
        "the grid-role rule must not be part of valid(): a failing configuration would be "
        "discarded and a commissioned controller would lose its Wi-Fi credentials")

# The control engine must select by role, never by array position.
require("meter_manager_get_data(0," not in CONTROL,
        "the control engine must not read the grid meter by hardcoded index 0")
require("roles.grid_index" in CONTROL, "the control engine must select the grid meter by role")
require("roles.valid &&" in CONTROL, "an invalid role assignment must yield no measurement")
require("_Static_assert(APP_MAX_GENERATORS == SOURCE_MAX_GENERATORS" in CONTROL,
        "generator slot counts must be checked against the source-mode channels")

# Role must be settable through the commissioning API.
require('"role"' in METER_API, "meter role must be configurable")
require('"generator_index"' in METER_API, "generator slot must be configurable")
require("METER_GENERATOR_INDEX_NONE" in METER_API,
        "a non-generator meter must clear its generator slot")

print("meter role and schema 4 upgrade contract passed")
