#!/usr/bin/env python3
"""Generator policy must use commissioned Generator 1..3 facts and fail closed."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODE_H = (ROOT / "components/control_engine/include/source_mode.h").read_text(encoding="utf-8")
MODE_C = (ROOT / "components/control_engine/source_mode.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
CONTROL_TYPES = (ROOT / "components/control_engine/include/control_types.h").read_text(encoding="utf-8")
POLICY = (ROOT / "components/control_engine/power_control_policy.c").read_text(encoding="utf-8")
STATUS = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
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
        "the measured-source fallback must remain available")
mapping = MODE_C[MODE_C.index("source_mode_result_t source_mode_from_measured_source"):]
for forbidden in ("SOURCE_MODE_GRID_GENERATOR_SYNC", "SOURCE_MODE_TRANSFER", "SOURCE_MODE_ISLAND"):
    require(forbidden not in mapping,
            f"measurement-only source mapping must never claim {forbidden}")
require("if (!evidence_fresh) return result;" in mapping,
        "stale measured-source evidence must fail closed")

# Schema 4 persists exactly three independent channels and migrates prior state
# into Generator 1 without inventing Generator 2/3 values.
require("SOLAR_GRID_CONFIG_VERSION 4u" in SG_H,
        "three-channel generator configuration requires Solar-Grid schema 4")
require("SOLAR_GRID_MAX_GENERATORS 3U" in SG_H and
        "solar_grid_generator_config_t generators[SOLAR_GRID_MAX_GENERATORS];" in SG_H,
        "Generator 1..3 must be persisted independently")
for legacy in ("legacy_solar_grid_config_v1_t", "legacy_solar_grid_config_v2_t",
               "legacy_solar_grid_config_v3_t"):
    require(legacy in SG_C, f"frozen migration layout missing: {legacy}")
require("migrate_v3" in SG_C and "legacy_to_generator0(loaded);" in SG_C,
        "schema 3 generator state must migrate to Generator 1")
require("generator_safe_defaults(&config->generators[i]);" in SG_C,
        "new generator channels must remain uncommissioned on migration/defaults")

# Per-channel contact freshness is separate from power freshness. A stopped/open
# machine may have stale power, but a connected running machine may not.
require("bool measurement_fresh;" in MODE_H,
        "generator channels need independent power freshness")
fleet = MODE_C[MODE_C.index("generator_fleet_result_t source_mode_aggregate_generators"):]
fleet = fleet[:fleet.index("float source_mode_generator_fleet_safe_pv_kw")]
for token in (
    "!channel->evidence_fresh",
    "channel->breaker_closed && !channel->running",
    "if (!channel->breaker_closed) continue;",
    "!channel->measurement_fresh",
    "channel->measured_kw < 0.0f",
):
    require(token in fleet, f"per-generator fleet guard missing: {token}")
require("fabs" not in fleet,
        "generator meter sign must be commissioned, never hidden with fabs()")

# Runtime must map Generator 1..3 meter roles, per-channel run/breaker contacts,
# and per-channel limits into the tested aggregate policy.
for token in (
    "build_generator_fleet",
    "roles->generator_index[i]",
    "s_grid_config.generators[i]",
    "solar_grid_config_generator_evidence_complete_at",
    "evidence->generator_running_channel[i]",
    "evidence->generator_breaker_closed_channel[i]",
    "meter_manager_get_data(meter_index, &meter)",
    "source_mode_aggregate_generators(channels)",
    "source_mode_generator_fleet_safe_pv_kw",
):
    require(token in CONTROL, f"live Generator 1..3 integration missing: {token}")
require("generator_fleet.conflict" in CONTROL and "SOURCE_MODE_CONFLICT" in CONTROL,
        "per-channel conflict must override the lossy aggregate source verdict")
require("current_target_kw + generator_fleet.measured_total_kw" in CONTROL,
        "safe PV ceiling must be derived from current setpoint plus measured generator contribution")

# Generator-only/island does not regulate a nonexistent grid exchange. It ramps
# up toward the safe fleet ceiling and clamps downward immediately when safety
# headroom disappears. Synchronized operation still needs grid PI plus the fleet
# ceiling.
require("const bool generator_only" in POLICY,
        "power policy must distinguish generator-only/island from synchronized operation")
generator_path = POLICY[POLICY.index("if (generator_only) {"):POLICY.index("/* Grid-only and synchronized")]
for token in (
    "output.error_kw = 0.0f",
    "output.next_integral_kw = 0.0f",
    "float requested = maximum",
    "input->current_pv_command_kw + upward_step",
):
    require(token in generator_path, f"generator-only safe-ceiling behavior missing: {token}")
require("measured_grid_kw" not in generator_path,
        "generator-only control must not depend on a disconnected grid meter")
require("if (!isfinite(input->measured_grid_kw))" in POLICY,
        "grid/synchronized PI path must still require a finite grid measurement")

# The normal safety gate must use the running generator fleet measurement in
# generator-only/island mode instead of incorrectly requiring the grid meter.
require("const meter_data_t *safety_meter = generator_only && evidence.configured" in CONTROL and
        "&generator_safety_meter" in CONTROL,
        "generator-only safety freshness must come from generator meters")

# Runtime observability is required so commissioning can see exactly which fleet
# facts are driving the safe ceiling without causing Modbus I/O in the handler.
for field in (
    "generator_channel_configured_mask",
    "generator_channel_running_mask",
    "generator_channel_breaker_mask",
    "generator_running_count",
    "generator_running_rated_kw",
    "generator_measured_total_kw",
    "generator_required_minimum_kw",
    "generator_safe_pv_limit_kw",
):
    require(field in CONTROL_TYPES, f"runtime status field missing: {field}")
    require(f'"{field}"' in STATUS, f"status API missing generator fleet field: {field}")
require("meter_manager_read_registers" not in STATUS,
        "status HTTP handler must remain cache-only")

# Legacy measured-source fallback remains fail-closed for sites without strong
# contact commissioning; it may not claim breaker/synchronism facts.
require("source_mode_generator_safe_pv_kw" in CONTROL,
        "legacy single-generator fallback was accidentally removed")
require("transition_pending" in CONTROL,
        "pending measured-source transition must remain blocked")

print("generator power policy source contract passed")
