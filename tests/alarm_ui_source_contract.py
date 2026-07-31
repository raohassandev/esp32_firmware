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

import re
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


# ------------------------------------------ what triage may NOT be hidden behind
#
# Roughly half this screen was moved behind two closed <details> drawers by the
# prose reduction. The cut was right in size and mostly right in target -- the
# lifecycle lesson, the priority rationalisation and the per-condition metadata
# are reference material and belong one level down. Three things are not, and
# this block is what stops a future prose pass from taking them with it.
#
# Asserted structurally, on comment-stripped source, by position relative to the
# details() calls in the same function. A drawer is created by details(), so
# "before the first details() in this function" is literally "on the first
# screen", and no wording in a comment can satisfy it.
CODE = re.sub(r"(?m)^\s*//.*$", "", re.sub(r"/\*.*?\*/", "", UI, flags=re.S))


def body(name, code=None):
    source = CODE if code is None else code
    start = source.index(f"function {name}(")
    return source[start:source.index("\n    function ", start + 1)]


row = body("alarmRow")
# A drawer is created by details() or by its badge-free twin levelledDetails().
DRAWER = re.compile(r"\b(?:details|levelledDetails)\(")
_drawers = list(DRAWER.finditer(row))
require(_drawers, "the alarm row no longer discloses anything")
first_drawer = _drawers[0].start()

# Exactly ONE drawer per row. Every row used to mount a full-width condition
# history panel AND a full-width shelving panel, so one condition cost 238px and
# four filled the screen twice over. A triage list is read by running down it;
# it stops being a list when each entry is a stack of engineering panels.
require(len(_drawers) == 1,
        f"the alarm row mounts {len(_drawers)} drawers; a triage row gets one, "
        "or the list becomes a stack of panels nobody scrolls")

# 1. The state pill. On an unattended site "Returned to normal, never
#    acknowledged" is the state that matters most, and it must be legible
#    without opening anything.
require("alarm-state-pill" in row and row.index("alarm-state-pill") < first_drawer,
        "the alarm state pill has been moved behind a disclosure; the "
        "returned-to-normal state must be readable without opening a drawer")

# 2. The obligation carried by that state. Every other state reads correctly
#    from the pill alone; this is the one where the condition is gone and the
#    work is not.
#
#    It used to be printed as a paragraph on every returned row. That is four
#    identical paragraphs on the live controller - a site that restarts returns
#    every condition at once, so the repetition is the normal case - and a
#    sentence repeated four times is not read four times. It is now stated ONCE
#    above the list and carried on every pill.
#
#    These assertions are stronger than the per-row paragraph they replace,
#    because they constrain the console as well as the row: the sentence must be
#    on the row's pill, it must be on the first screen of the console, and it
#    must NOT be inside a disclosure control.
require("pill.title = meta.meaning" in row and row.index("pill.title") < first_drawer,
        "the state pill must carry its own meaning on the row; the obligation "
        "behind 'Returned to normal · never acknowledged' cannot be reachable "
        "only by opening a drawer")

console = body("renderAlarmConsole")
# Built AND appended, and appended before the list it explains. Naming the
# function is not enough: a legend that is computed and dropped is a legend the
# operator never sees.
require("const legend = alarmStateLegend(visible);" in console,
        "the screen must build the state legend from the visible conditions")
require("if (legend) view.append(legend);" in console,
        "the state legend must be appended to the screen, not merely computed")
_legend_at = console.index("if (legend) view.append(legend);")
require("view.append(list);" in console and _legend_at < console.index("view.append(list);"),
        "the state legend must come before the list it explains")
_console_drawers = list(DRAWER.finditer(console))
require(_console_drawers and _legend_at < _console_drawers[0].start(),
        "the state legend must be on the first screen, not behind a disclosure "
        "control; the returned-to-normal obligation is why this screen exists")

legend = body("alarmStateLegend")
require("ALARM_STATES[key]" in legend and "entry.meaning" in legend,
        "the legend must draw its wording from ALARM_STATES, so the legend, the "
        "tooltip and the lifecycle reference cannot drift into three different "
        "explanations of the same state")
require("alarm-state-pill" in legend,
        "the legend must show the pill it is explaining, not just its wording")
# Only states actually on screen. A legend explaining four states when the list
# shows one is the same defect at a different scale.
require("visible" in legend and "seen[key]" in legend,
        "the legend must explain only the states present in the visible list")

# 3. The required action the controller wrote.
require("recommended_action" in row and row.index("recommended_action") < first_drawer,
        "the controller's recommended action must stay on the first screen")

# 4. Acknowledge stays a plain button on the row, needing no session. ISA-18.2
#    assigns acknowledgement to the operator; burying it behind the Engineering
#    level is what stopped anything from ever being acknowledged.
require("acknowledgeControl(alarm)" in row,
        "the acknowledge control must stay on the row")
