#!/usr/bin/env python3
"""Generator policy must use commissioned evidence, never invent breaker/sync state."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODE_H = (ROOT / "components/control_engine/include/source_mode.h").read_text(encoding="utf-8")
MODE_C = (ROOT / "components/control_engine/source_mode.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
SG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
SG_C = (ROOT / "components/solar_grid_config/solar_grid_config.c").read_text(encoding="utf-8")


def require(condition, message):
    assert condition, message


# Measurement establishes which source carries load - nothing stronger.
require("measured_source_t" in MODE_H, "a measured source identity type must exist")
for token in ("MEASURED_SOURCE_UNKNOWN", "MEASURED_SOURCE_GRID", "MEASURED_SOURCE_GENERATOR"):
    require(token in MODE_H, f"measured source vocabulary missing {token}")
require("source_mode_from_measured_source" in MODE_H and
        "source_mode_from_measured_source" in MODE_C,
        "the power-following mapping must exist")

mapping = MODE_C[MODE_C.index("source_mode_result_t source_mode_from_measured_source"):]
require("SOURCE_MODE_GRID_GENERATOR_SYNC" not in mapping,
        "measurement cannot prove two sources are synchronised")
require("SOURCE_MODE_TRANSFER" not in mapping,
        "measurement cannot prove a transfer is in progress")
require("SOURCE_MODE_ISLAND" not in mapping,
        "measurement cannot prove islanding")
require("if (!evidence_fresh) return result;" in mapping,
        "stale measurement evidence must fail closed")

# Strong evidence must now come from persisted signals and actual reads. The
# measured-source fallback remains only for sites without commissioned grid
# contact evidence and may not populate breaker/synchronism fields itself.
for field in ("generator_running", "generator_breaker_closed", "transfer_active",
              "grid_generator_synchronized"):
    require(field in SG_H, f"strong source evidence field missing: {field}")
    require(f"evidence.{field}" in CONTROL,
            f"control engine does not consume runtime strong evidence: {field}")
require("read_optional_signal" in CONTROL,
        "strong evidence must be acquired through the bounded evidence task")
require("source_mode_from_measured_source" in CONTROL,
        "the measured-source fallback must remain available")
require("if (evidence.configured)" in CONTROL,
        "strong evidence must have an explicit configured branch")
require("transition_pending" in CONTROL,
        "a pending measured-source transition must not be treated as settled")

# Persisted schema 3 must migrate schemas 1 and 2 without guessing new signals.
for field in ("generator_rated_kw", "generator_minimum_loading_percent",
              "generator_reserve_kw", "generator_reverse_power_margin_kw"):
    require(field in SG_H, f"generator limit configuration missing {field}")
require("SOLAR_GRID_CONFIG_VERSION 3u" in SG_H,
        "strong source evidence requires Solar-Grid schema 3")
for legacy in ("legacy_solar_grid_config_v1_t", "legacy_solar_grid_config_v2_t"):
    require(legacy in SG_C, f"frozen migration layout missing: {legacy}")
require("offsetof(solar_grid_config_t, generator_running)" in SG_C,
        "schema 2 must be proven an exact prefix of schema 3")
require("Migrated Solar-Grid configuration schema 2" in SG_C,
        "schema 2 must migrate rather than fall back to defaults")
require("signal_safe_defaults(&loaded->generator_running)" in SG_C,
        "migration must leave generator evidence disabled instead of guessing it")

limit = CONTROL[CONTROL.index("float generator_safe_limit_kw = 0.0f;"):]
limit = limit[:limit.index("power_control_input_t input")]
require("SOURCE_MODE_GENERATOR_ONLY" in limit and "SOURCE_MODE_ISLAND" in limit,
        "generator minimum-load protection must cover generator-only and island operation")
require("s_grid_config.generator_rated_kw" in limit,
        "the generator limit must use the commissioned rating")
require("source_mode_generator_safe_pv_kw" in limit,
        "the generator limit must come from the tested policy function")
require("evidence.generator_configured" in limit,
        "strong generator operation must fail closed until run/breaker evidence is commissioned")

safe = MODE_C[MODE_C.index("float source_mode_generator_safe_pv_kw"):]
safe = safe[:safe.index("\n}")]
require("running_generator_rated_kw <= 0.0f" in safe,
        "a zero or negative generator rating must yield zero PV")
require("isfinite" in safe, "non-finite generator inputs must fail closed")

print("generator power policy source contract passed")
