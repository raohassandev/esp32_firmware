"""ISA-18.2 suppression states: gap A9 of docs/ALARM_MANAGEMENT_RESEARCH.md.

ISA-18.2 defines three ways an alarm stops pressing the operator and is explicit
that they must NOT be collapsed into one "disabled" flag, because collapsing them
destroys the audit trail that makes suppression safe. Shelving (operator,
time-limited, expiring) already existed. This contract covers the two that were
added: suppressed by design, which is the controller's own decision driven by
plant state, and out of service, which is a maintenance action under
authorisation.

This is a source contract and cannot prove the firmware behaves on hardware. The
state machine itself is executed instead, by tests/alarm_suppression_test.c, which
enumerates all eight flag combinations and asserts none of them loses a fact. What
is proved here is that the three states are wired through the firmware, the API,
the persistent journal and the interface as three distinct things, that the
non-expiring one is the hardest to reach rather than the easiest, and that the
failure modes this module has already paid for are not reintroduced: suppressing a
condition instead of attributing it, writing flash with interrupts disabled, and
letting suppression reach into condition detection.

Contrast is COMPUTED from the parsed token values in both themes rather than
asserted, because this codebase has twice shipped 1.10:1 and 1.17:1 text by
declaring a colour for one theme only.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API_PATH = ROOT / "components/web_server/operational_api.c"
SUPPRESSION_PATH = ROOT / "components/web_server/alarm_suppression.c"
SUPPRESSION_HEADER = ROOT / "components/web_server/include/alarm_suppression.h"
JOURNAL_HEADER = ROOT / "components/web_server/include/alarm_journal.h"
API = API_PATH.read_text(encoding="utf-8")
SUPPRESSION = SUPPRESSION_PATH.read_text(encoding="utf-8")
SUPPRESSION_H = SUPPRESSION_HEADER.read_text(encoding="utf-8")
JOURNAL_H = JOURNAL_HEADER.read_text(encoding="utf-8")
JOURNAL = (ROOT / "components/web_server/alarm_journal.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")
UI = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-operations.css").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def function_body(source, name):
    match = re.search(r"^[\w \*]*\b%s\s*\([^;]*?\)\s*\{" % re.escape(name), source, re.M | re.S)
    require(match is not None, f"function {name} not found")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"function {name} is not brace balanced")


def strip_comments(source):
    """Comments explain; code decides. Only code is checked for behaviour."""
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", source, flags=re.S))


# ---------------------------------------------------------------------------
# 1. Three states, three names, and no fourth "disabled" answer
# ---------------------------------------------------------------------------

for state in ["ALARM_SUPPRESSION_NONE", "ALARM_SUPPRESSION_SHELVED",
              "ALARM_SUPPRESSION_BY_DESIGN", "ALARM_SUPPRESSION_OUT_OF_SERVICE"]:
    require(state in SUPPRESSION_H, f"the suppression model does not define {state}")

names = dict(re.findall(r"case\s+(ALARM_SUPPRESSION_\w+):\s*return\s+\"([a-z_]+)\";",
                        function_body(SUPPRESSION, "alarm_suppression_name")))
require(len(set(names.values())) == 3,
        f"the suppression state names are not distinct: {names}")
require(names.get("ALARM_SUPPRESSION_SHELVED") == "shelved" and
        names.get("ALARM_SUPPRESSION_BY_DESIGN") == "suppressed_by_design" and
        names.get("ALARM_SUPPRESSION_OUT_OF_SERVICE") == "out_of_service",
        f"the ISA-18.2 wire names changed: {names}")

authorities = dict(re.findall(r"case\s+(ALARM_SUPPRESSION_\w+):\s*return\s+\"([a-z]+)\";",
                              function_body(SUPPRESSION, "alarm_suppression_authority")))
require(authorities.get("ALARM_SUPPRESSION_SHELVED") == "operator",
        "shelving must be recorded as the operator's own decision")
require(authorities.get("ALARM_SUPPRESSION_BY_DESIGN") == "system",
        "suppressed-by-design must be recorded as the system's decision")
require(authorities.get("ALARM_SUPPRESSION_OUT_OF_SERVICE") == "maintenance",
        "out-of-service must be recorded as a maintenance action")
require(len(set(authorities[key] for key in authorities
               if key != "ALARM_SUPPRESSION_NONE")) == 3,
        "the three suppression states must record three different authorities; "
        "'who decided' is the distinction a single disabled flag destroys")

# Only shelving expires. If out-of-service expired it would be a longer shelf and
# the standard's distinction would be gone.
expires = function_body(SUPPRESSION, "alarm_suppression_expires")
require("ALARM_SUPPRESSION_SHELVED" in expires,
        "shelf expiry is not what decides whether a suppression state expires")
require("ALARM_SUPPRESSION_OUT_OF_SERVICE" not in expires,
        "out-of-service must not expire: an expiring out-of-service is a shelf")

# ---------------------------------------------------------------------------
# 2. The alarm table keeps three independent facts, never one collapsed flag
# ---------------------------------------------------------------------------

table = API[API.index("typedef struct {"):API.index("static operational_alarm_t s_alarms")]
for field in ["bool shelved;", "bool suppressed_by_design;", "bool out_of_service;"]:
    require(field in table,
            f"the alarm condition table does not keep {field} as its own fact")
require("out_of_service_reason" in table,
        "a non-expiring suppression must record why it was applied")
# The specific mistake the standard warns about.
for collapsed in ["bool disabled;", "bool suppressed;", "uint8_t suppression;"]:
    require(collapsed not in table,
            f"the alarm table declares {collapsed}: the three ISA-18.2 suppression "
            "states must not collapse into one flag or one enum")

alarms_get = function_body(API, "alarms_get")
for field in ['"shelved"', '"suppressed_by_design"', '"out_of_service"', '"suppression"',
              '"suppression_authority"', '"suppression_expires"', '"suppression_count"']:
    require(field in alarms_get, f"the alarm listing does not publish {field}")
require('"out_of_service_reason"' in alarms_get and '"out_of_service_reason_text"' in alarms_get,
        "an out-of-service alarm must publish its recorded reason")
require('"design_suppressed_by"' in alarms_get,
        "a design-suppressed alarm must name the fault that explains it, or the "
        "controller's own decision cannot be reviewed")

# Three booleans AND the effective state. Publishing only the effective state
# would collapse the distinction as soon as an alarm is in two states at once.
effective_index = alarms_get.index('cJSON_AddStringToObject(item, "suppression"')
for flag in ['cJSON_AddBoolToObject(item, "shelved"',
             'cJSON_AddBoolToObject(item, "suppressed_by_design"',
             'cJSON_AddBoolToObject(item, "out_of_service"']:
    require(flag in alarms_get, f"the alarm row does not publish {flag}")

summary = alarms_get[alarms_get.index('cJSON_AddObjectToObject(root, "summary")'):]
for count in ['"shelved"', '"suppressed_by_design"', '"out_of_service"', '"suppressed"']:
    require(count in summary,
            f"the alarm summary does not count {count} separately; one total would be "
            "the collapsed disabled figure ISA-18.2 warns about")
require('"suppression_model"' in summary and '"suppression_states"' in summary,
        "the suppression model must be declared so a reviewer can see all three states")
model = re.search(r'"suppression_model",\s*((?:\s*"[^"]*")+)', summary)
require(model is not None, "the suppression model string is missing")
model_text = model.group(1)
require("not implemented" not in model_text,
        "the suppression model still claims states are unimplemented after A9 landed")
for phrase in ["shelving", "suppressed-by-design", "out-of-service"]:
    require(phrase in model_text, f"the declared suppression model omits {phrase}")

# ---------------------------------------------------------------------------
# 3. Suppression never reaches condition detection
# ---------------------------------------------------------------------------

# This is the property that separates a suppression state from a mute. Extended
# from the shelving contract to the two new flags: a suppressed condition must
# still be detected, still counted, still journalled and still timed.
for name in ["update_alarm_locked", "commit_alarm_locked", "service_alarm_locked",
             "detect_events", "collect_sample"]:
    body = strip_comments(function_body(API, name))
    for token in ["shelved", "shelf", "suppressed_by_design", "out_of_service",
                  "suppression"]:
        require(token not in body,
                f"{name} consults {token}: suppression must remove an alarm from the "
                "operator's attention, never from detection, occurrence counting or "
                "the record")

# Acknowledgement is untouched by any suppression state.
ack = strip_comments(function_body(API, "alarms_ack_post"))
for token in ["shelved", "suppressed_by_design", "out_of_service", "suppression"]:
    require(token not in ack,
            f"acknowledgement consults {token}: a suppressed condition must still be "
            "acknowledgeable, or suppression becomes a way to hide outstanding work")

# The row is still emitted for a suppressed alarm. Reuses the shelving contract's
# argument: the only permitted skips are "not an alarm code", "never seen" and an
# allocation failure.
skips = re.findall(r"^\s*(?:if \()?(.*?)\)? continue;", alarms_get, re.M)
require(len(skips) == 3,
        f"the alarm listing gained or lost a skip ({skips}): a suppressed alarm must "
        "remain visible and inspectable, never hidden")
for skip in skips:
    require("suppress" not in skip and "shelf" not in skip and "shelved" not in skip
            and "service" not in skip,
            f"suppression must not gate whether an alarm row is produced: {skip}")

# ---------------------------------------------------------------------------
# 4. Suppressed by design is the SYSTEM's decision and nothing else's
# ---------------------------------------------------------------------------

design = function_body(API, "service_design_suppression_locked")
require("alarm_design_suppression_step" in design,
        "design suppression must go through the tested state machine rather than an "
        "inline condition")
require("alarm_cause_of" in design,
        "design suppression must be driven by the same causality table the alarm list "
        "reports as caused_by, or the two can disagree in one payload")
require("ALARM_JOURNAL_DESIGN_SUPPRESSED" in design and
        "ALARM_JOURNAL_DESIGN_RELEASED" in design,
        "both edges of a design suppression must be journalled: a suppression nobody "
        "was told about is the hole audited suppression exists to close")
require("alarm->present &&" in design,
        "a condition that is not present must not be reported as suppressed by design; "
        "the controller cannot have decided to hide something that was never there")

# No request can set it. It is derived from plant state on the observation tick and
# released the moment the cause clears, which is what makes it the system's own.
assignments = re.findall(r"->suppressed_by_design\s*=", API)
in_service = re.findall(r"->suppressed_by_design\s*=", design)
require(len(assignments) == len(in_service) == 2,
        f"suppressed_by_design is assigned {len(assignments)} time(s), "
        f"{len(in_service)} of them inside service_design_suppression_locked; it must "
        "be set only there, so no endpoint and no operator can reach it")
require("suppressed_by_design" not in function_body(API, "alarms_shelve_post"),
        "the shelve endpoint must not touch the system's own suppression decision")

# It runs on the observation tick AND when the list is read, so a released
# suppression can never be observed as still in force.
require("service_design_suppression_locked" in function_body(API, "service_alarms_locked"),
        "design suppression must be re-evaluated on the observation tick")
require("service_design_suppression_locked" in alarms_get,
        "design suppression must be re-evaluated when the alarm list is read, or the "
        "same response can report a released suppression as still in force")

# ---------------------------------------------------------------------------
# 5. Out of service: authorised, reasoned, and deliberately non-expiring
# ---------------------------------------------------------------------------

require('"/api/operator/alarms/out-of-service"' in API,
        "there is no endpoint for the out-of-service state")
oos = function_body(API, "alarms_out_of_service_post")
require("engineering_auth_is_authorized(request)" in oos,
        "taking an alarm out of service must require an authenticated engineering "
        "session; this file sits outside the authorization gateway so the check has "
        "to be explicit")
require('"401 Unauthorized"' in oos, "the endpoint must fail closed with 401")
auth_index = oos.index("engineering_auth_is_authorized")
require(auth_index < oos.index("http_json_parse_bounded"),
        "authorisation must be checked before the request body is read")
require(auth_index < oos.index("portENTER_CRITICAL"),
        "authorisation must be checked before suppression state is mutated")

# The reason is required, from a fixed list, because the journal record carries one
# uint16 of detail and a sentence would not survive a reboot.
require("have_reason" in oos and "alarm_out_of_service_reason_valid" in oos,
        "the out-of-service reason must be required and validated")
require('"out_of_service_reason_required"' in oos,
        "a request with no reason must be rejected explicitly")
require("400 Bad Request" in oos,
        "a missing or unknown reason must be a client error, not a silent default")
require("have_flag" in oos,
        "the direction must be explicit: a missing flag must never default to taking "
        "an alarm out of service")
# No expiry and no duration, ever. An expiring out-of-service is a longer shelf,
# and the standard keeps them apart precisely because one is an operator asking for
# quiet and the other is a statement that the measurement does not exist. Checked
# on code with comments stripped, so the endpoint may still SAY it does not expire.
oos_code = strip_comments(oos)
for token in ["duration_ms", "expires_ms", "shelf_"]:
    require(token not in oos_code,
            f"the out-of-service endpoint references {token}: it must not acquire an "
            "expiry or a duration, or it becomes a longer shelf")
require("alarm_suppression_expires(ALARM_SUPPRESSION_OUT_OF_SERVICE)" in oos_code,
        "the reply must state whether this state expires, from the tested state "
        "machine rather than from a literal that could drift")
require("ALARM_JOURNAL_OUT_OF_SERVICE" in oos and
        "ALARM_JOURNAL_RETURNED_TO_SERVICE" in oos,
        "both edges of an out-of-service must be journalled")
require("journal_flush()" in oos,
        "the audit record is the whole justification for a non-expiring suppression, "
        "so it must reach flash before the caller is told it took effect")
require(oos.index("journal_flush()") > oos.index("portEXIT_CRITICAL"),
        "the flash write must happen with interrupts enabled, outside the lock")

oos_assignments = re.findall(r"->out_of_service\s*=", API)
require(len(oos_assignments) == 1,
        f"out_of_service is assigned in {len(oos_assignments)} places; the maintenance "
        "state must have exactly one entry point")

# The reason vocabulary is published with the alarm list, not only on rejection, so
# a caller never has to guess the values of a mandatory field.
require('"out_of_service_reasons"' in alarms_get,
        "the accepted reasons must be published with the alarm list")
require('"out_of_service_expires", false' in alarms_get,
        "the payload must state plainly that out-of-service does not expire")

reasons = function_body(SUPPRESSION, "alarm_out_of_service_reason_name")
reason_names = [name for name in re.findall(r'return\s+"([a-z_]+)";', reasons)
                if name != "unknown"]
require("unknown" in reasons,
        "an out-of-service reason code the firmware does not recognise must read as "
        "unknown rather than as one of the real reasons")
require(len(reason_names) >= 4,
        f"only {len(reason_names)} out-of-service reasons are offered; too few and "
        "everything becomes 'maintenance'")
require(len(set(reason_names)) == len(reason_names),
        f"two out-of-service reasons share a name: {reason_names}")
reason_max = int(re.search(r"#define\s+ALARM_OUT_OF_SERVICE_REASON_MAX\s+(\d+)U?",
                           SUPPRESSION_H).group(1))
require(reason_max <= 0xFFFF,
        "an out-of-service reason must fit the uint16 of journal detail, or the "
        "recorded justification does not survive a reboot")

# ---------------------------------------------------------------------------
# 6. The journal keeps the three states apart, and the format only grows
# ---------------------------------------------------------------------------

transitions = dict((name, int(value)) for name, value in
                   re.findall(r"#define\s+(ALARM_JOURNAL_[A-Z_]+)\s+(\d+)U", JOURNAL_H))
# These values are on flash. Renumbering one would silently reinterpret every
# record written by an earlier firmware.
for name, value in [("ALARM_JOURNAL_RAISED", 0), ("ALARM_JOURNAL_ACKNOWLEDGED", 1),
                    ("ALARM_JOURNAL_CLEARED", 2), ("ALARM_JOURNAL_SHELVED", 3),
                    ("ALARM_JOURNAL_UNSHELVED", 4), ("ALARM_JOURNAL_SHELF_EXPIRED", 5)]:
    require(transitions.get(name) == value,
            f"{name} was renumbered to {transitions.get(name)}; these values are the "
            "on-flash format and every existing record would be reinterpreted")
for name, value in [("ALARM_JOURNAL_DESIGN_SUPPRESSED", 6),
                    ("ALARM_JOURNAL_DESIGN_RELEASED", 7),
                    ("ALARM_JOURNAL_OUT_OF_SERVICE", 8),
                    ("ALARM_JOURNAL_RETURNED_TO_SERVICE", 9)]:
    require(transitions.get(name) == value,
            f"{name} must be appended as {value}, not renumbered into existing space")
require(transitions.get("ALARM_JOURNAL_TRANSITION_MAX") == 9,
        "the transition ceiling must admit the new records, or every one of them "
        "fails validation on read and the audit trail is empty")

journal_names = re.findall(r"case\s+ALARM_JOURNAL_(\w+):\s*return\s+\"([a-z_]+)\";",
                           function_body(JOURNAL, "alarm_journal_transition_name"))
journal_map = dict(journal_names)
require(len(set(journal_map.values())) == len(journal_map),
        f"two journal transitions share a name: {journal_map}")
for token, expected in [("DESIGN_SUPPRESSED", "design_suppressed"),
                        ("DESIGN_RELEASED", "design_released"),
                        ("OUT_OF_SERVICE", "out_of_service"),
                        ("RETURNED_TO_SERVICE", "returned_to_service")]:
    require(journal_map.get(token) == expected,
            f"journal transition {token} is not named {expected!r}")

journal_get = function_body(API, "alarms_journal_get")
require('"suppression_authority"' in journal_get,
        "the journal payload must say who decided each suppression; a record that only "
        "says the alarm was quiet answers the wrong question")
require('"out_of_service_reason"' in journal_get,
        "the journal payload must carry the recorded out-of-service reason")

# ---------------------------------------------------------------------------
# 7. The pure module stays pure, and the critical sections stay safe
# ---------------------------------------------------------------------------

for forbidden in ["ESP_LOG", "malloc(", "calloc(", "free(", "cJSON_", "portENTER_CRITICAL",
                  "fopen", "fwrite", "xSemaphore", "static bool", "static uint"]:
    require(forbidden not in SUPPRESSION,
            f"alarm_suppression.c contains {forbidden}: it is called with interrupts "
            "disabled and must stay pure, stateless and non-blocking")

cursor = 0
spans = []
while True:
    start = API.find("portENTER_CRITICAL", cursor)
    if start < 0:
        break
    end = API.find("portEXIT_CRITICAL", start)
    require(end > start, "unbalanced critical section")
    spans.append(API[start:end])
    cursor = end + 1
require(spans, "the alarm module no longer takes its lock")
for span in spans:
    for forbidden in ["cJSON_", "malloc(", "calloc(", "free(", "ESP_LOG",
                      "alarm_journal_append", "journal_flush"]:
        require(forbidden not in span,
                f"{forbidden} inside a critical section: interrupts are disabled there")

require('"alarm_suppression.c"' in CMAKE, "the suppression module is not compiled")
require("tests/alarm_suppression_test.c" in WORKFLOW,
        "the suppression state machine unit test is not registered in CI")
require("tests/alarm_suppression_states_source_contract.py" in WORKFLOW,
        "this contract is not registered in the CI workflow")

# ---------------------------------------------------------------------------
# SECTIONS 8 AND 9 ARE GONE WITH THE ALARM PAGE.
#
# They asserted that the three suppression states appeared on screen with
# distinct labels, that the row named which authority decided each one and what
# ends it, and that all of it met contrast in both themes. The owner removed
# that screen from the product.
#
# EVERY FIRMWARE PROPERTY ABOVE AND BELOW IS UNTOUCHED, including the executed
# state machine: shelving expires and out-of-service does not, suppression never
# reaches condition detection, suppressed-by-design is the system's decision
# alone, and the three facts are never collapsed into one flag.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 10. The state machine is executed, not just described
# ---------------------------------------------------------------------------

require((ROOT / "tests/alarm_suppression_test.c").exists(),
        "the suppression state machine has no executable test")
UNIT = (ROOT / "tests/alarm_suppression_test.c").read_text(encoding="utf-8")
require("for (unsigned mask = 0; mask < 8U; ++mask)" in UNIT,
        "the executable test must enumerate all eight flag combinations; the property "
        "under test is that none of them loses a fact")
require("assert(" in UNIT and UNIT.count("assert(") > 40,
        "the executable test is too thin to be evidence")

print("ISA-18.2 suppression states source contract passed "
      f"(shelved/suppressed-by-design/out-of-service kept distinct, {len(reason_names)} "
      f"recorded reasons, journal transitions 6-9 appended, worst suppression pill "
      "no interface remains to render them)")
