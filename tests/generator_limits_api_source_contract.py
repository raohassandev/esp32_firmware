#!/usr/bin/env python3
"""Persisted generator limits must remain commissionable over the HTTP API.

Schema 3 adds strong source evidence after the original schema-2 generator
limits. The four limits remain readable, writable, bounded and fail-closed; a
zero generator rating is still the safe uncommissioned state.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/solar_grid_api.c").read_text(encoding="utf-8")
SG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
SG_C = (ROOT / "components/solar_grid_config/solar_grid_config.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

KW_FIELDS = (
    "generator_rated_kw",
    "generator_reserve_kw",
    "generator_reverse_power_margin_kw",
)
FIELDS = KW_FIELDS + ("generator_minimum_loading_percent",)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def section(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


for field in FIELDS:
    require(field in SG_H, f"persisted generator limit missing from schema: {field}")
_schema = re.search(r"#define SOLAR_GRID_CONFIG_VERSION (\d+)u", SG_H)
require(_schema and int(_schema.group(1)) >= 2,
        "generator limits require Solar-Grid schema 2 or later")

config_json = section(API, "static cJSON *config_json(", "static bool read_bool(")
for field in FIELDS:
    require(f'cJSON_AddNumberToObject(root, "{field}"' in config_json,
            f"GET /api/solar-grid/config must serialise {field}")

config_post = section(API, "static esp_err_t config_post(", "esp_err_t solar_grid_api_register(")
for field in FIELDS:
    require(f'read_limit(root, "{field}"' in config_post,
            f"POST /api/solar-grid/config must accept {field}")

require("#define SOLAR_GRID_KW_MAX 1000000.0f" in API,
        "the kW upper bound must mirror solar_grid_config_valid()")
require("#define SOLAR_GRID_PERCENT_MAX 100.0f" in API,
        "the loading-percent upper bound must mirror solar_grid_config_valid()")
for field in KW_FIELDS:
    limit_call = config_post[config_post.index(f'read_limit(root, "{field}"'):]
    require("SOLAR_GRID_KW_MAX" in limit_call[:220],
            f"{field} must be bounded by the shared 1000000 kW ceiling")
percent_call = config_post[config_post.index('read_limit(root, "generator_minimum_loading_percent"'):]
require("SOLAR_GRID_PERCENT_MAX" in percent_call[:220],
        "generator_minimum_loading_percent must be bounded at 100")

read_limit = section(API, "static bool read_limit(", "static bool parse_signal(")
require("candidate < 0.0f" in read_limit,
        "negative generator limits must be rejected while zero remains accepted")
require("candidate > maximum" in read_limit,
        "generator limits must be rejected above their ceiling")
require("read_float(object, key, &candidate)" in read_limit,
        "generator limits must go through the finite-number reader")
require("Solar-Grid field '%s'" in read_limit,
        "a rejected generator limit must name the offending field")
require("if (!cJSON_GetObjectItemCaseSensitive(object, key)) return true;" in read_limit,
        "an absent key must leave the stored value untouched")

require("<= 0.0f" not in read_limit,
        "zero must be accepted as the uncommissioned generator rating")
for rejection in ("generator_rated_kw <= 0.0f", "generator_rated_kw == 0",
                  "generator_rated_kw > 0.0f)) {"):
    require(rejection not in API,
            f"the API must not reject an uncommissioned generator rating: {rejection}")
assignments = re.findall(r"generator_\w+\s*=\s*([-\w.]+)", API)
require(all(value == "0.0f" for value in assignments),
        f"the API must not substitute a guessed generator rating: {assignments}")

require("config->generator_rated_kw < 0.0f" in SG_C,
        "solar_grid_config_valid() must reject only a negative rating")
require("generator_rated_kw <= 0.0f" not in SG_C,
        "solar_grid_config_valid() must not reject an uncommissioned rating")
require("solar_grid_config_valid(&next)" in config_post,
        "the POST handler must still defer to the full model validator")

require("application->control.enabled = false" in config_post,
        "Solar-Grid writes must persist automatic control disabled")
require("control.enabled = true" not in API,
        "the Solar-Grid write handler must never enable automatic control")
for key in ('"control"', '"control_enabled"', '"enable_control"'):
    require(key not in API,
            f"the Solar-Grid write handler must not accept a control-enable key: {key}")
require("control_engine_force_disable();" in config_post,
        "Solar-Grid writes must latch the running control task disabled")
require(config_post.index("application->control.enabled = false") <
        config_post.index("solar_grid_config_save(&next)"),
        "control must be disabled before the new source model is persisted")
require('cJSON_AddBoolToObject(response, "control_forced_disabled", true)' in config_post,
        "the save response must disclose the enforced disable")
require('cJSON_AddBoolToObject(response, "restart_required", true)' in config_post,
        "the save response must still require a restart before control is re-armed")

require("tests/generator_limits_api_source_contract.py" in WORKFLOW,
        "the generator limits API contract must be registered in CI")

print("generator limits API source contract passed")
