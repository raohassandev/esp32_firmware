#!/usr/bin/env python3
"""Commissioning gate (P0-6) source contract.

Automatic control must be structurally incapable of engaging until an enumerated
set of commissioning prerequisites is satisfied, and the gate must fail closed:
unknown or unreadable state means not-commissioned, never commissioned.

This file checks the wiring. The decision logic itself is executed, not grepped,
by tests/commissioning_gate_test.c which this script compiles and runs.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GATE_H = (ROOT / "components/commissioning_gate/include/commissioning_gate.h").read_text(encoding="utf-8")
GATE_C = (ROOT / "components/commissioning_gate/commissioning_gate.c").read_text(encoding="utf-8")
GATE_CMAKE = (ROOT / "components/commissioning_gate/CMakeLists.txt").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
CONTROL_H = (ROOT / "components/control_engine/include/control_engine.h").read_text(encoding="utf-8")
CONTROL_TYPES = (ROOT / "components/control_engine/include/control_types.h").read_text(encoding="utf-8")
CONTROL_CMAKE = (ROOT / "components/control_engine/CMakeLists.txt").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8")
WEB_SERVER = (ROOT / "components/web_server/web_server.c").read_text(encoding="utf-8")
WEB_CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
STATUS_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")

# ---------------------------------------------------------------- enumeration
REQUIRED_PREREQS = [
    "COMMISSIONING_PREREQ_METER_ROLES",
    "COMMISSIONING_PREREQ_INVERTER_PROFILE_QUALIFIED",
    "COMMISSIONING_PREREQ_WRITE_READBACK",
    "COMMISSIONING_PREREQ_FLEET_CAPACITY",
    "COMMISSIONING_PREREQ_RAMP_POLICY",
    "COMMISSIONING_PREREQ_SOURCE_DETECTION",
    "COMMISSIONING_PREREQ_GRID_POLICY",
    "COMMISSIONING_PREREQ_GENERATOR_LIMITS",
    "COMMISSIONING_PREREQ_CONTROL_TUNING",
]
missing = [token for token in REQUIRED_PREREQS if token not in GATE_H]
if missing:
    raise SystemExit(f"commissioning prerequisites are not enumerated: {missing}")

# Every prerequisite must be individually evaluated, not folded into a blanket
# check, or the API cannot tell an engineer which one is unmet.
for prereq in REQUIRED_PREREQS:
    if f"status.results[{prereq}]" not in GATE_C:
        raise SystemExit(f"{prereq} is enumerated but never evaluated")

# ---------------------------------------------------------------- fail closed
if "if (!inputs || !inputs->state_readable)" not in GATE_C:
    raise SystemExit("the gate must treat a missing or unreadable input as not commissioned")
if "COMMISSIONING_REASON_STATE_UNREADABLE" not in GATE_C:
    raise SystemExit("unreadable state must have its own explicit reason code")
if "status.commissioned = status.unmet_count == 0U;" not in GATE_C:
    raise SystemExit("commissioned must require every prerequisite, not a subset")

# Each evidence group must be guarded by its own `known` flag, so a group the
# collector could not read cannot be silently treated as satisfied.
for known in ("meter_roles_known", "inverter_fleet_known", "ramp_policy_known",
              "source_detection_known", "grid_policy_known",
              "generator_limits_known", "control_tuning_known"):
    if known not in GATE_H or f"!in->{known}" not in GATE_C:
        raise SystemExit(f"{known} is not used to fail closed on unreadable state")

# The module must stay pure so it can be host-compiled and called from any task.
for forbidden in ("esp_", "portENTER_CRITICAL", "malloc(", "cJSON", "ESP_LOG",
                  "#include \"freertos"):
    if forbidden in GATE_C or forbidden in GATE_H:
        raise SystemExit(f"the commissioning gate must stay pure; found '{forbidden}'")
if "REQUIRES" in GATE_CMAKE:
    raise SystemExit("the commissioning gate component must not depend on anything")

# ------------------------------------------------------- structurally applied
if "REQUIRES commissioning_gate" not in CONTROL_CMAKE:
    raise SystemExit("control engine does not depend on the commissioning gate")
if "if (!commissioning.commissioned) control_enabled = false;" not in CONTROL:
    raise SystemExit(
        "the commissioning gate must disable the control task's own enable, not "
        "merely be reported alongside it"
    )
if "commissioning.commissioned && control_enabled" not in CONTROL:
    raise SystemExit("command authority must require commissioning")
if "!commissioning.commissioned ? commissioning_gate_summary(&commissioning)" not in CONTROL:
    raise SystemExit("the inhibit reason must lead with the unmet commissioning prerequisite")

# The gate is evaluated before the policy is stepped, so an uncommissioned
# controller never produces a command to clamp in the first place.
if CONTROL.index("if (!commissioning.commissioned) control_enabled = false;") > \
        CONTROL.index("if (control_enabled) policy = power_control_step(&input);"):
    raise SystemExit("the commissioning gate must be applied before the control policy is stepped")

# Collected from real state, with each `known` flag set only after the read.
for token in ("s_commissioning_inputs.meter_roles_known = true;",
              "s_commissioning_inputs.ramp_policy_known = true;",
              "s_commissioning_inputs.control_tuning_known = true;",
              "s_commissioning_inputs.inverter_fleet_known = fleet.known;",
              "s_commissioning_inputs.grid_policy_known = error == ESP_OK;",
              "s_commissioning_inputs.state_readable = true;",
              "inverter_manager_commissioning_summary(&fleet);"):
    if token not in CONTROL:
        raise SystemExit(f"commissioning evidence is not collected: {token}")

# state_readable must be the LAST thing set, after every group has been read.
if CONTROL.index("s_commissioning_inputs.state_readable = true;") < \
        CONTROL.index("s_commissioning_inputs.inverter_fleet_known = fleet.known;"):
    raise SystemExit("state_readable must be asserted only after every group has been collected")

# A caller reading the gate before the first evaluation must get the fail-closed
# answer, not a zeroed struct that would read as "nothing unmet".
if "if (!valid) snapshot = commissioning_gate_evaluate(NULL);" not in CONTROL:
    raise SystemExit("the commissioning getter must fail closed before the first evaluation")
if "void control_engine_get_commissioning(commissioning_status_t *out_status);" not in CONTROL_H:
    raise SystemExit("the commissioning status is not exposed to the API layer")
for field in ("bool commissioned;", "uint8_t commissioning_unmet_count;",
              "uint8_t commissioning_first_unmet;"):
    if field not in CONTROL_TYPES:
        raise SystemExit(f"control status does not carry commissioning summary field: {field}")

# ------------------------------------------------------------------- exposure
if "/api/commissioning/gate" not in API:
    raise SystemExit("the commissioning gate is not exposed over the API")
if "commissioning_gate_api_register(s_server)" not in WEB_SERVER:
    raise SystemExit("the commissioning gate API is never registered")
if '"commissioning_gate_api.c"' not in WEB_CMAKE:
    raise SystemExit("the commissioning gate API is not built")
if "REQUIRES commissioning_gate" not in WEB_CMAKE:
    raise SystemExit("web server does not depend on the commissioning gate")

# The API must name the unmet prerequisite AND say why, per prerequisite.
for token in ('commissioning_prereq_id(i)', 'commissioning_prereq_title(i)',
              'commissioning_reason_id(status.results[i].reason)',
              'commissioning_reason_message(status.results[i].reason)',
              'cJSON_AddBoolToObject(item, "satisfied", status.results[i].satisfied)'):
    if token not in API:
        raise SystemExit(f"the API does not report per-prerequisite detail: {token}")
if 'cJSON_AddStringToObject(root, "summary", commissioning_gate_summary(&status));' not in API:
    raise SystemExit("the API does not report the gate summary")

# Rule 7: HTTP handlers must never perform a blocking Modbus transaction.
for forbidden in ("modbus_tcp_", "meter_manager_read_registers", "inverter_manager_probe_read_only",
                  "inverter_manager_set_total_power_kw", "vTaskDelay"):
    if forbidden in API:
        raise SystemExit(f"the commissioning API handler must not call '{forbidden}'")
if 'cJSON_AddBoolToObject(root, "modbus_io_in_http_handler", false)' not in API:
    raise SystemExit("the commissioning API must declare that it performs no Modbus I/O")

# One status poll must be able to state the gate result without a second request.
for token in ('cJSON_AddBoolToObject(root, "commissioned", status.commissioned)',
              '"commissioning_first_unmet"'):
    if token not in STATUS_API:
        raise SystemExit(f"the Solar-Grid status API does not surface the gate: {token}")

# ------------------------------------------------------------------ unit test
with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "commissioning_gate_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "components/commissioning_gate/include"),
        str(ROOT / "tests/commissioning_gate_test.c"),
        str(ROOT / "components/commissioning_gate/commissioning_gate.c"),
        "-lm", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

print("commissioning gate contract passed")
