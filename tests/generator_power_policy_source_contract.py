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

# Strong evidence still comes only from persisted signals and actual reads. The
# current control engine consumes the compatibility Generator-1 mirror; schema 4
# additionally stores Generator 1..3 for the next runtime-integration slice.
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

# Persisted schema 4 must migrate schemas 1/2/3 without inventing new channels.
for field in ("generator_rated_kw", "generator_minimum_loading_percent",
              "generator_reserve_kw", "generator_reverse_power_margin_kw"):
    require(field in SG_H, f"Generator-1 compatibility limit missing {field}")
require("SOLAR_GRID_CONFIG_VERSION 4u" in SG_H,
        "three-channel generator configuration requires Solar-Grid schema 4")
require("solar_grid_generator_config_t generators[SOLAR_GRID_MAX_GENERATORS];" in SG_H,
        "schema 4 must persist three generator channels")
for legacy in ("legacy_solar_grid_config_v1_t", "legacy_solar_grid_config_v2_t",
               "legacy_solar_grid_config_v3_t"):
    require(legacy in SG_C, f"frozen migration layout missing: {legacy}")
require("offsetof(solar_grid_config_t, generators)" in SG_C,
        "schema 3 must be proven an exact prefix of schema 4")
require("migrate_v3" in SG_C and "legacy_to_generator0(loaded);" in SG_C,
        "schema 3 must migrate its generator state into Generator 1")
require("generator_safe_defaults(&config->generators[i]);" in SG_C,
        "Generator 2-3 must stay uncommissioned on migration/defaults")

# Existing single-machine runtime remains safe while the independent runtime
# integration slice is developed: generator-only/island both use the tested
# safe-limit function and require commissioned evidence.
limit = CONTROL[CONTROL.index("float generator_safe_limit_kw = 0.0f;"):]
limit = limit[:limit.index("power_control_input_t input")]
require("SOURCE_MODE_GENERATOR_ONLY" in limit and "SOURCE_MODE_ISLAND" in limit,
        "generator minimum-load protection must cover generator-only and island operation")
require("s_grid_config.generator_rated_kw" in limit,
        "current Generator-1 runtime must use the commissioned compatibility rating")
require("source_mode_generator_safe_pv_kw" in limit,
        "the current generator limit must come from the tested policy function")
require("evidence.generator_configured" in limit,
        "strong generator operation must fail closed until run/breaker evidence is commissioned")

# Fleet aggregation groundwork must itself be fail-closed before it is wired into
# control_engine: per-channel non-finite/stale/conflicting evidence invalidates the
# whole fleet and the fleet-safe helper can only subtract a validated aggregate
# required minimum from a finite facility load.
require("source_mode_aggregate_generators" in MODE_H and
        "source_mode_aggregate_generators" in MODE_C,
        "three-channel fleet aggregation policy must exist")
require("source_mode_generator_fleet_safe_pv_kw" in MODE_H and
        "source_mode_generator_fleet_safe_pv_kw" in MODE_C,
        "fleet safe-PV helper must exist")
fleet = MODE_C[MODE_C.index("generator_fleet_result_t source_mode_aggregate_generators"):]
fleet = fleet[:fleet.index("float source_mode_generator_fleet_safe_pv_kw")]
for token in ("!channel->evidence_fresh", "channel->rated_kw <= 0.0f",
              "!isfinite(channel->measured_kw)",
              "channel->breaker_closed && !channel->running"):
    require(token in fleet, f"fleet aggregation fail-closed guard missing: {token}")

fleet_safe = MODE_C[MODE_C.index("float source_mode_generator_fleet_safe_pv_kw"):]
fleet_safe = fleet_safe[:fleet_safe.index("float source_mode_generator_safe_pv_kw")]
for token in ("!fleet->valid", "fleet->conflict", "fleet->running_count == 0U",
              "!isfinite(facility_load_kw)", "facility_load_kw - fleet->required_minimum_kw"):
    require(token in fleet_safe, f"fleet safe-PV guard/calculation missing: {token}")

safe = MODE_C[MODE_C.index("float source_mode_generator_safe_pv_kw"):]
safe = safe[:safe.index("\n}")]
require("running_generator_rated_kw <= 0.0f" in safe,
        "a zero or negative generator rating must yield zero PV")
require("isfinite" in safe, "non-finite generator inputs must fail closed")

print("generator power policy source contract passed")
