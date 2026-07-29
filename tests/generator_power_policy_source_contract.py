#!/usr/bin/env python3
"""Phase 3: the power-following generator policy must never claim breaker or
synchronisation knowledge, and must hold PV off when the machine is unknown."""

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
        "measurement cannot prove two sources are synchronised; the power-following mapping "
        "must never return GRID_GENERATOR_SYNC")
require("SOURCE_MODE_TRANSFER" not in mapping,
        "measurement cannot prove a transfer is in progress")
require("SOURCE_MODE_ISLAND" not in mapping, "measurement cannot prove islanding")
require("if (!evidence_fresh) return result;" in mapping,
        "stale evidence must fail closed before any source is considered")

# The control engine must not fabricate breaker or synchronisation evidence.
require(".generator_breaker_closed = false," in CONTROL,
        "generator breaker evidence must stay unset while no genset controller is integrated")
require(".grid_generator_synchronized = false," in CONTROL,
        "synchronisation evidence must stay unset while no genset controller is integrated")
require("source_mode_from_measured_source" in CONTROL,
        "the control engine must use the measured-source path when no evidence is configured")
require("transition_pending" in CONTROL,
        "a pending source transition must not be treated as a settled source")

# Generator limits are configuration, and uncommissioned means PV off.
for field in ("generator_rated_kw", "generator_minimum_loading_percent",
              "generator_reserve_kw", "generator_reverse_power_margin_kw"):
    require(field in SG_H, f"generator limit configuration missing {field}")
require("SOLAR_GRID_CONFIG_VERSION 2u" in SG_H, "generator limits require schema 2")
require("legacy_solar_grid_config_v1_t" in SG_C,
        "schema 1 layout must be frozen so a commissioned policy is upgraded, not discarded")
require("_Static_assert(sizeof(solar_grid_config_t) ==" in SG_C,
        "schema 1 must be proven a byte-exact prefix of schema 2")
require("Migrated Solar-Grid configuration schema 1" in SG_C,
        "schema 1 must be migrated rather than replaced by defaults")

limit = CONTROL[CONTROL.index("float generator_safe_limit_kw = 0.0f;"):]
limit = limit[:limit.index("power_control_input_t input")]
require("SOURCE_MODE_GENERATOR_ONLY" in limit,
        "the generator limit must only apply while a generator carries the plant")
require("s_grid_config.generator_rated_kw" in limit,
        "the generator limit must use the commissioned rating, never an assumed one")
require("source_mode_generator_safe_pv_kw" in limit,
        "the generator limit must come from the tested policy function")

# The policy function itself must fail closed on an uncommissioned rating.
safe = MODE_C[MODE_C.index("float source_mode_generator_safe_pv_kw"):]
safe = safe[:safe.index("\n}")]
require("running_generator_rated_kw <= 0.0f" in safe,
        "a zero or negative generator rating must yield zero PV, not an unlimited machine")
require("isfinite" in safe, "non-finite generator inputs must fail closed")

print("generator power policy source contract passed")
