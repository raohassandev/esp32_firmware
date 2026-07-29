"""Alarm management contract: ISA-18.2 / EEMUA 191 gaps A4, A5, A6, A7.

This is a source contract, not a runtime test. It cannot prove the firmware
behaves correctly on hardware; it proves the mechanisms the standards require
are present in the source, wired into the payload an operator reads, and not
quietly reintroducing the failure modes this module has already been bitten by
(serialising or allocating with interrupts disabled, and suppressing a real
condition instead of attributing it).
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
API_PATH = ROOT / "components/web_server/operational_api.c"
API = API_PATH.read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def define_value(name: str) -> int:
    match = re.search(r"#define\s+%s\s+(\d+)U?\b" % re.escape(name), API)
    require(match is not None, f"{name} is not defined")
    return int(match.group(1))


def function_body(name: str) -> str:
    """Return the brace-balanced body of a function definition."""
    match = re.search(r"^[\w \*]*\b%s\s*\([^;]*?\)\s*\{" % re.escape(name), API, re.M | re.S)
    require(match is not None, f"function {name} not found")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(API)):
        if API[index] == "{":
            depth += 1
        elif API[index] == "}":
            depth -= 1
            if depth == 0:
                return API[start:index + 1]
    raise AssertionError(f"function {name} is not brace balanced")


# ---------------------------------------------------------------------------
# A4: on-delay, off-delay and chatter accounting
# ---------------------------------------------------------------------------

on_delay = define_value("ALARM_ON_DELAY_MS")
off_delay = define_value("ALARM_OFF_DELAY_MS")

# The delay must exist, must not be zero (that is the unfixed behaviour), and
# must be bounded. Both standards treat on-delay as time taken directly out of
# the operator's response budget, so an unbounded "just make it bigger" value
# is a defect, not a tuning choice. 5 s is one observation interval and is the
# outer limit this contract will accept.
require(0 < on_delay <= 5000,
        f"ALARM_ON_DELAY_MS={on_delay} is unbounded or absent; on-delay is subtracted "
        "from operator response time and must stay small")
require(0 < off_delay <= 30000,
        f"ALARM_OFF_DELAY_MS={off_delay} is absent or unreasonably long")
require(off_delay >= on_delay,
        "off-delay (debounce) must not be shorter than the on-delay: clearing too "
        "eagerly is what lets a chattering condition vanish from the list")

# The chosen value must be justified in the source, not merely present, and must
# be justified against the control interval rather than picked for convenience.
delay_context = API[max(0, API.index("#define ALARM_ON_DELAY_MS") - 2000):
                    API.index("#define ALARM_ON_DELAY_MS")]
require("250 ms" in delay_context,
        "the on-delay must be justified against the 250 ms control interval in a comment")
require("response time" in delay_context,
        "the comment must record that on-delay consumes operator response time")

# Delays must be published, not hidden in firmware.
for field in ['"on_delay_ms"', '"off_delay_ms"']:
    require(field in API, f"alarm payload does not publish {field}")

# Suppressed transitions must be counted and exposed, so a chattering signal is
# visible as a diagnostic instead of being silently swallowed.
require("suppressed_transitions" in API, "chattering transitions are not counted")
require('"suppressed_transitions"' in API,
        "suppressed transition count is not exposed to the operator")

update = function_body("update_alarm_locked")
require("candidate" in update and "candidate_since_ms" in update,
        "update_alarm_locked does not track a pending candidate state")
require("suppressed_transitions++" in update,
        "a transition that reverts before its delay expires must still be counted")

service = function_body("service_alarm_locked")
require("ALARM_ON_DELAY_MS" in service and "ALARM_OFF_DELAY_MS" in service,
        "the delay timer does not distinguish on-delay from off-delay")

# A condition that simply persists produces no further event, so the timer has
# to be re-evaluated on the sampling tick or a raise would never happen.
task = function_body("operational_task")
require("service_alarms_locked" in task,
        "pending alarm delays are never re-evaluated: a persisting condition would "
        "never be raised")

# ---------------------------------------------------------------------------
# A5: root-cause grouping
# ---------------------------------------------------------------------------

cause = function_body("alarm_cause_of")
require("EVENT_NETWORK_STATE" in cause,
        "root-cause mapping does not consider the network")
# Network loss is the deepest observable cause: it must never be attributed to
# anything else, because the meter is reached over that network.
require("return ALARM_NO_CAUSE" in cause,
        "root-cause mapping has no 'this alarm is primary' outcome")
for consequential_code in ["EVENT_METER_STATE", "EVENT_METER_OFFLINE_ALARM",
                           "EVENT_METER_STALE_ALARM"]:
    require(consequential_code in cause,
            f"{consequential_code} has no cause relationship defined")

network_case = cause[cause.index("switch"):]
require("case EVENT_NETWORK_STATE:" not in network_case,
        "network loss must not be attributable to a downstream fault; it is the "
        "deepest cause this controller can observe")

for field in ['"caused_by"', '"role"', '"consequential"', '"primary"']:
    require(field in API, f"alarm payload does not express {field}")

alarms_get = function_body("alarms_get")
require('"caused_by"' in alarms_get and '"role"' in alarms_get,
        "cause attribution is not part of the alarm listing")

# Consequential alarms must still be listed and still be inspectable. The only
# permitted skips in the listing loop are "not an alarm code", "never seen" and
# an allocation failure - never "this one is only a consequence".
skips = re.findall(r"^\s*(?:if \()?(.*?)\)? continue;", alarms_get, re.M)
require(len(skips) == 3,
        f"the alarm listing gained or lost a skip ({skips}): consequential alarms "
        "must remain visible and inspectable, never hidden")
for skip in skips:
    require("consequential" not in skip and "cause" not in skip and "role" not in skip,
            f"cause grouping must not gate whether an alarm row is produced: {skip}")

# The row is emitted unconditionally once built, regardless of attribution.
require(alarms_get.index('cJSON_AddStringToObject(item, "role"') <
        alarms_get.index("cJSON_AddItemToArray(items, item)"),
        "attribution must be attached to the row, not decide whether it exists")

# Acknowledgement must remain available for every alarm code, including
# consequential ones.
ack = function_body("alarms_ack_post")
require("consequential" not in ack and "caused_by" not in ack,
        "acknowledgement must not be blocked or altered by cause grouping")

for field in ['"primary_active"', '"consequential_active"', '"primary_unacknowledged"']:
    require(field in API, f"alarm summary does not distinguish {field}")

summary = alarms_get[alarms_get.index('cJSON_AddObjectToObject(root, "summary")'):]
require('"active"' in summary and '"unacknowledged"' in summary,
        "the existing active/unacknowledged summary counts were removed")

# ---------------------------------------------------------------------------
# A6: priority rationalisation
# ---------------------------------------------------------------------------

priority = function_body("alarm_priority")
mapping = dict(re.findall(r"case\s+(EVENT_\w+):\s*return\s+(\d+);", priority))
require(mapping, "alarm_priority has no per-code mapping")

# Anything that stops the plant being controlled safely is high. The grid meter
# is reached over the site network, so losing either removes the measurement
# automatic control depends on.
for high_code in ["EVENT_NETWORK_STATE", "EVENT_METER_OFFLINE_ALARM"]:
    require(mapping.get(high_code) == "2",
            f"{high_code} must be high priority: it stops the plant being controlled "
            "safely on a measured grid value")
# Degraded-but-still-controllable conditions are medium, and a condition that is
# only the derived view of another fault must not compete with its own cause.
for medium_code in ["EVENT_METER_STALE_ALARM", "EVENT_METER_STATE"]:
    require(mapping.get(medium_code) == "1",
            f"{medium_code} must be medium priority: control degrades before it fails "
            "and it must not outrank its own root cause")

rationale = function_body("alarm_priority_rationale")
for code in ["EVENT_NETWORK_STATE", "EVENT_METER_OFFLINE_ALARM",
             "EVENT_METER_STALE_ALARM", "EVENT_METER_STATE"]:
    require(code in rationale, f"{code} has no documented priority rationale")
require(rationale.count("High:") == 2 and rationale.count("Medium:") == 2,
        "the published rationale must agree with the assigned priorities")

require('"priority"' in API and '"priority_rationale"' in API,
        "the rationalised priority and its reasoning are not exposed for review")
require('"EEMUA-191"' in API, "the priority model is not declared in the summary")

priority_comment = API[max(0, API.index("static uint8_t alarm_priority(") - 2200):
                       API.index("static uint8_t alarm_priority(")]
require("5%" in priority_comment and "80%" in priority_comment,
        "the EEMUA target distribution must be recorded alongside the assignment")

# One source of truth: the stored severity must come from the rationalised
# table, so the alarm list and its priority field cannot disagree.
require("alarm_priority((uint8_t)code)" in update,
        "stored alarm severity is not driven by the rationalised priority table")

# ---------------------------------------------------------------------------
# A7: stale alarm detection
# ---------------------------------------------------------------------------

stale_threshold = define_value("ALARM_STALE_THRESHOLD_MS")
require(stale_threshold == 86400000,
        f"ALARM_STALE_THRESHOLD_MS={stale_threshold}; the conventional stale threshold "
        "is 24 h (86400000 ms)")
require('"stale"' in API, "alarms do not expose a stale flag")
require('"stale_threshold_ms"' in API,
        "the stale threshold must be published so the judgement is auditable")
require("ALARM_STALE_THRESHOLD_MS" in alarms_get,
        "the stale flag is not evaluated in the alarm listing")

# ---------------------------------------------------------------------------
# Regressions this module has already paid for
# ---------------------------------------------------------------------------

# portENTER_CRITICAL disables interrupts. Nothing that allocates, logs or
# serialises may run inside one.
spans = []
cursor = 0
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
    for forbidden in ["cJSON_", "malloc(", "calloc(", "free(", "ESP_LOG"]:
        require(forbidden not in span,
                f"{forbidden} inside a critical section: interrupts are disabled there")

# The condition table is still updated in the same lock and instant as the
# event that produced it.
append = function_body("append_event_ex")
require("update_alarm_locked" in append and
        append.index("portENTER_CRITICAL") < append.index("update_alarm_locked") <
        append.index("portEXIT_CRITICAL"),
        "the alarm table must be updated inside the same lock as its event")

# ISA-18.2 state names, RTN Unacknowledged retention, and outstanding work.
for token in ['"rtn_unacknowledged"', '"unacknowledged"', '"acknowledged"', '"normal"',
              '"ISA-18.2"']:
    require(token in alarms_get or token in API,
            f"ISA-18.2 alarm state reporting lost {token}")
commit = function_body("commit_alarm_locked")
require("alarm->acknowledged = false" in commit,
        "a recurrence must clear a previous acknowledgement")
require("alarm->acknowledged" not in commit.split("} else {")[1],
        "clearing a condition must not touch the acknowledged flag: RTN "
        "Unacknowledged retention depends on it")
require("if (!a->acknowledged)" in alarms_get,
        "the outstanding count must measure work to look at, not live conditions only")

# Acknowledgement still requires an authenticated session.
require("engineering_auth_is_authorized(request)" in ack,
        "acknowledgement must require an authenticated engineering session")

# Conditions present at boot are still raised exactly once, and bypass the
# on-delay so a controller that boots into a fault is not silent.
detect = function_body("detect_events")
boot_scan = detect[:detect.index("s_observed = *next;")]
require(boot_scan.count("append_event_ex(") == 4,
        "conditions present at the first sample must still be raised once")
require(boot_scan.count(", true);") == 4,
        "boot-present conditions must bypass the on-delay; there is no transition to "
        "debounce and deferring them leaves a faulted controller reporting nothing")

# Endpoint surface unchanged.
for endpoint in ['"/api/operator/alarms"', '"/api/operator/alarms/ack"']:
    require(endpoint in API, f"missing alarm endpoint {endpoint}")

# The contract runs in CI.
require("tests/alarm_management_source_contract.py" in WORKFLOW,
        "the alarm management contract is not registered in the CI workflow")

print("Alarm management source contract passed "
      f"(on-delay {on_delay} ms, off-delay {off_delay} ms, stale threshold "
      f"{stale_threshold // 3600000} h)")
