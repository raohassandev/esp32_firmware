"""The site power-flow model must represent the plant and must not invent values.

P0-7. The energy-flow view carried solar, controller and utility grid. On a
PV-DG product the generator is the asset the entire control strategy exists to
protect, and facility load is what both sources are there to serve; a flow
diagram missing both does not describe this plant.

The governing constraint is what is actually measured. On the reference site one
meter is configured, at the point of common coupling. Generator power and
facility load are not measured, and the firmware says so itself in
/api/telemetry (generator_telemetry_supported, facility_load_supported). A node
showing "0.00 kW" for an unmetered generator tells an operator it is off-load
when in truth nothing is watching it, so an unmeasured quantity must say "Not
measured", and no arrow may be drawn out of a node whose magnitude is unknown.

Facility load in particular must not be derived. With a single measurement point
load = grid + generator + solar has more unknowns than equations, and the result
would be the most convincing wrong number on the screen.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UTILS = (ROOT / "web/devices-utils.js").read_text(encoding="utf-8")
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
INDEX = (ROOT / "web/index.html").read_text(encoding="utf-8")
OPERATOR = (ROOT / "web/operator-view.js").read_text(encoding="utf-8")
DEVICES = (ROOT / "web/devices.js").read_text(encoding="utf-8")
DEVICE_API = (ROOT / "components/web_server/device_api.c").read_text(encoding="utf-8")
WEB_API = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ------------------------------------------------------------- the five nodes

require("function buildPowerFlowModel" in UTILS,
        "there is no shared power-flow model")
for node in ["function gridNode", "function generatorNode", "function solarNode",
             "function controllerNode", "function loadNode"]:
    require(node in UTILS, f"the flow model is missing a node: {node}")
for label in ["'Utility grid'", "'Point of common coupling'", "'Generator'",
              "'Solar fleet'", "'Facility load'", "'PV-DG controller'"]:
    require(label in UTILS, f"the flow model does not name {label}")

# One model, both screens, so the operator view and the engineering dashboard
# cannot disagree about the plant.
require("buildPowerFlowModel" in APP and "buildPowerFlowModel" in OPERATOR,
        "the two energy views must share one model")
require('id="powerFlow"' in INDEX, "the dashboard has no flow container")


# ------------------------------------------------- unmeasured means unmeasured

require("const NOT_MEASURED = 'Not measured'" in UTILS,
        "there is no explicit not-measured state")
require("generator_telemetry_supported" in UTILS and "facility_load_supported" in UTILS,
        "the model must use the controller's own declaration of what it measures")
require("generator_telemetry_supported" in DEVICE_API and
        "facility_load_supported" in DEVICE_API,
        "the firmware no longer declares generator/load measurement support")

# The firmware currently declares both unsupported; the copy must match.
require('cJSON_AddBoolToObject(availability, "generator_telemetry_supported", false)' in DEVICE_API,
        "generator power became measured; the UI copy must be revisited")
require('cJSON_AddBoolToObject(availability, "facility_load_supported", false)' in DEVICE_API,
        "facility load became measured; the UI copy must be revisited")

require("It is not zero" in UTILS,
        "an unmetered generator must say it is not zero, it is unwatched")
require("It is not derived from the grid meter" in UTILS,
        "facility load must state that it is not derived")
require("more unknowns than equations" in UTILS,
        "the refusal to derive facility load must explain why")

# No arithmetic invents a node value from the others.
model = UTILS[UTILS.index("function buildPowerFlowModel"):]
for invention in ["grid_power_kw +", "grid_power_kw -", "+ applied_pv_kw", "- applied_pv_kw"]:
    require(invention not in UTILS,
            f"the flow model derives an unmeasured quantity: {invention}")

# A direction of flow is only drawn where the magnitude is measured.
direction = UTILS[UTILS.index("function flowDirection"):UTILS.index("function gridNode")]
require("if (value == null) return { direction: 'unknown'" in direction,
        "an unmeasured node must not be given a flow direction")
require("'Flow not measured'" in UTILS,
        "the unmeasured connector must be labelled, not silently blank")


# --------------------------------- provenance travels with every measured value

require("function describeProvenance" in UTILS,
        "provenance rendering must be shared, not duplicated per screen")
nodes = UTILS[UTILS.index("function gridNode"):UTILS.index("function buildPowerFlowModel")]
require(nodes.count("provenance:") >= 5,
        "every flow node must carry its own provenance line")
require('"grid_measurement"' in WEB_API and '"control_authority"' in WEB_API,
        "the status API no longer publishes the provenance the flow view renders")
require("describeProvenance(measurement)" in UTILS,
        "the grid node must render the measurement provenance the firmware publishes")
require("age_ms: authority.last_command_age_ms" in UTILS,
        "the controller node must show how old its last command is")

# The applied PV limit is a command, not production. Presenting it as generation
# is the easiest way for this screen to lie.
require("a limit written to the fleet, not measured production" in UTILS,
        "the applied PV command must be distinguished from measured production")
require("valueKind" in UTILS and "'command'" in UTILS,
        "commanded values must be typed separately from measured values")

# The type must survive into the DOM, or the distinction exists only in the
# model. "Not measured" and "Monitoring only" were set at the same size and
# weight as "25.23 kW" - a word standing where a reader is looking for a
# quantity, which is the same confusion between a measurement and the absence
# of one that the unmeasured rules elsewhere exist to prevent.
OPERATOR_CSS = (ROOT / "web/product-mode.css").read_text(encoding="utf-8")
require("kind-${String(entry.valueKind" in OPERATOR,
        "the flow node must carry its value KIND into the DOM, or the model's "
        "distinction between a measurement and a command cannot be styled")
_flow_rules = re.sub(r"/\*.*?\*/", "", OPERATOR_CSS, flags=re.S)
require(".op-flow-source.kind-not_measured > strong" in _flow_rules
        and ".op-flow-source.kind-command > strong" in _flow_rules,
        "a state word must not be typeset as a measured quantity")


# ----------------------------------------------- no extra load on the controller

# /api/telemetry is already polled by devices.js on the dashboard route. The
# server offers four client sockets (audit S1), so the flow model reuses that
# answer instead of issuing a second poll.
require("amx-site-telemetry" in DEVICES and "amx-site-telemetry" in APP,
        "the site capability report must be republished, not fetched twice")
require("amx-site-telemetry" in OPERATOR,
        "the operator flow card must reuse the shared telemetry poll")
require("mayUseEngineering('dashboard')" in APP,
        "engineering-gated source detection must be scoped before it is requested")


# -------------------------------------------------------------------- styling

for token in [".flow-node.flow-unmeasured", ".flow-kind", ".flow-line-unknown",
              ".flow-column"]:
    require(token in APP_CSS, f"flow styling is incomplete: {token}")
flow_css = APP_CSS[APP_CSS.index("/* P0-7 site energy model"):APP_CSS.index(".health-list")]
require("#" not in flow_css,
        "flow styling must resolve every colour through a theme token, not hardcoded hex")


# ------------------------------------------------------------ CI registration

require("tests/power_flow_model_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")

print("site power-flow model source contract: PASS")
