#!/usr/bin/env python3
"""Inverter setpoint write confirmation / readback (P0-9) source contract.

A written setpoint must be read back and confirmed, and reported as confirmed,
mismatched or unverified - never as a silent success. The confirmation must ride
the existing background acquisition path: it may not add a blocking Modbus
transaction to any HTTP handler and it may not slow the control loop.

The decision logic itself is executed, not grepped, by
tests/inverter_write_confirmation_test.c which this script compiles and runs.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CONF_H = (ROOT / "components/inverter_manager/include/inverter_write_confirmation.h").read_text(encoding="utf-8")
CONF_C = (ROOT / "components/inverter_manager/inverter_write_confirmation.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
MANAGER_H = (ROOT / "components/inverter_manager/include/inverter_manager.h").read_text(encoding="utf-8")
TYPES = (ROOT / "components/inverter_manager/include/inverter_types.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/inverter_manager/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8")
WEB_SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

# ------------------------------------------------------- the reported outcomes
for state in ("INVERTER_WRITE_UNVERIFIED", "INVERTER_WRITE_PENDING",
              "INVERTER_WRITE_CONFIRMED", "INVERTER_WRITE_MISMATCHED"):
    if state not in CONF_H:
        raise SystemExit(f"write confirmation state {state} is missing")
# UNVERIFIED must be the zero value so a zeroed struct is never "confirmed".
if "INVERTER_WRITE_UNVERIFIED = 0" not in CONF_H:
    raise SystemExit("the default write confirmation state must be UNVERIFIED, not confirmed")

# A manufacturer whose readback register this firmware does not have must report
# unverified. It must never be promoted to confirmed.
# The rule now has a second, stronger arm: a profile may confirm on an
# independent MEASURED quantity instead of a setpoint readback, because for a
# command target whose readback is an echo of a stored command the measurement is
# the stronger witness and the echo is worth nothing. What must not change is that
# a target with NEITHER source can never report confirmed.
if "if (!evidence->readback_supported && !measured_required)" not in CONF_C:
    raise SystemExit("a profile without a readback register must be handled explicitly")
if "INVERTER_MEASURED_CONFIRM_REQUIRED" not in CONF_H:
    raise SystemExit("measured-power confirmation must be expressible, or a stored "
                     "command echo is the only evidence a logger-level write can have")
# Measured power below a limit is equally consistent with the limit being honoured
# and with the sun going in. Only a fall from ABOVE the limit demonstrates one, and
# the ambiguous case must be reported as proving nothing.
if "INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM" not in CONF_H:
    raise SystemExit("the ambiguous measured case must have its own reported reason")
if "limit_demonstrated" not in CONF_H:
    raise SystemExit("a demonstrated limit must be distinguishable from a consistent one")
UNSUPPORTED = CONF_C[CONF_C.index("if (!evidence->readback_supported && !measured_required)"):]
if "verdict(INVERTER_WRITE_UNVERIFIED, true, true)" not in UNSUPPORTED.split("}")[0] + "}":
    raise SystemExit("a profile without a readback register must report unverified and go safe")
if "if (!evidence) return verdict(INVERTER_WRITE_UNVERIFIED, true, true);" not in CONF_C:
    raise SystemExit("absent evidence must fail closed to unverified")
if "readback_after_write" not in CONF_C:
    raise SystemExit("a readback taken before the write must not be able to confirm it")
if "if (!states || count == 0U) return INVERTER_WRITE_UNVERIFIED;" not in CONF_C:
    raise SystemExit("an empty fleet roll-up must be unverified, never confirmed")

# The evaluator must stay pure so it can be host-compiled and called under a lock.
for forbidden in ("esp_", "portENTER_CRITICAL", "malloc(", "ESP_LOG", "vTaskDelay",
                  "modbus_", "cJSON"):
    if forbidden in CONF_C or forbidden in CONF_H:
        raise SystemExit(f"the write confirmation evaluator must stay pure; found '{forbidden}'")

if '"inverter_write_confirmation.c"' not in CMAKE:
    raise SystemExit("the write confirmation module is not built")

# ------------------------------------- confirmation rides background acquisition
if "static bool evaluate_write_confirmation(inverter_runtime_t *runtime, uint32_t timestamp)" not in MANAGER:
    raise SystemExit("there is no background write confirmation evaluator")
TELEMETRY = MANAGER[MANAGER.index("static void inverter_telemetry_task"):]
if "evaluate_write_confirmation(runtime, timestamp)" not in TELEMETRY:
    raise SystemExit("write confirmation does not ride the background acquisition task")
if "issue_safe_zero(runtime)" not in TELEMETRY:
    raise SystemExit("an unconfirmed setpoint must be driven safe by the background task")

# It must be evaluated before any gate that can skip this inverter, so the
# confirmation deadline still expires on a device that has gone silent.
if TELEMETRY.index("evaluate_write_confirmation(runtime, timestamp)") > \
        TELEMETRY.index("if ((int32_t)(timestamp - runtime->next_poll_ms) < 0) continue;"):
    raise SystemExit(
        "write confirmation must be evaluated before the poll gate, or an "
        "unconfirmed setpoint can stand indefinitely on a silent inverter"
    )

# ------------------------------------------- the control loop is not slowed down
COMMAND = MANAGER[MANAGER.index("esp_err_t inverter_manager_set_total_power_kw"):
                  MANAGER.index("inverter_write_state_t inverter_manager_fleet_write_confirmation")]
for forbidden in ("vTaskDelay", "read_profile_block", "read_limit_percent",
                  "modbus_tcp_read_registers"):
    if forbidden in COMMAND:
        raise SystemExit(
            f"the command path must not '{forbidden}': it runs on the control period"
        )
if "INVERTER_COMMAND_READBACK_DELAY_MS" in MANAGER:
    raise SystemExit("the synchronous post-write settle delay must be gone from the write path")
if "note_write_issued(target->runtime, target->percent, write_error == ESP_OK," not in COMMAND:
    raise SystemExit("an issued write must be recorded for later confirmation")

# ---------------------------------------------- no silent assumption of success
# Only the confirmation evaluator may promote a written value to a commanded one.
if MANAGER.count("data.commanded_percent =") != 1 or MANAGER.count("data.commanded_power_kw =") != 1:
    raise SystemExit("the confirmed command must have exactly one writer")
CONFIRM = MANAGER[MANAGER.index("static bool evaluate_write_confirmation"):
                  MANAGER.index("static void mark_status_unknown")]
for token in ("if (changed && verdict.state == INVERTER_WRITE_CONFIRMED) {",
              "runtime->data.commanded_percent = runtime->data.requested_percent;",
              "runtime->data.confirmation_fault = false;",
              "runtime->data.confirmation_fault = true;",
              "runtime->data.unverified_count++;",
              "runtime->data.mismatch_count++;"):
    if token not in CONFIRM:
        raise SystemExit(f"the confirmation evaluator does not record: {token}")

for field in ("uint8_t write_confirmation;", "bool confirmation_fault;",
              "float requested_percent;", "uint32_t last_write_ms;",
              "uint32_t confirmed_count;", "uint32_t unverified_count;"):
    if field not in TYPES:
        raise SystemExit(f"inverter runtime data does not carry: {field}")

# An unconfirmed setpoint must structurally remove that inverter from the fleet.
if MANAGER.count("!runtime->data.confirmation_fault") < 2:
    raise SystemExit(
        "confirmation_fault must exclude the inverter from both the commandable "
        "capacity and the command target selection"
    )

# The write gate already requires a readback register, so no production-approved
# profile can exist without one.
if "profile->has_power_limit && profile->has_power_limit_readback" not in PROFILES:
    raise SystemExit("production write approval must require a readback register")

# ------------------------------------------------------------------- reporting
for token in ("inverter_write_state_t inverter_manager_fleet_write_confirmation(void);",
              "bool inverter_manager_write_confirmation_fault(void);"):
    if token not in MANAGER_H:
        raise SystemExit(f"write confirmation is not exposed: {token}")

if "inverter_manager_write_confirmation_fault()" not in CONTROL:
    raise SystemExit("the control engine does not observe the write confirmation fault")
if "!confirmation_fault" not in CONTROL:
    raise SystemExit("command authority must be withdrawn while a write is unconfirmed")
if "could not be confirmed by readback" not in CONTROL:
    raise SystemExit("the operator is not told why control is inhibited")

if "/api/inverters/write-confirmation" not in API:
    raise SystemExit("write confirmation is not exposed over the API")
if "commissioning_gate_api_register(s_server)" not in WEB_SERVER:
    raise SystemExit("the write confirmation API is never registered")
for token in ('"requested_percent"', '"confirmed_percent"', '"readback_percent"',
              '"fleet_state"', '"confirmation_fault"', 'inverter_write_state_name'):
    if token not in API:
        raise SystemExit(f"the write confirmation API does not report: {token}")
for forbidden in ("modbus_tcp_", "inverter_manager_set_total_power_kw",
                  "inverter_manager_probe_read_only", "vTaskDelay"):
    if forbidden in API:
        raise SystemExit(f"the write confirmation handler must not call '{forbidden}'")

# ------------------------------------------------------------------ unit test
with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "inverter_write_confirmation_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "components/inverter_manager/include"),
        str(ROOT / "tests/inverter_write_confirmation_test.c"),
        str(ROOT / "components/inverter_manager/inverter_write_confirmation.c"),
        "-lm", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

print("inverter write confirmation and readback contract passed")
