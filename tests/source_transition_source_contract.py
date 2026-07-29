#!/usr/bin/env python3
"""Phase 4: a source change must never leave PV commanded on the outgoing source.

Phase 4 needed far less new code than expected, because the debounce added in
phase 1 and the stability gate already in the power policy together produce the
required behaviour. This contract pins that behaviour down so it cannot be
dismantled by a later change that looks harmless in isolation.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY = (ROOT / "components/control_engine/power_control_policy.c").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
DETECT = (ROOT / "components/source_detection/source_detection_engine.c").read_text(encoding="utf-8")
POLICY_TEST = (ROOT / "tests/power_control_policy_test.c").read_text(encoding="utf-8")


def require(condition, message):
    assert condition, message


# 1. Only a settled source may drive the plant. Anything else is not "stable".
stable = POLICY[POLICY.index("static bool source_mode_is_stable"):]
stable = stable[:stable.index("\n}")]
for unsettled in ("SOURCE_MODE_UNKNOWN", "SOURCE_MODE_NO_SOURCE",
                  "SOURCE_MODE_TRANSFER", "SOURCE_MODE_CONFLICT"):
    require(unsettled not in stable,
            f"{unsettled} must never count as a stable source: a transition would keep PV "
            "commanded on the source being left behind")

# 2. The stability gate must sit in the early-return guard, so an unsettled source
#    is rejected before any command is computed - never ramped down from.
step = POLICY[POLICY.index("power_control_output_t power_control_step"):]
guard = step[:step.index("return")]
require("source_mode_is_stable(input->source_mode)" in guard,
        "the stability gate must be part of the guard that rejects an input outright, "
        "so a changeover cannot produce a ramped-down command instead of zero")

# 3. The control engine must act within the same cycle, not wait for the next one.
require("if (!control_enabled || !policy.valid) {" in CONTROL,
        "an invalid policy result must be handled in the same control cycle")
zeroing = CONTROL[CONTROL.index("if (!control_enabled || !policy.valid) {"):]
zeroing = zeroing[:zeroing.index("float applied_kw")]
require("policy.requested_pv_kw = 0.0f;" in zeroing,
        "an invalid policy result must zero the request immediately, not decay it")

# 4. The transition itself must be debounced, and must report unknown while it runs
#    rather than holding the previous source.
require("SOURCE_STATE_UNKNOWN" in DETECT, "the detection engine must have an unknown state")
require("DEBOUNCE_PENDING" in DETECT or "debounce" in DETECT.lower(),
        "source transitions must be debounced")
require("stable_state = SOURCE_STATE_UNKNOWN" in DETECT,
        "a new candidate source must clear the settled state rather than retaining the old one")

# 5. The behaviour must be executably tested, not merely asserted here.
require("SOURCE_MODE_TRANSFER" in POLICY_TEST and "SOURCE_MODE_CONFLICT" in POLICY_TEST,
        "every unsettled source must be exercised by the host test")
require("ramp_down_kw_per_second = 1.0f" in POLICY_TEST,
        "the test must prove a slow ramp cannot soften the zeroing on a transition")

print("source transition source contract passed")
