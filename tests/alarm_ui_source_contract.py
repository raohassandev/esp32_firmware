"""The alarm management screen must exist and must be honest about alarm state.

P0-5b. The firmware has served a complete ISA-18.2 condition table at
GET /api/operator/alarms since the condition-table work landed, and nothing in
the interface displayed it. An external industrial-UI audit scored the product
63/100 and named the alarm screen a release blocker: no severity, no alarm
identifier, no first or last occurrence, no duration, no occurrence count, no
acknowledgement state, no filtering, no sorting and no acknowledge action.

Two properties in here are safety semantics rather than presentation, and both
come from docs/ALARM_MANAGEMENT_RESEARCH.md:

  * "rtn_unacknowledged" is a condition that CLEARED ITSELF while nobody had
    accepted it. On an unattended PV-DG site that is the ordinary shape of a
    real fault - something stumbled at 03:00 and recovered - and it is the most
    important row on the page. It must stay visible, must count as outstanding,
    and must never be drawn as resolved (gap A1).

  * The controller has no operator identity model. It can record that an
    authenticated engineering session acknowledged a condition; it cannot say
    who. A UI that renders "acknowledged by <name>" would put a fact into the
    incident record that the device never had (gap A8).

Acknowledgement also requires an authenticated engineering session, so the
screen must not offer a control that can only ever return 401.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "web/operator-operations.js").read_text(encoding="utf-8")
CSS = (ROOT / "web/operator-operations.css").read_text(encoding="utf-8")
API = (ROOT / "components/web_server/operational_api.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ------------------------------------------------- the screen exists and is wired

require('page.dataset.page = \'alarms\'' in UI or 'dataset.page = "alarms"' in UI,
        "there is no alarms route/page")
require("alarmConsole" in UI and "renderAlarmConsole" in UI,
        "the alarm condition screen is not built")
require('"/api/operator/alarms"' in API and '"/api/operator/alarms/ack"' in API,
        "the firmware alarm endpoints this screen depends on are missing")
require("'/api/operator/alarms'" in UI,
        "the alarm screen does not read the controller alarm condition table")
require("'/api/operator/alarms/ack'" in UI,
        "the alarm screen offers no acknowledge action")

# The acknowledge request must name a single condition. A blanket "clear all"
# is precisely the habit alarm management exists to prevent.
require("JSON.stringify({ code: Number(alarm.code) })" in UI,
        "acknowledgement must name one alarm code, not acknowledge in bulk")
require("method: 'POST'" in UI, "acknowledgement must be a POST")


# ----------------------------------------- every field the audit found missing

for field in [
    "severity",
    "first_raised_age_ms",
    "last_raised_age_ms",
    "duration_ms",
    "occurrences",
    "acknowledged",
    "acknowledged_age_ms",
    "present",
]:
    require(field in UI, f"the alarm screen does not surface {field}")

for label in [
    "First occurrence",
    "Last occurrence",
    "Duration",
    "Occurrences",
    "Acknowledged",
]:
    require(label in UI, f"the alarm row does not label {label}")

require("alarm.id" in UI, "the alarm identifier (for example MTR-002) is not shown")


# ------------------------------------------------------- filtering and sorting

require("alarmFilterState" in UI and "alarmFilterSeverity" in UI,
        "the alarm screen must filter by state and by severity")
require("'Active only'" in UI, "an active-only filter is required")
require("'Outstanding only (unacknowledged)'" in UI,
        "an outstanding-work filter is required")
for severity in ["'critical'", "'warning'", "'information'"]:
    require(severity in UI, f"severity filtering is incomplete: {severity}")
require("alarmSort" in UI and "SEVERITY_RANK" in UI,
        "the alarm screen must sort by severity")
for sort_key in ["'recent'", "'first'", "'duration'"]:
    require(sort_key in UI, f"time-based sorting is incomplete: {sort_key}")
require("function filteredAlarms" in UI,
        "filtering and sorting must be a single reviewable function")


# ------------------------------------------- ISA-18.2: rtn_unacknowledged is work

require("rtn_unacknowledged" in UI,
        "the returned-to-normal-unacknowledged state is not handled")
require("ISA-18.2" in UI, "the alarm state model is not declared")

states = UI[UI.index("const ALARM_STATES"):UI.index("const SEVERITY_RANK")]
require("rtn_unacknowledged" in states,
        "rtn_unacknowledged has no state description")
require("cleared itself" in states,
        "the UI must explain that a returned-to-normal alarm cleared itself")
require("Returned to normal" in states,
        "the returned-to-normal state must be named in operator language")
require("normal" in states and "not present and has been acknowledged" in states.lower(),
        "the genuinely resolved state must be distinguished from rtn_unacknowledged")

# Outstanding is "nobody accepted it", not "something is wrong now". Those two
# differ exactly on rtn_unacknowledged, so the predicate must key on
# acknowledgement rather than presence.
outstanding = UI[UI.index("function isOutstanding"):UI.index("function isActive")]
require("acknowledged !== true" in outstanding,
        "outstanding work must be defined by acknowledgement, not by presence")
require("state.filterState === 'outstanding') return isOutstanding(alarm)" in UI,
        "the outstanding filter must retain returned-to-normal conditions")

# A returned-but-unacknowledged row must not be styled as resolved.
require(".alarm-returned" in CSS, "returned-to-normal rows have no distinct styling")
require("opacity" not in CSS.split(".alarm-console", 1)[1],
        "an unacknowledged condition must never be faded out as if resolved")

# The navigation badge counts outstanding work, so an overnight fault that
# cleared itself is still visible from every page.
badge = UI[UI.index("function updateAlarmBadge"):]
require("isOutstanding" in badge or "summary.unacknowledged" in badge,
        "the alarm badge must count outstanding work, not only live conditions")


# --------------------------------- acknowledging never clears, and never invents

require("It never clears it" in UI,
        "the UI must state that acknowledging does not clear a condition")
require("still counts as active" in UI,
        "the UI must state that an acknowledged, still-present alarm stays active")

# No operator identity model exists. Nothing may claim one.
#
# `acknowledged_by` is now a REAL field and is deliberately not on this list: since
# acknowledgement is open to operators, the journal records which CLASS acted
# (operator vs engineering session). That is a fact the controller genuinely has.
# What it still does not have is a PERSON, so the inventions below stay banned --
# including the camelCase spelling, which no endpoint emits and could therefore
# only come from the UI making something up.
for invention in [
    "acknowledgedBy",
    "Acknowledged by user",
    "Acknowledged by operator ",
    "operator_name",
    "acknowledged_user",
    "username",
]:
    require(invention not in UI,
            f"the UI claims an acknowledging identity the controller does not have: {invention}")

# And the class that IS recorded must be read from the response, never assumed. The
# bug this prevents is real: before acknowledgement was opened to operators the UI
# hard-coded "by an authenticated engineering session", which would now misattribute
# every operator acknowledgement to engineering.
require("alarm.acknowledged_by" in UI,
        "the UI must read the recorded actor class rather than assuming one; "
        "assuming 'engineering session' would misattribute every operator "
        "acknowledgement now that operators may acknowledge")
require("records no operator identity" in UI,
        "the UI must say plainly that the controller cannot record who acknowledged")


# ------------------------------------------------- authentication is handled well

require("engineeringAuthorized" in UI and "AutomatrixEngineeringAccess" in UI,
        "the suppression controls must consult the shared engineering access state")

# The UI must mirror the backend asymmetry, or the two disagree and one of them is
# lying to the operator.
#
# ACKNOWLEDGE: no gate. It previously offered "Sign in to acknowledge", which was
# the front end of a restriction that made the returned-to-normal state impossible
# for the only person normally on site to discharge. Both halves were removed
# together; a UI that still hid the button would leave the endpoint reachable but
# undiscoverable, which is the same defect wearing a different hat.
require("Sign in to acknowledge" not in UI,
        "acknowledgement is no longer credential-gated, so the UI must not send an "
        "operator to a sign-in page to do it")
require("Acknowledging requires an engineering session" not in UI,
        "the UI must not tell an operator that acknowledgement needs a session; it "
        "does not, and the claim would stop them doing the one thing ISA-18.2 "
        "assigns to them")

# SUPPRESS: still gated, and still without dead buttons.
require("Shelving and out-of-service are engineering actions" in UI,
        "shelving and out-of-service must still be presented as engineering actions "
        "-- they hide a live condition, which acknowledgement does not")
ack_fn = UI[UI.index("function acknowledgeControl("):UI.index("function suppressionBlock(")]
require("engineeringAuthorized()" not in ack_fn,
        "the acknowledge control must not branch on engineering access at all")
sup_fn = UI[UI.index("function suppressionControls("):]
require("engineeringAuthorized()" in sup_fn,
        "the suppression controls must still branch on engineering access")
require("error.status === 401" in UI,
        "the acknowledge POST must handle 401 explicitly")
require("requires an authenticated engineering session" in UI,
        "the 401 path must explain that an engineering session is required")
require("Nothing was changed." in UI,
        "a failed acknowledgement must say that the condition is unchanged")
require("engineering_authentication_required" in API,
        "the firmware no longer gates acknowledgement; the UI copy would be wrong")


# -------------------------------------------------------- styling and hit targets

require(".alarm-row" in CSS and ".alarm-state-pill" in CSS,
        "alarm condition styling is missing")
require("min-height: 44px" in CSS,
        "alarm controls must meet the 44px minimum tap target")
console_css = CSS.split(".alarm-console", 1)[1]
require("#" not in console_css.replace("#/", ""),
        "alarm styling must resolve every colour through a theme token, not hardcoded hex")
require('html[data-theme="light"] .alarm-row' in CSS,
        "the alarm table has no light-theme surface, which is how S5 produced unreadable text")


# ------------------------------------------------------------ CI registration

require("tests/alarm_ui_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")

print("alarm management UI source contract: PASS")
