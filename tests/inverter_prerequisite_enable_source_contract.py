#!/usr/bin/env python3
"""Prerequisite enable-register sequencing: the wiring a unit test cannot see.

Solis (manual tag 3070 = 0xAA), Sungrow (tag 5007 = 0xAA) and Chint/CPS
(0x2602 = 1) each ignore their active-power setpoint until a separate register
holds a value first. The lethal part is not that the setpoint is ignored: the
setpoint register still ACCEPTS the write and still ECHOES it back, so the
ordinary readback matches and the controller reports the command CONFIRMED while
the inverter keeps generating.

The decision logic is EXECUTED by tests/inverter_prerequisite_test.c, and the
write-authority rule by tests/inverter_write_permission_test.c. This file guards
the four structural properties those tests cannot observe:

  1. The I/O rides the BACKGROUND acquisition task. The control path gains no
     blocking transaction - it runs at a 20 ms period and HTTP handlers must
     never perform a direct blocking Modbus transaction.
  2. prerequisite_satisfied is set from the verdict and from nowhere else, so an
     accepted write can never satisfy anything. Only a read may.
  3. An unverified prerequisite excludes the inverter from BOTH the commandable
     capacity and the command target selection, exactly as a confirmation fault
     already does.
  4. Re-verification is periodic, with a justification recorded in the source
     rather than a bare number.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

PREREQ_H = (ROOT / "components/inverter_manager/include/inverter_prerequisite.h").read_text(encoding="utf-8")
PREREQ_C = (ROOT / "components/inverter_manager/inverter_prerequisite.c").read_text(encoding="utf-8")
PROFILES_H = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
PROFILES_C = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
MANAGER_H = (ROOT / "components/inverter_manager/include/inverter_manager.h").read_text(encoding="utf-8")
TYPES = (ROOT / "components/inverter_manager/include/inverter_types.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/inverter_manager/CMakeLists.txt").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def code_only(text):
    """Strips comments, so a comment explaining what the code must NOT do cannot
    be mistaken for the code doing it."""
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


# ---------------------------------------------------------------------------
# The profile can describe a prerequisite, including its readback
# ---------------------------------------------------------------------------

for field in ("bool requires_prerequisite_enable;",
              "bool has_prerequisite_enable;",
              "uint8_t prerequisite_write_function;",
              "uint16_t prerequisite_address;",
              "uint16_t prerequisite_value;",
              "bool has_prerequisite_readback;",
              "uint8_t prerequisite_readback_function;",
              "uint16_t prerequisite_readback_address;",
              "uint16_t prerequisite_readback_mask;"):
    require(field in PROFILES_H, f"the profile cannot describe a prerequisite: missing {field}")

# A readback is not optional. A prerequisite written blind is an assertion the
# controller cannot check, and the setpoint echoes regardless.
require("inverter_prerequisite_write_blocked" in PROFILES_H,
        "the profile header must express the write-authority consequence of a "
        "prerequisite that cannot be described or read back")
require("!write_described || !readback_described" in PREREQ_H,
        "an unreadable prerequisite must block write authority exactly as a missing "
        "setpoint readback does")

# The gate must consult it, in both modes, and must not have been softened into a
# warning.
require("inverter_profile_prerequisite_blocks_write(profile)" in PROFILES_C,
        "the write permission gate must consult the prerequisite predicate")
require(PROFILES_C.count("inverter_profile_prerequisite_blocks_write(profile)") >= 2,
        "both inverter_profile_allows_write and inverter_profile_write_permission "
        "must apply the prerequisite rule, or callers of the two would disagree")
permission = re.search(
    r"inverter_write_permission_t inverter_profile_write_permission\(.*?\n\{(.*?)\n\}",
    PROFILES_C, re.DOTALL)
require(permission is not None, "inverter_profile_write_permission must exist")
if permission:
    body = permission.group(1)
    blocked_at = body.find("inverter_profile_prerequisite_blocks_write")
    lab_at = body.find("declared_lab_target")
    require(blocked_at != -1 and lab_at != -1 and blocked_at < lab_at,
            "the prerequisite must be checked BEFORE lab authority is granted, or a "
            "declared lab target with an unverifiable prerequisite would be commandable")

# A zero mask must never be used literally: it would make every reading compare
# equal and confirm the prerequisite unconditionally.
require("prerequisite_readback_mask == 0U) return 0xFFFFU" in PROFILES_H,
        "a zero readback mask must mean the whole register, never 'match anything'")


# ---------------------------------------------------------------------------
# The evaluator stays pure, so host tests execute the real rule
# ---------------------------------------------------------------------------

for forbidden in ("esp_", "portENTER_CRITICAL", "malloc(", "ESP_LOG", "vTaskDelay",
                  "modbus_", "cJSON"):
    if forbidden in PREREQ_C or forbidden in PREREQ_H:
        failures.append(f"the prerequisite evaluator must stay pure; found '{forbidden}'")

require('"inverter_prerequisite.c"' in CMAKE, "the prerequisite module is not built")

# Fail closed on unknown, and NONE must be the zero action so a zeroed verdict
# provokes no Modbus traffic.
require("INVERTER_PREREQ_ACTION_NONE = 0" in PREREQ_H,
        "the no-op action must be zero so a zeroed verdict requests no I/O")
require("if (!evidence || !evidence->populated)" in PREREQ_C,
        "absent or unpopulated evidence must fail closed rather than read as satisfied")
require("bool populated;" in PREREQ_H,
        "the evidence must carry an explicit populated flag, or a zeroed struct "
        "would read as a device that simply needs no prerequisite")


# ---------------------------------------------------------------------------
# 1. The I/O rides the background task; the control path gains nothing
# ---------------------------------------------------------------------------

require("static void service_prerequisite(inverter_runtime_t *runtime, uint32_t timestamp)"
        in MANAGER,
        "there is no background prerequisite sequencer")
TELEMETRY = MANAGER[MANAGER.index("static void inverter_telemetry_task"):]
require("service_prerequisite(runtime, timestamp)" in TELEMETRY,
        "prerequisite verification does not ride the background acquisition task")
# Before the poll gate, so a sample can expire on an inverter that has gone quiet
# while holding a setpoint.
if "service_prerequisite(runtime, timestamp)" in TELEMETRY:
    require(TELEMETRY.index("service_prerequisite(runtime, timestamp)") <
            TELEMETRY.index("if ((int32_t)(timestamp - runtime->next_poll_ms) < 0) continue;"),
            "the prerequisite must be evaluated before the poll gate, or a switch "
            "turned off on a quiet inverter would never be noticed")

COMMAND = MANAGER[MANAGER.index("esp_err_t inverter_manager_set_total_power_kw"):
                  MANAGER.index("inverter_write_state_t inverter_manager_fleet_write_confirmation")]
for forbidden in ("service_prerequisite", "read_prerequisite", "write_prerequisite",
                  "write_profile_register", "vTaskDelay"):
    if forbidden in COMMAND:
        failures.append(
            f"the command path must not '{forbidden}': it runs on the control period "
            "and must only read the already-resolved flag"
        )


# ---------------------------------------------------------------------------
# 2. Only a read may satisfy it
# ---------------------------------------------------------------------------

require(MANAGER.count("data.prerequisite_satisfied =") == 2,
        "prerequisite_satisfied must have exactly two writers: the init that sets it "
        "from whether a prerequisite is needed at all, and the verdict")
require("runtime->data.prerequisite_satisfied = decision.satisfied;" in MANAGER,
        "prerequisite_satisfied must be taken from the pure verdict, not re-derived")
WRITE_FN = code_only(MANAGER[MANAGER.index("static esp_err_t write_prerequisite"):
                             MANAGER.index("static void service_prerequisite")])
require("prerequisite_satisfied" not in WRITE_FN,
        "the prerequisite WRITE must never set satisfied: an accepted write proves "
        "only that the transport took the frame, and the whole hazard is a device "
        "that accepts and ignores")
require("prerequisite_holds" not in WRITE_FN,
        "the prerequisite write must not claim the register now holds; only a read may")

READ_FN = MANAGER[MANAGER.index("static esp_err_t read_prerequisite"):
                  MANAGER.index("static esp_err_t write_prerequisite")]
require("runtime->data.prerequisite_holds = false;" in READ_FN,
        "a failed prerequisite read must not leave the previous 'holds' standing as "
        "though it had been re-confirmed")

# The write must use a documented write function code and nothing else.
require("inverter_prerequisite_write_function_supported(function_code)" in MANAGER,
        "the prerequisite write must refuse any function code that is not a "
        "documented register-write code")


# ---------------------------------------------------------------------------
# 3. An unverified prerequisite removes the inverter, in both places
# ---------------------------------------------------------------------------

require(MANAGER.count("runtime->data.prerequisite_satisfied &&") >= 2,
        "an unverified prerequisite must exclude the inverter from BOTH the "
        "commandable capacity and the command target selection, exactly as a "
        "confirmation fault does")
require("bool prerequisite_satisfied;" in TYPES,
        "the runtime data must carry the prerequisite verdict")
require("bool prerequisite_required;" in TYPES and "bool prerequisite_unverifiable;" in TYPES,
        "the runtime data must distinguish 'needs one' from 'cannot verify one'")


# ---------------------------------------------------------------------------
# 4. Re-verification is periodic and justified
# ---------------------------------------------------------------------------

require("INVERTER_PREREQUISITE_RECHECK_MS" in MANAGER,
        "the prerequisite must be re-verified on a period, not once")
require("uint32_t prerequisite_recheck_ms;" in PROFILES_H,
        "a profile must be able to tighten the re-verification period")
require("0x55 OFF(Power to 100%)" in MANAGER,
        "the re-verification period must record WHY it exists: Solis returns the "
        "machine to 100% when the switch is disabled")
require("expiry_ms" in PREREQ_H and "recheck_ms >= evidence->expiry_ms" in PREREQ_C,
        "a sample must expire on a longer horizon than its own re-read is due, or "
        "the inverter would leave and rejoin the commandable fleet continuously")
require("prerequisite_lost_count" in TYPES,
        "a prerequisite that stops holding must be counted, because that is the "
        "failure periodic re-verification exists to catch")


# ---------------------------------------------------------------------------
# Reported, and distinct from a setpoint mismatch
# ---------------------------------------------------------------------------

require("bool inverter_manager_prerequisite_enable_fault(void);" in MANAGER_H,
        "the prerequisite fault is not exposed")
require("inverter_manager_prerequisite_enable_fault" in MANAGER,
        "the prerequisite fault accessor is not implemented")
for field in ("uint8_t prerequisite_required_count;",
              "uint8_t prerequisite_unconfirmed_count;",
              "uint8_t prerequisite_unverifiable_count;"):
    require(field in MANAGER_H, f"the fleet summary does not report: {field}")
for field in ("prerequisite_required_count++", "prerequisite_unconfirmed_count++",
              "prerequisite_unverifiable_count++"):
    require(field in MANAGER, f"the fleet summary never populates: {field}")
# The counts must be taken before the permission switch, whose FORBIDDEN branch
# continues - and an undescribable prerequisite is exactly why one is forbidden.
SUMMARY = MANAGER[MANAGER.index("void inverter_manager_commissioning_summary"):
                  MANAGER.index("float inverter_manager_get_total_rated_kw")]
require(SUMMARY.index("prerequisite_required_count++") < SUMMARY.index("switch (runtime->permission)"),
        "the prerequisite counts must be taken before the permission switch, whose "
        "forbidden branch continues, or the count that explains the refusal is lost")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("inverter prerequisite enable sequencing contract passed "
      "(readback mandatory for authority, sequencing in the background task, only a "
      "read may satisfy, unverified excludes the inverter, re-verified periodically)")
