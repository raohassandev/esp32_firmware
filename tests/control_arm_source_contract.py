#!/usr/bin/env python3
"""Automatic control must be armable, and arming must not be a way around the gate.

Until POST /api/control/enable existed there was no way to arm automatic control
at all. control.enabled was written false in four places and true in none, and
no route in the web server reached it. The control engine was complete -- step,
ramp, readback confirmation, safe zero -- and unreachable, which is the reason
the loop had never run end to end on any unit. The missing piece was never
hardware; it was the switch.

Adding a switch to a safety interlock is exactly the kind of change that can
quietly become the way around it, so five properties are asserted.

1. THE ENGINE STILL DECIDES. The control task re-evaluates the commissioning
   gate every cycle and withholds command authority itself. Arming must not be
   able to grant authority the gate refuses, so that per-cycle check must stay.

2. ARMING IS REFUSED WHEN THE GATE IS UNSATISFIED. Not because that is what
   protects the plant -- point 1 is -- but so an engineer is told why nothing
   happened instead of arming into a controller that silently does nothing.

3. DISARMING IS UNCONDITIONAL. Being unable to command is recoverable; being
   unable to stop commanding is not. No gate check, no commissioning check and
   no persistence failure may stand between a request to stop and stopping.

4. A REFUSED ARM IS NOT PERSISTED. Storing an armed intent the engine declined
   would arm the plant on the next reboot on the strength of a request that was
   rejected.

5. LAB SCOPE IS DECLARED IN THE RESPONSE THAT GRANTS THE AUTHORITY. An engineer
   who arms against a simulator must be told in the same breath, not in a
   document.

Asserted against comment-stripped source, so a promise made only in a comment
cannot satisfy any of it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def source(path):
    text = (ROOT / path).read_text(encoding="utf-8", errors="replace")
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)


engine = source("components/control_engine/control_engine.c")
api = source("components/web_server/commissioning_gate_api.c")
guard = source("components/web_server/engineering_guard.c")

# --- 1. The engine still decides ------------------------------------------

require(
    re.search(r"command_authority\s*=\s*commissioning\.commissioned\s*&&", engine) is not None,
    "the control task no longer conditions command authority on the commissioning "
    "gate; arming would become the whole decision",
)

# --- 2. Arming is refused when the gate is unsatisfied ---------------------

arm_fn = re.search(r"esp_err_t control_engine_set_enabled\(bool enabled\)\s*\{(.*?)\n\}", engine, re.DOTALL)
require(arm_fn is not None, "control_engine_set_enabled() could not be located")
if arm_fn is not None:
    body = arm_fn.group(1)
    require(
        "!commissioning.commissioned" in body and "ESP_ERR_INVALID_STATE" in body,
        "arming does not check the commissioning gate",
    )
    # A latched disable that has not reached zero must not be armed across: the
    # safe-zero write would race the first commanded setpoint and which landed
    # last would depend on timing.
    require(
        "s_safe_zero_pending" in body,
        "arming does not wait for a pending safe zero, so a disable settling to "
        "zero could race the first commanded setpoint",
    )

    # --- 3. Disarming is unconditional -------------------------------------
    disarm = re.search(r"if \(!enabled\) \{(.*?)\}", body, re.DOTALL)
    require(disarm is not None, "the disarm path could not be located")
    if disarm is not None:
        require(
            "control_engine_force_disable()" in disarm.group(1)
            and "return ESP_OK" in disarm.group(1),
            "disarming does not unconditionally latch the controller disabled",
        )
        require(
            "commissioned" not in disarm.group(1),
            "disarming consults the commissioning gate; a plant that cannot be "
            "commissioned must still be stoppable",
        )

# --- 4. A refused arm is not persisted ------------------------------------

handler = re.search(r"static esp_err_t control_enable_post\(httpd_req_t \*request\)\s*\{(.*?)\n\}", api, re.DOTALL)
require(handler is not None, "control_enable_post() could not be located")
if handler is not None:
    body = handler.group(1)
    require(
        re.search(r"if \(applied == ESP_OK\) \{[^}]*config_manager_save", body, re.DOTALL) is not None,
        "the persisted value is not conditioned on the engine having accepted it; "
        "a refused arm would arm the plant on the next reboot",
    )
    require(
        "control_engine_set_enabled" in body,
        "the handler does not go through control_engine_set_enabled()",
    )
    # 5. Lab scope declared where the authority is granted.
    # Matched inside one string literal. C concatenates adjacent literals, so a
    # phrase that spans the join ("... is not " "evidence about ...") is not a
    # substring of the source and searching for it would fail against correct code.
    require(
        "COMMISSIONING_SCOPE_LAB" in body
        and "evidence about physical equipment" in body
        and "lab simulator scope" in body,
        "arming in lab scope does not say so in its own response",
    )
    # A refusal must carry the firmware's own reason, not a summary invented here.
    require(
        "commissioning_gate_summary(&status)" in body,
        "a refused arm does not report the firmware's own reason",
    )
    # No Modbus in an HTTP handler -- the standing rule for this component.
    require(
        "modbus_" not in body,
        "the arm handler performs Modbus I/O; the control task applies the change "
        "on its next cycle and the handler must not transact",
    )

# --- The route exists and is engineering-gated ----------------------------

require(
    re.search(r'\.uri = "/api/control/enable",\s*\.method = HTTP_POST', api) is not None,
    "POST /api/control/enable is not registered",
)

public = re.search(r"static bool public_uri\(const char \*uri\)\s*\{(.*?)\n\}", guard, re.DOTALL)
require(public is not None, "public_uri() could not be located")
if public is not None:
    require(
        "/api/control/enable" not in public.group(1),
        "the arm route is in public_uri(): anyone on the network could arm the plant",
    )
for line in guard.splitlines():
    if "GATEWAY_MODE_SAFE" in line and "/api/control/enable" in line:
        failures.append(
            "the arm route carries a SAFE_* classification, which bypasses the "
            "engineering session"
        )

# --- Nothing else writes an armed state -----------------------------------

# Four call sites force control.enabled false; a fifth writing true somewhere
# other than this handler would be a second, unreviewed way to arm.
arm_writes = []
for path in sorted((ROOT / "components").rglob("*.c")):
    text = re.sub(r"/\*.*?\*/", " ", path.read_text(encoding="utf-8", errors="replace"), flags=re.DOTALL)
    for match in re.finditer(r"control\.enabled\s*=\s*([A-Za-z_][A-Za-z0-9_]*|true)", text):
        value = match.group(1)
        if value == "false":
            continue
        arm_writes.append(f"{path.relative_to(ROOT).as_posix()}: control.enabled = {value}")

require(
    len(arm_writes) == 1 and arm_writes[0].startswith("components/web_server/commissioning_gate_api.c"),
    f"automatic control is armed from more than one place: {arm_writes}",
)

if failures:
    print("Control arm contract FAILED:")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)

print(
    "Control arm contract passed (engine still gates every cycle, arming refused "
    "when unsatisfied, disarm unconditional, refused arm not persisted, lab scope "
    "declared)"
)
