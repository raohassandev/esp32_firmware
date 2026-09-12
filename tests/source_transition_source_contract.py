#!/usr/bin/env python3
"""A source change must never leave PV commanded on the outgoing source."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY = (ROOT / "components/control_engine/power_control_policy.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
DETECT = (ROOT / "components/source_detection/source_detection_engine.c").read_text(encoding="utf-8")
GATE_H = (ROOT / "components/control_engine/include/grid_control_gate.h").read_text(encoding="utf-8")
GATE = (ROOT / "components/control_engine/grid_control_gate.c").read_text(encoding="utf-8")
POLICY_TEST = (ROOT / "tests/power_control_policy_test.c").read_text(encoding="utf-8")
INTEGRATION_TEST = (ROOT / "tests/solar_grid_integration_test.c").read_text(encoding="utf-8")


def require(condition, message):
    assert condition, message


stable = POLICY[POLICY.index("static bool source_mode_is_stable"):]
stable = stable[:stable.index("\n}")]
for unsettled in ("SOURCE_MODE_UNKNOWN", "SOURCE_MODE_NO_SOURCE",
                  "SOURCE_MODE_TRANSFER", "SOURCE_MODE_CONFLICT"):
    require(unsettled not in stable,
            f"{unsettled} must never count as a stable source")

step = POLICY[POLICY.index("power_control_output_t power_control_step"):]
guard = step[:step.index("return")]
require("source_mode_is_stable(input->source_mode)" in guard,
        "source stability must remain in the early fail-closed guard")
require("if (!control_enabled || !policy.valid) {" in CONTROL,
        "invalid policy results must be handled in the same control cycle")
zeroing = CONTROL[CONTROL.index("if (!control_enabled || !policy.valid) {"):]
zeroing = zeroing[:zeroing.index("float applied_kw")]
require("policy.requested_pv_kw = 0.0f;" in zeroing,
        "an invalid source/policy result must zero the request immediately")
require("integral_kw = 0.0f;" in zeroing,
        "invalid source evidence must discard the previous PI integral")
require("else if (!previous_cycle_valid)" in CONTROL and
        "current_target_kw = 0.0f;" in CONTROL,
        "recovery must restart ramp state after an invalid cycle")

require("SOURCE_STATE_UNKNOWN" in DETECT, "source detection needs an unknown state")
require("DEBOUNCE_PENDING" in DETECT or "debounce" in DETECT.lower(),
        "source transitions must be debounced")
require("stable_state = SOURCE_STATE_UNKNOWN" in DETECT,
        "a candidate change must clear the outgoing settled state")

require("source_mode_t recovery_mode;" in GATE_H,
        "gate memory must remember which carrying mode earned the dwell")
for carrying in ("SOURCE_MODE_GRID_ONLY", "SOURCE_MODE_GENERATOR_ONLY",
                 "SOURCE_MODE_ISLAND", "SOURCE_MODE_GRID_GENERATOR_SYNC"):
    require(carrying in GATE, f"qualified carrying mode missing from source gate: {carrying}")
require("memory->recovery_mode != input->source_mode" in GATE,
        "a source-mode change must restart the stabilization dwell")
require("memory->recovery_mode = input->source_mode" in GATE,
        "the source gate must bind the dwell to the current carrying mode")
require("input->source_mode == SOURCE_MODE_TRANSFER" not in
        GATE[GATE.index("static bool source_mode_can_carry_control"):GATE.index("grid_gate_output_t")],
        "transfer must never be a carrying mode")
require("output.control_allowed = output.recovery_stable" in GATE,
        "authority must remain blocked until the current source earns its dwell")
require("source_absent_or_unknown" in GATE and "output.loss_confirmed" in GATE,
        "loss classification must remain separate from immediate authority blocking")
require('case GRID_GATE_LOST: return "source_lost"' in GATE,
        "status text must not mislabel generator/source loss as grid-only loss")

require("SOURCE_MODE_TRANSFER" in POLICY_TEST and "SOURCE_MODE_CONFLICT" in POLICY_TEST,
        "unsettled source modes must be exercised by the host policy test")
require("ramp_down_kw_per_second = 1.0f" in POLICY_TEST,
        "host test must prove a slow ramp cannot soften transition zeroing")
for token in (
    "test_source_change_requires_new_dwell",
    "SOURCE_MODE_GENERATOR_ONLY",
    "SOURCE_MODE_ISLAND",
    "SOURCE_MODE_GRID_GENERATOR_SYNC",
    "memory.recovery_mode == SOURCE_MODE_GENERATOR_ONLY",
):
    require(token in INTEGRATION_TEST, f"source-gate integration coverage missing: {token}")

print("source transition and recovery gate contract passed")
