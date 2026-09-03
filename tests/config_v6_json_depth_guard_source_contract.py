#!/usr/bin/env python3
"""Schema-6 config parsing must not recurse through unbounded JSON before the core guard."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "components/config_manager/CMakeLists.txt").read_text(encoding="utf-8")
V6 = (ROOT / "components/config_manager/config_manager_v6.c").read_text(encoding="utf-8")
GUARD = (ROOT / "components/config_manager/config_manager_json_guard.c").read_text(encoding="utf-8")
HEADER = (ROOT / "components/config_manager/include/config_manager_json_guard.h").read_text(encoding="utf-8")
CORE = (ROOT / "components/config_manager/config_manager.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# The legacy core still owns its original independent depth check.
for token in (
    "CONFIG_JSON_MAX_DEPTH 16U",
    "json_depth_valid",
    "if (!text || !json_depth_valid(text))",
):
    require(token in CORE, f"core configuration depth guard missing: {token}")

# Schema-6 necessarily parses once before the core so it can preserve/extend
# connection-mode fields. That parse must be transparently routed through the
# bounded adapter; otherwise the wrapper reopens the recursive nesting hazard.
for token in (
    '"config_manager_json_guard.c"',
    'SOURCE "config_manager_v6.c"',
    "cJSON_Parse=config_manager_guarded_cjson_parse",
    'COMPILE_OPTIONS "-include;config_manager_json_guard.h"',
):
    require(token in CMAKE, f"schema-6 guarded parser integration missing: {token}")

require("cJSON_Parse(" in V6,
        "schema-6 parse shape changed; review bounded-parser routing")
require("config_manager_guarded_cjson_parse" in HEADER,
        "guarded parser is not declared for the schema-6 wrapper")

for token in (
    "CONFIG_MANAGER_JSON_MAX_DEPTH 16U",
    "bool in_string = false",
    "bool escaped = false",
    "ch == '\\\\'",
    "ch == '\"'",
    "ch == '{' || ch == '['",
    "depth > CONFIG_MANAGER_JSON_MAX_DEPTH",
    "ch == '}' || ch == ']'",
    "if (depth == 0U) return false",
    "return !in_string && !escaped && depth == 0U",
    "if (!json_depth_within_limit(text)) return NULL",
    "return cJSON_Parse(text)",
):
    require(token in GUARD, f"bounded config JSON parser behavior missing: {token}")

# The adapter itself must be the only new raw cJSON parse owner. The compile
# definition applies to config_manager_v6.c only, so this call remains the real
# library parser rather than recursively macro-expanding back into itself.
require(CMAKE.count("cJSON_Parse=config_manager_guarded_cjson_parse") == 1,
        "guarded cJSON macro must remain source-local to schema-6")

print("schema-6 configuration JSON depth guard contract passed")
