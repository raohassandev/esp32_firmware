#!/usr/bin/env python3
"""Generator limits/evidence must remain bounded, persistent and fail-closed.

Schema 4 keeps the legacy Generator-1 fields as a compatibility mirror while
adding an authoritative three-channel `generators` array. No channel may gain a
rating, register address or polarity through migration/defaults.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "components/web_server/solar_grid_api.c").read_text(encoding="utf-8")
SG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
SG_C = (ROOT / "components/solar_grid_config/solar_grid_config.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

KW_FIELDS = ("rated_kw", "reserve_kw", "reverse_power_margin_kw")
CHANNEL_FIELDS = KW_FIELDS + ("minimum_loading_percent",)
LEGACY_FIELDS = (
    "generator_rated_kw",
    "generator_reserve_kw",
    "generator_reverse_power_margin_kw",
    "generator_minimum_loading_percent",
)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def section(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


schema = re.search(r"#define SOLAR_GRID_CONFIG_VERSION (\d+)u", SG_H)
require(schema and int(schema.group(1)) >= 4,
        "three generator channels require Solar-Grid schema 4 or later")
require("#define SOLAR_GRID_MAX_GENERATORS 3U" in SG_H,
        "the persisted Solar-Grid model must expose exactly three generator slots")
require("solar_grid_generator_config_t generators[SOLAR_GRID_MAX_GENERATORS];" in SG_H,
        "the persisted model must carry Generator 1..3 independently")
for field in CHANNEL_FIELDS:
    require(field in SG_H, f"per-generator field missing from schema: {field}")
for field in ("running", "breaker_closed"):
    require(field in SG_H, f"per-generator evidence field missing: {field}")

# Schema 3 is a frozen prefix and must migrate into slot 0, leaving slots 1/2
# untouched at safe defaults.
require("legacy_solar_grid_config_v3_t" in SG_C,
        "schema 3 must have a frozen migration layout")
require("offsetof(solar_grid_config_t, generators)" in SG_C and
        "sizeof(legacy_solar_grid_config_v3_t)" in SG_C,
        "schema 3 must be proven a byte-exact prefix of schema 4")
require("migrate_v3" in SG_C and "legacy_to_generator0(loaded);" in SG_C,
        "schema 3 generator state must migrate to Generator 1")
require("generator_safe_defaults(&config->generators[i]);" in SG_C,
        "new generator slots must start uncommissioned with disabled evidence")
require("signal_safe_defaults" in SG_C,
        "migration/defaults must use disabled safe signal values rather than guesses")

config_json = section(API, "static cJSON *config_json(", "static bool read_bool(")
require('cJSON_AddArrayToObject(root, "generators")' in config_json,
        "GET /api/solar-grid/config must expose the three-channel array")
require("for (uint8_t i = 0U; i < SOLAR_GRID_MAX_GENERATORS; ++i)" in config_json,
        "GET must serialize all configured generator slots")
for field in LEGACY_FIELDS:
    require(f'"{field}"' in config_json,
            f"Generator-1 compatibility field must remain readable: {field}")

config_post = section(API, "static esp_err_t config_post(", "esp_err_t solar_grid_api_register(")
require("parse_generators(root, &next" in config_post,
        "POST must accept the schema-4 generators array")
require("sync_generator0_compat(&next);" in config_post,
        "Generator-1 compatibility mirror must be normalized before validation/save")
for field in LEGACY_FIELDS:
    require(f'read_limit(root, "{field}"' in config_post,
            f"legacy Generator-1 POST field must remain accepted: {field}")

require("#define SOLAR_GRID_KW_MAX 1000000.0f" in API,
        "the kW upper bound must mirror solar_grid_config_valid()")
require("#define SOLAR_GRID_PERCENT_MAX 100.0f" in API,
        "the loading-percent upper bound must mirror solar_grid_config_valid()")
parse_generator = section(API, "static bool parse_generator(", "static bool parse_generators(")
for field in CHANNEL_FIELDS:
    require(f'"{field}"' in parse_generator,
            f"per-channel POST parser missing {field}")
require('parse_optional_signal(object, "running"' in parse_generator and
        'parse_optional_signal(object, "breaker_closed"' in parse_generator,
        "each generator channel must parse its own run/breaker evidence")

read_limit = section(API, "static bool read_limit(", "static bool parse_signal(")
require("candidate < 0.0f" in read_limit and "candidate > maximum" in read_limit,
        "limits must reject negative/out-of-range values")
require("read_float(object, key, &candidate)" in read_limit,
        "limits must pass through finite-number validation")
require("<= 0.0f" not in read_limit,
        "zero rated kW must remain the safe uncommissioned state")

# Full-model validation: all three channels are checked, run/breaker must be a
# pair, and zero rated kW is valid while negative/non-finite is rejected.
require("static bool generator_valid(" in SG_C,
        "per-generator model validation must exist")
require("generator->running.enabled == generator->breaker_closed.enabled" in SG_C,
        "run and breaker evidence must be commissioned as a pair")
require("rated_kw >= 0.0f" in SG_C and "rated_kw <= 1000000.0f" in SG_C,
        "zero must be valid while rated kW remains bounded")
require("for (uint8_t i = 0U; i < SOLAR_GRID_MAX_GENERATORS; ++i)" in SG_C and
        "generator_valid(&config->generators[i])" in SG_C,
        "all three channels must be validated before persistence")
require("solar_grid_config_valid(&next)" in config_post,
        "POST must defer to the full persisted-model validator")

# Any commissioning write must keep automatic control disabled until restart and
# a new evidence proof; the HTTP handler may not re-arm control itself.
require("application->control.enabled = false" in config_post,
        "Solar-Grid writes must persist automatic control disabled")
require("control_engine_force_disable();" in config_post,
        "Solar-Grid writes must latch the running control task disabled")
require(config_post.index("application->control.enabled = false") <
        config_post.index("solar_grid_config_save(&next)"),
        "control must be disabled before the new source model is persisted")
require('cJSON_AddBoolToObject(response, "control_forced_disabled", true)' in config_post,
        "save response must disclose the enforced disable")
require('cJSON_AddBoolToObject(response, "restart_required", true)' in config_post,
        "save response must require restart before re-arming")

require("tests/generator_limits_api_source_contract.py" in WORKFLOW,
        "the generator configuration API contract must remain registered in CI")

print("generator limits API source contract passed")