require(row.index("acknowledgeControl(alarm)") > first_drawer
        or "alarm-actions" in row,
        "acknowledge must be a row control, not a drawer item")
ack = body("acknowledgeControl")
require("engineeringAuthorized" not in ack,
        "acknowledgement must not be gated on an engineering session in the "
        "browser; it is the operator's action under ISA-18.2")

# 5. The ENGINEERING badge is said once per section, not once per drawer. The
#    level itself is never lost: the badge-free drawer keeps it in the class the
#    stylesheet colours it with, in its accessible name and in its tooltip.
quiet = body("levelledDetails")
require("op-more-level" not in quiet,
        "the per-row drawer must not repeat the level badge")
require("level-${level}" in quiet and "aria-label" in quiet and ".title =" in quiet,
        "a badge-free drawer must still carry its level in the class, the "
        "accessible name and the tooltip")
require("levelNote" in console,
        "the screen must state the engineering level once for the list")

# The suppression pill changes what the counts above mean, so it stays on the
# row even though the reasoning behind it is disclosed.
suppression = body("suppressionBlock")
_suppression_drawer = DRAWER.search(suppression)
require("alarm-suppression-pill" in suppression and _suppression_drawer is not None
        and suppression.index("alarm-suppression-pill") < _suppression_drawer.start(),
        "the suppression pill must stay on the row; hiding it makes the triage "
        "counts above misleading")

# Alarm load: the headline is triage, the EEMUA evidence is not. An operator
# scanning a long list has to be able to tell twenty faults from one flood.
tiles = body("alarmSummaryTiles")
require("alarmLoadTile" in tiles,
        "the alarm rate headline is not on the first screen, so a flood is only "
        "discoverable by opening the performance drawer")
require("details(" not in tiles, "the summary tiles must not be disclosed")
load = body("alarmLoadTile")
# The honesty rules that govern the full panel govern its headline too: no
# verdict before the window has elapsed, and the peak is never a pass.
require("steady_window_observed" in load,
        "the load headline must gate its verdict on the controller's own "
        "'window observed' flag rather than extrapolating one")
require("Not yet measured" in load or "not yet measured" in load,
        "before the steady-state window has elapsed the headline must say so")
require("peak_target_breached" in load,
        "the load headline ignores the peak breach flag")
require("'good'" in load and load.index("peak_target_breached") < load.index("'good'"),
        "a breached peak must be reported before any pass verdict can be reached")

# And the evidence is still one level down, not promoted with it. Reference
# material is built into a local first and only then wrapped, so position in the
# function proves nothing; what proves it is that the only route these take onto
# the page is a view.append() whose argument is a details() call.
console = body("renderAlarmConsole")


def appends(source, sink):
    """Every `sink.append(...)` call in source, with balanced parentheses."""
    calls, cursor = [], 0
    opener = f"{sink}.append("
    while True:
        start = source.find(opener, cursor)
        if start < 0:
            return calls
        depth, index = 0, start + len(opener) - 1
        while index < len(source):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        calls.append(source[start:index + 1])
        cursor = index + 1


view_appends = appends(console, "view")
require(len(view_appends) >= 5,
        "the alarm console no longer assembles its screen through view.append()")
for reference in ("alarmRateSection", "alarmRationalisationSection", "ALARM_STATES"):
    require(reference in console, f"{reference} has been dropped from the alarm screen")
for call in view_appends:
    for reference in ("alarmRateSection", "alarmRationalisationSection", "lifecycle"):
        if reference not in call:
            continue
        require("details(" in call,
                f"{reference} reaches the first screen without a disclosure; the "
                "lifecycle lesson, the EEMUA evidence and the priority "
                "rationalisation are reference material and the prose reduction "
                "is being undone rather than rebalanced")
# The lifecycle glossary is what that local is built from, so it travels with it.
require("Object.keys(ALARM_STATES)" in console and "lifecycle.append" in console,
        "the state glossary is no longer assembled into the disclosed lifecycle "
        "block, so the assertion above no longer covers it")


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
# suppressionControls() is gone: it existed only to wrap suppressionActions() in
# a second per-row drawer, and the row now mounts one. The gate it carried did
# not move -- it is in suppressionActions() itself, which is what the drawer
# renders, so the branch is on the controls rather than on the wrapper.
require("function suppressionControls(" not in CODE,
        "the second per-row drawer wrapper is back")
sup_fn = body("suppressionActions")
require("engineeringAuthorized()" in sup_fn,
        "the suppression controls must still branch on engineering access")
# And the asymmetry itself: shelving is Engineering-levelled, acknowledgement is
# not. Stated on the structure rather than on the deleted wrapper's name.
require("levelledDetails('engineering'" in row,
        "the row's drawer, which holds the suppression controls, must carry the "
        "engineering level")
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
