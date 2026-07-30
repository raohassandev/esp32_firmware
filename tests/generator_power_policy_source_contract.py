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
require("SOLAR_GRID_CONFIG_VERSION 3u" in SG_H,
        "per-engine generator limits require schema 3")
require("legacy_solar_grid_config_v1_t" in SG_C,
        "schema 1 layout must be frozen so a commissioned policy is upgraded, not discarded")
require("legacy_solar_grid_config_v2_t" in SG_C,
        "schema 2 layout must be frozen so a commissioned single-generator rating is "
        "upgraded, not discarded")
# Stated with offsetof rather than a hand-computed size sum: the appended block can
# be preceded by alignment padding, and an arithmetic assertion would then be wrong
# in exactly the way that drops a commissioned unit back to defaults.
require("_Static_assert(offsetof(legacy_solar_grid_config_v2_t, generator_rated_kw) ==" in SG_C,
        "schema 1 must be proven a byte-exact prefix of schema 2")
require("_Static_assert(offsetof(solar_grid_config_t, generator_extra) ==" in SG_C,
        "schema 2 must be proven a byte-exact prefix of schema 3")
require("sizeof(solar_grid_config_t) > sizeof(legacy_solar_grid_config_v2_t)" in SG_C,
        "schema 3 must stay distinguishable from schema 2 by blob size")
for schema in (1, 2):
    require(f"Migrated Solar-Grid configuration schema {schema}" in SG_C,
            f"schema {schema} must be migrated rather than replaced by defaults")

# Per-engine limits, read through one uniform accessor so no caller has to know
# where a given slot is stored.
require("SOLAR_GRID_MAX_GENERATORS" in SG_H,
        "the number of engine slots must be declared")
require("solar_grid_generator_limits_t" in SG_H,
        "per-engine generator limits must be a type, not four parallel arrays")
require("solar_grid_config_generator(" in SG_C,
        "engine slots must be readable through one uniform accessor")

# ------------------------------------------------------- the aggregate limit
# Which engines are running is a RUNTIME fact. The floor must be computed against
# the aggregate rating of the engines actually online, and the decision must live
# in a pure, host-testable function rather than inside the control loop.
FLEET_H = (ROOT / "components/control_engine/include/generator_fleet_limit.h").read_text(encoding="utf-8")
FLEET_C = (ROOT / "components/control_engine/generator_fleet_limit.c").read_text(encoding="utf-8")
CONTROL_CMAKE = (ROOT / "components/control_engine/CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

require('"generator_fleet_limit.c"' in CONTROL_CMAKE,
        "the aggregate generator limit must be built")
for forbidden in ("esp_", "portENTER_CRITICAL", "malloc(", "cJSON", "ESP_LOG",
                  "#include \"freertos", "meter_manager_", "vTaskDelay"):
    require(forbidden not in FLEET_C and forbidden not in FLEET_H,
            f"the aggregate generator limit must stay pure; found '{forbidden}'")
require("source_mode_generator_safe_pv_kw" in FLEET_C,
        "the aggregate limit must come from the tested policy function rather than "
        "reimplementing the safe-PV arithmetic")
require("GENERATOR_FLEET_RUNNING_SET_UNKNOWN" in FLEET_H,
        "an undeterminable running set must have its own reason code")
require("safe_pv_kw = 0.0f" in FLEET_C,
        "an unknown running set must yield zero PV, which is the only conservative "
        "reading: any other denominator is a guess")

limit = CONTROL[CONTROL.index("float generator_safe_limit_kw = 0.0f;"):]
limit = limit[:limit.index("power_control_input_t input")]
require("SOURCE_MODE_GENERATOR_ONLY" in limit,
        "the generator limit must only apply while a generator carries the plant")
require("solar_grid_config_generator(&s_grid_config" in limit,
        "the generator limit must use the commissioned per-engine ratings, never an "
        "assumed one")
require("generator_fleet_limit_evaluate" in limit,
        "the generator limit must come from the tested aggregate policy function")
require("meter_manager_get_data" in limit,
        "which engines are online must be read from the generator-role meters, not "
        "taken from configuration")
require("meter_sample_fresh" in limit,
        "only a fresh, online, non-degraded generator sample may establish an engine "
        "as being on the bus")
require("fleet_limit.known ? fleet_limit.safe_pv_kw : 0.0f" in limit,
        "an unknown running set must hold PV at zero rather than fall through to a "
        "permissive limit")
# No blocking I/O may enter the 20 ms loop.
for forbidden in ("meter_manager_read_registers", "vTaskDelay", "nvs_",
                  "config_manager_get_snapshot", "solar_grid_config_get_snapshot"):
    require(forbidden not in limit,
            f"the generator limit must not add blocking I/O to the control loop: {forbidden}")

# The policy function itself must fail closed on an uncommissioned rating.
safe = MODE_C[MODE_C.index("float source_mode_generator_safe_pv_kw"):]
safe = safe[:safe.index("\n}")]
require("running_generator_rated_kw <= 0.0f" in safe,
        "a zero or negative generator rating must yield zero PV, not an unlimited machine")
require("isfinite" in safe, "non-finite generator inputs must fail closed")

# Both host-compiled tests have to run in CI to be worth anything.
require("tests/generator_fleet_limit_test.c" in WORKFLOW,
        "the aggregate generator limit unit test must be registered in the CI workflow")

print("generator power policy source contract passed")
