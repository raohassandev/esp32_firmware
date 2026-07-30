"""Lab-simulator authority, setpoint confirmation and the commissioning gate.

The real 100 kW inverters are 2000 miles away, so the control loop is validated
against a Modbus TCP lab simulator. The firmware can have a specific inverter
endpoint DECLARED a simulator, which grants command authority through a profile
that has NOT been qualified on physical equipment; the commissioning gate then
reports scope "lab_simulator_only" rather than "production".

Four properties in here are safety semantics rather than presentation.

  1. LAB MODE IS UNMISTAKABLE. While lab_simulator_mode holds, a banner is part
     of the SHELL, above the routed content, so there is no page a user can be
     standing on where "commands are going to a declared simulator, this is not
     production control, physical qualification has not been performed" is off
     screen. The controller's own wording (lab_simulator_notice, scope_notice) is
     rendered verbatim; this interface must not invent safety copy.

  2. THE FOUR WRITE-CONFIRMATION STATES ARE FOUR STATES. confirmed means a
     post-write readback matched. mismatched means a readback disagreed, which
     the firmware treats as a fault and answers with a safe zero. pending means
     the transport accepted the write and nothing has confirmed it - a real
     inverter defers a setpoint by over a second, so pending must NOT look like
     success. unverified means confirmation is impossible or never arrived -
     neither success nor failure. Requested and confirmed percent are separate
     figures and are never merged: on a controller that writes power limits,
     "45%" with no statement of whether that is what was asked or what came back
     is the defect.

  3. THE GATE IS EXPLAINED, NOT SUMMARISED. All nine prerequisites are listed
     with the firmware's own title, machine reason code and explanatory sentence,
     so an engineer can see which one is unmet and why without reading a log.
     control_authority.mode_label and inhibit_reason are shown exactly as
     received.

  4. DECLARING A LAB TARGET STATES ITS CONSEQUENCES FIRST. It enables commands
     through an unqualified profile AND it disables automatic control, which the
     firmware does deliberately. Both are said before the engineer confirms, and
     the returned write_permission_after_restart and lab_target_notice are shown
     as received rather than interpreted.

Contrast is computed here from the PARSED token values in both themes rather
than asserted by eye: two of this codebase's shipped defects were 1.10:1 and
1.17:1 text produced by a hardcoded surface meeting a themed foreground, and
--muted-2 measures only 4.39:1 dark / 3.56:1 light on the new panel surface,
which is why it is banned from these blocks rather than "probably fine".
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")
INDEX = (ROOT / "web/index.html").read_text(encoding="utf-8")
MODE = (ROOT / "web/product-mode.js").read_text(encoding="utf-8")
GATE_API = (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8")
SOLAR_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
PROFILE_API = (ROOT / "components/web_server/inverter_profile_api.c").read_text(encoding="utf-8")
GATE_CORE = (ROOT / "components/commissioning_gate/commissioning_gate.c").read_text(encoding="utf-8")
CONFIRM_CORE = (ROOT / "components/inverter_manager/inverter_write_confirmation.c").read_text(encoding="utf-8")
WEB_API = (ROOT / "components/web_server/web_api.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# ============================================================ token parsing
#
# Both themes are read from the stylesheets themselves. A token that exists in
# one theme only cannot pass, because the lookup below simply will not find it.

TOKEN_PATTERN = re.compile(r"(--[a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{6})\s*;")


def token_block(sheet: str, opener: str) -> str:
    require(opener in sheet, f"stylesheet has no {opener!r} block to read tokens from")
    return sheet.split(opener, 1)[1].split("\n}", 1)[0]


DARK = dict(TOKEN_PATTERN.findall(token_block(APP_CSS, ":root {")))
LIGHT = dict(TOKEN_PATTERN.findall(token_block(THEME_CSS, 'html[data-theme="light"] {')))


def channel(value: int) -> float:
    fraction = value / 255.0
    return fraction / 12.92 if fraction <= 0.03928 else ((fraction + 0.055) / 1.055) ** 2.4


def luminance(hex_colour: str) -> float:
    text = hex_colour.lstrip("#")
    red, green, blue = (int(text[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * channel(red) + 0.7152 * channel(green) + 0.0722 * channel(blue)


def contrast(first: str, second: str) -> float:
    high, low = sorted((luminance(first), luminance(second)), reverse=True)
    return (high + 0.05) / (low + 0.05)


def resolved(theme: dict, name: str, theme_name: str) -> str:
    require(name in theme, f"{name} is not declared as a literal colour for the {theme_name} theme")
    return theme[name]


def check_contrast(name: str, foreground: str, background: str, minimum: float) -> None:
    for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
        ratio = contrast(resolved(theme, foreground, theme_name),
                         resolved(theme, background, theme_name))
        require(ratio >= minimum,
                f"{theme_name} theme: {name} measures {ratio:.2f}:1 on {background}, "
                f"below the required {minimum}:1")


# Text under 18px must reach 4.5:1. Every string in the lab banner and in the two
# new panels is 10-16px, so 4.5:1 applies to all of it.
check_contrast("lab banner title and body", "--lab-text", "--lab-surface", 4.5)
check_contrast("lab banner secondary copy", "--lab-muted", "--lab-surface", 4.5)
check_contrast("lab scope pill text", "--lab-pill-text", "--lab-pill-surface", 4.5)
# The banner's rule is the boundary that identifies it, and it butts onto the
# page background as well as its own surface. 3:1 in both directions.
check_contrast("lab banner rule against its surface", "--lab-line", "--lab-surface", 3.0)
check_contrast("lab banner rule against the page", "--lab-line", "--bg", 3.0)

for token in ("--confirm-confirmed", "--confirm-pending",
              "--confirm-mismatched", "--confirm-unverified",
              "--gate-met", "--gate-unmet"):
    check_contrast(f"{token} state text", token, "--confirm-surface", 4.5)
check_contrast("panel body text", "--text", "--confirm-surface", 4.5)
check_contrast("panel secondary text", "--muted", "--confirm-surface", 4.5)
check_contrast("panel boundary", "--gate-boundary", "--confirm-surface", 3.0)

# The state colours must actually be four different colours in both themes; two
# states sharing a value is two states the reader cannot separate.
for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
    values = [theme[token] for token in ("--confirm-confirmed", "--confirm-pending",
                                         "--confirm-mismatched", "--confirm-unverified")]
    require(len(set(values)) == 4,
            f"{theme_name} theme: the four write-confirmation states do not have four distinct colours")
    # Pending must not be the success colour. That substitution is precisely how
    # "the transport accepted it" becomes "it worked".
    require(theme["--confirm-pending"] != theme["--confirm-confirmed"],
            f"{theme_name} theme: pending is painted with the confirmed colour")


# ================================================== 1. lab mode is unmistakable

require('id="labSimulatorBanner"' in INDEX, "there is no lab simulator banner")
banner_index = INDEX.index('id="labSimulatorBanner"')
require(INDEX.index('<main class="content"') > banner_index,
        "the lab banner must sit in the shell, above the routed content, so it is "
        "present on every route rather than buried on one page")
require(INDEX.index('class="app-shell"') < banner_index,
        "the lab banner must be inside the application shell")
require('data-page=' not in INDEX[:banner_index],
        "the lab banner must not be placed inside a routed page")

# The three facts the banner has to carry.
banner = INDEX[banner_index:INDEX.index("</aside>", banner_index)]
require("not production control" in banner,
        "the banner does not say that this is not production control")
require("declared Modbus simulator" in banner,
        "the banner does not say that commands go to a declared simulator")
require("Physical qualification on real equipment has not been performed" in banner,
        "the banner does not say that physical qualification has not been performed")
require('id="labSimulatorNotice"' in banner and 'id="labSimulatorScopeNotice"' in banner,
        "the banner has nowhere to render the controller's own notices")
require('id="labSimulatorScope"' in banner,
        "the banner does not show the commissioning scope the controller published")

# Both firmware notices are rendered verbatim, and neither is rewritten.
require("function renderLabBanner" in APP, "nothing renders the lab banner")
lab_banner = APP[APP.index("function renderLabBanner"):APP.index("function gateItemElement")]
require("solar?.lab_simulator_notice" in lab_banner,
        "lab_simulator_notice is not rendered")
require("gate?.scope_notice" in lab_banner,
        "scope_notice is not rendered")
require("setNoticeLine" in lab_banner,
        "the controller's notices must be written as received, not composed into a sentence")
require("gate?.lab_simulator_mode === true || solar?.lab_simulator_mode === true" in lab_banner,
        "the banner must appear when either publisher reports lab_simulator_mode")
# A hidden banner is not evidence that the plant is real. Nothing may invent
# wording for the lab state or translate the scope into a friendlier word.
for invention in ("LAB_NOTICE_TEXT", "labNoticeText", "SCOPE_LABELS",
                  "describeScope", "'Production'", "friendlyScope"):
    require(invention not in APP,
            f"the interface invents or paraphrases lab-scope wording: {invention}")

# role="alert" is only safe if the text is not rewritten on every poll: a screen
# reader would otherwise re-announce it several times a minute.
require('role="alert"' in banner, "the lab banner is not announced when it appears")
require("function setTextIfChanged" in APP and "function setNoticeLine" in APP,
        "the banner writes text unconditionally, so role=alert would re-announce it")
require("if (node.textContent !== text) node.textContent = text;" in APP,
        "banner text must only be written when it actually changed")

# The firmware still publishes what the banner claims it publishes.
require('cJSON_AddStringToObject(root, "lab_simulator_notice"' in SOLAR_API,
        "the firmware no longer publishes lab_simulator_notice")
require('cJSON_AddStringToObject(root, "scope_notice"' in GATE_API,
        "the firmware no longer publishes scope_notice")
require('cJSON_AddBoolToObject(root, "lab_simulator_mode"' in SOLAR_API,
        "/api/solar-grid/status no longer publishes lab_simulator_mode")
require('cJSON_AddBoolToObject(root, "lab_simulator_mode",' in GATE_API,
        "/api/commissioning/gate no longer publishes lab_simulator_mode")
require('return "lab_simulator_only";' in GATE_CORE,
        "the lab scope slug the banner displays has changed")

# Styling resolves through tokens only. A hardcoded hex here is how a themed
# foreground ended up on an unthemed surface twice before.
lab_css = APP_CSS[APP_CSS.index("/* ====================================================== lab simulator banner"):
                  APP_CSS.index("/* Measurement provenance")]
require("#" not in lab_css,
        "the new banner and panel styling must resolve every colour through a "
        "theme token, not a hardcoded hex")
# Declarations only: the comments in this block name --muted-2 in order to record
# why it is excluded.
lab_declarations = re.sub(r"/\*.*?\*/", "", lab_css, flags=re.S)
require("--muted-2" not in lab_declarations,
        "--muted-2 measures 4.39:1 dark and 3.56:1 light on --confirm-surface and "
        "must not carry text in these panels")
require(".lab-simulator-banner[hidden] { display: none; }" in lab_css,
        "the banner is not hidden when the controller is not in lab mode")


# ================================================ 2. write confirmation display

require('id="writeConfirmationPanel"' in INDEX, "there is no setpoint confirmation panel")
require("'/api/inverters/write-confirmation'" in APP,
        "nothing reads the write-confirmation endpoint")
require("const WRITE_CONFIRMATION_STATES = Object.freeze({" in APP,
        "there is no closed vocabulary for the confirmation states")

states = APP[APP.index("const WRITE_CONFIRMATION_STATES"):APP.index("function writeStateMeta")]
for name, label in (("confirmed", "'Confirmed'"), ("pending", "'Pending'"),
                    ("unverified", "'Unverified'"), ("mismatched", "'Mismatched'")):
    require(f"{name}: Object.freeze({{" in states, f"confirmation state missing: {name}")
    require(label in states, f"confirmation state {name} is unlabelled")

# Each state says what it actually means, in terms of what the firmware does.
require("matched the requested setpoint" in states,
        "confirmed does not say that a readback matched")
require("This is not success" in states,
        "pending must state plainly that it is not success")
require("longer than a second" in states,
        "pending does not explain that a real inverter defers a setpoint")
require("treats this as a fault" in states,
        "mismatched must read as the fault it is")
require("safe fallback" in states,
        "mismatched does not state the consequence the firmware applies")
require("neither success nor failure" in states,
        "unverified must be neither success nor failure")
require("no manual-verified readback register" in states,
        "unverified does not explain that confirmation can be impossible")

# The firmware semantics those sentences describe.
require("return verdict(INVERTER_WRITE_CONFIRMED, false, true);" in CONFIRM_CORE,
        "a matching readback no longer produces CONFIRMED; the copy would be wrong")
require("return verdict(INVERTER_WRITE_MISMATCHED, true, true);" in CONFIRM_CORE,
        "a mismatch no longer demands a safe zero; the copy would be wrong")
require('case INVERTER_WRITE_PENDING: return "pending";' in CONFIRM_CORE,
        "the confirmation state slugs this panel keys on have changed")
require("An empty or absent fleet is unverified" in CONFIRM_CORE,
        "the empty-fleet rule the panel states has changed")

# Requested, confirmed and the raw readback are three separate figures.
require("'Requested'" in APP and "'Confirmed'" in APP and "'Last readback'" in APP,
        "requested, confirmed and readback are not shown as separate values")
require("entry.requested_percent" in APP and "entry.confirmed_percent" in APP
        and "entry.readback_percent" in APP,
        "the panel does not read the three separate percent fields")
require("What the controller wrote." in APP,
        "the requested figure does not say what it is")
require("Only set when a readback matched." in APP,
        "the confirmed figure does not say what it is")
# Merging them, or substituting one for the other, is the defect.
for invention in ("requested_percent ||", "confirmed_percent ||",
                  "requested_percent ?? entry.confirmed_percent",
                  "confirmed_percent ?? entry.requested_percent"):
    require(invention not in APP,
            f"requested and confirmed percent must never fall back to one another: {invention}")
require("function formatPercent" in APP, "percent values are not formatted in one place")
percent = APP[APP.index("function formatPercent"):APP.index("function setTextIfChanged")]
require("value == null || !Number.isFinite(number) ? absent" in percent,
        "a missing percent must stay missing, not be coerced to a number")

# The firmware publishes requested and confirmed separately, deliberately.
require('add_finite(item, "requested_percent", data.requested_percent)' in GATE_API,
        "the firmware no longer reports requested_percent separately")
require('add_finite(item, "confirmed_percent", data.commanded_percent)' in GATE_API,
        "the firmware no longer reports confirmed_percent separately")

# Presentation: pending is not the success treatment, mismatched is a fault,
# unverified is dashed, and colour is never the only channel.
require(".confirm-state-pending { color: var(--confirm-pending); border-color: var(--confirm-pending); }" in lab_css,
        "pending has no distinct treatment of its own")
require(".confirm-state-mismatched" in lab_css and "border-width: 2px" in lab_css,
        "a mismatched setpoint is not drawn as the fault it is")
require("border-style: dashed" in lab_css,
        "unverified must be visibly 'not measurable', as unmeasured nodes are elsewhere")
require("opacity" not in lab_css,
        "no confirmation state may be faded out; an unconfirmed setpoint is not resolved")
require("mark: '⋯'" in states and "mark: '!'" in states and "mark: '?'" in states
        and "mark: '✓'" in states,
        "state must be carried by a glyph and a word as well as a colour")


# ================================================== 3. commissioning gate panel

require('id="commissioningGatePanel"' in INDEX, "there is no commissioning gate panel")
require("'/api/commissioning/gate'" in APP, "nothing reads the commissioning gate")
require("function renderCommissioningGate" in APP, "the gate is never rendered")
require("function gateItemElement" in APP, "the prerequisites are not rendered as items")

# All nine prerequisites the firmware enumerates, by their own identifiers.
for prereq in ("meter_roles", "inverter_profile_qualified", "write_readback",
               "fleet_capacity", "ramp_policy", "source_detection",
               "grid_policy", "generator_limits", "control_tuning"):
    require(f'"{prereq}"' in GATE_CORE, f"firmware prerequisite disappeared: {prereq}")
require("COMMISSIONING_PREREQ_COUNT" in GATE_CORE, "the prerequisite set is no longer enumerated")
require("gate.prerequisites" in APP,
        "the panel does not render the firmware's own prerequisite array")
require("item.satisfied === true" in APP,
        "a prerequisite must be satisfied explicitly, not by truthiness")

# Which one is unmet, and why, without reading a log: the firmware's title, its
# explanatory sentence and its stable reason code.
item_render = APP[APP.index("function gateItemElement"):APP.index("function renderCommissioningGate")]
require("verbatim(item.title" in item_render, "the prerequisite title is not shown verbatim")
require("item.detail" in item_render, "the firmware's explanation is not shown")
require("Reason code:" in item_render, "the stable reason code is not shown")
require("verbatim(item.reason" in item_render, "the reason code is paraphrased")
require("STATES.workflow.blocked" in item_render and "STATES.workflow.complete" in item_render,
        "the checklist does not use the shared workflow vocabulary")
require('cJSON_AddStringToObject(item, "detail"' in GATE_API,
        "the firmware no longer publishes a per-prerequisite explanation")
require('cJSON_AddStringToObject(item, "reason"' in GATE_API,
        "the firmware no longer publishes a per-prerequisite reason code")

# control_authority.mode_label and inhibit_reason, verbatim.
gate_render = APP[APP.index("function renderCommissioningGate"):APP.index("function confirmStatePill")]
require("verbatim(authority?.mode_label)" in gate_render,
        "the controller's own control-authority label must be shown verbatim")
require("verbatim(authority?.inhibit_reason" in gate_render,
        "the controller's own inhibit reason must be shown verbatim")
require("gate.summary" in gate_render,
        "the firmware's gate summary sentence is not shown")
require('cJSON_AddStringToObject(authority, "mode_label"' in WEB_API,
        "the firmware no longer publishes a control mode label")
require('cJSON_AddStringToObject(authority, "inhibit_reason", control.inhibit_reason)' in WEB_API,
        "the firmware no longer publishes an inhibit reason")
for invention in ("MODE_LABELS", "INHIBIT_LABELS", "describeInhibit",
                  "Inhibited because", "modeLabelText"):
    require(invention not in APP,
            f"the interface paraphrases a firmware safety statement: {invention}")

# commissioned, lab-only and production-qualified are three different answers.
require("gate.production_qualified === true" in gate_render,
        "a production-qualified gate is not distinguished from a lab-only one")
require("gate.lab_simulator_mode === true" in gate_render,
        "the panel does not distinguish a lab-only gate")
require("STATES.commissioning.qualified" in gate_render
        and "STATES.commissioning.configured" in gate_render
        and "STATES.commissioning.notConfigured" in gate_render,
        "the gate badge does not use the shared commissioning vocabulary")
# A lab-only gate must never be badged Qualified.
lab_branch = gate_render[gate_render.index("gate.lab_simulator_mode === true"):]
require("STATES.commissioning.qualified" not in lab_branch.split("} else {", 1)[0],
        "a lab-only gate must not be presented as Qualified")


# ================================================= 4. lab target control (write)

require('id="labTargetPanel"' in INDEX, "there is no lab target control")
require("'/api/inverter-profile-assignment'" in APP,
        "the lab target control does not use the profile assignment endpoint")
require("function applyLabTarget" in APP, "nothing sends a lab target declaration")
apply_lab = APP[APP.index("function applyLabTarget"):APP.index("function engineeringAuthorized")]
require("lab_target: declare" in apply_lab,
        "the request must carry an explicit lab_target boolean")
require("inverter_index: index" in apply_lab and "profile_id: profileId" in apply_lab,
        "the request must name the inverter and the profile")

# The consequence is explicit BEFORE the engineer confirms, in the panel and
# again at the moment of the decision.
require("window.confirm(consequences)" in apply_lab,
        "the declaration is sent without an explicit confirmation step")
require("has not been qualified on physical equipment" in apply_lab,
        "the confirmation does not say the profile is unqualified")
require("disables automatic control" in apply_lab,
        "the confirmation does not say that automatic control is disabled")
require("the firmware does this deliberately" in apply_lab,
        "the confirmation does not say the firmware disables control on purpose")
require("lab_simulator_only, never production" in apply_lab,
        "the confirmation does not state the resulting commissioning scope")
require("Declaring a lab target changes what the controller may command." in INDEX,
        "the panel does not state the consequence before the control is used")
require("It grants command authority through a profile that has not been qualified on physical equipment." in INDEX,
        "the panel does not state the unqualified-profile consequence")
require("It disables automatic control." in INDEX,
        "the panel does not state the automatic-control consequence")
require('id="labTargetAcknowledge"' in INDEX,
        "there is no explicit acknowledgement before an unqualified profile is armed")

# The response is reported as received, not interpreted.
require("payload.write_permission_after_restart" in APP,
        "the returned write permission is not shown")
require("payload.lab_target_notice" in APP,
        "the returned lab_target_notice is not shown")
require("verbatim(payload.write_permission_after_restart)" in APP,
        "the returned write permission must be shown verbatim")
require("payload?.lab_target === true" in APP,
        "the message must report the STORED declaration, not the requested one")
require('cJSON_AddStringToObject(response, "write_permission_after_restart"' in PROFILE_API,
        "the firmware no longer returns write_permission_after_restart")
require('cJSON_AddStringToObject(response, "lab_target_notice"' in PROFILE_API,
        "the firmware no longer returns lab_target_notice")
require('cJSON_AddBoolToObject(response, "lab_target", lab_target)' in PROFILE_API,
        "the firmware no longer returns the stored lab_target declaration")
require("Report the stored declaration, not the requested one" in PROFILE_API,
        "the firmware's stored-declaration guarantee this panel relies on has changed")

# The declaration currently held IS now published, on the profiles GET, so the
# panel must show the controller's state rather than echoing its own last request.
# This replaces an earlier caveat asserting the opposite; the tripwire that
# guarded it fired correctly when the read path was added.
require("inverter_profile_store_lab_target_get" in PROFILE_API.split("profile_assignment_post", 1)[0],
        "the profiles GET must publish the lab-target declaration currently held")
require('cJSON_AddArrayToObject(root, "inverter_assignments")' in PROFILE_API,
        "the profiles GET must expose per-inverter assignments")
require("unreadable means" in PROFILE_API or 'lab_target = false' in PROFILE_API,
        "an unreadable declaration must fail closed to 'not a lab target'")
require("inverter_assignments" in APP,
        "the panel must read the declarations the controller holds")
require("renderLabAssignments" in APP,
        "the panel must render the currently held declarations")
# Whitespace-normalised: the phrase is prose in a wrapped comment, and asserting
# the raw text would break on a harmless re-wrap.
require("not what this browser last sent" in " ".join(APP.split()),
        "the panel must distinguish controller state from its own last request")
require("does not publish the lab-target declaration it currently holds" not in INDEX,
        "the panel must no longer claim the present declaration cannot be read back")
require("read from the controller itself" in INDEX,
        "the panel must state that declarations come from the controller")

# 44px hit targets on a control that arms an unqualified write path.
require(".lab-target-panel .button { min-height: 44px; min-width: 44px; }" in lab_css,
        "the lab target action does not meet the 44px minimum tap target")
require(".lab-target-ack { margin-top: 14px; min-height: 44px; }" in lab_css,
        "the acknowledgement control does not meet the 44px minimum tap target")


# ================================================ authentication is handled well

# Every one of these endpoints is engineering-guarded by the force-included
# gateway, so a request made without a session can only answer 401. The
# controller serves a small client socket pool; a guaranteed 401 costs a socket
# an operator needs.
require("#define httpd_register_uri_handler engineering_register_uri_handler"
        in (ROOT / "components/web_server/include/engineering_auth.h").read_text(encoding="utf-8"),
        "the registration gateway that makes these endpoints engineering-only is gone")
require("-include;engineering_auth.h"
        in (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8"),
        "the gateway is no longer force-included, so the 401 assumption may be wrong")

require("function engineeringAuthorized" in APP,
        "the reads do not consult the engineering session state")
require("access.isAuthenticated()" in APP,
        "the reads must ask the shared access layer, not track auth themselves")
require("access.mayRequest('/api/solar-grid/status')" in APP,
        "the solar-grid read must go through the shared endpoint scope table")
require("access.mayUseEngineering('inverters', 'control', 'commissioning')" in APP,
        "the confirmation read must be scoped to the routes that need it")
require("{ path: '/api/solar-grid/status', routes: ['control', 'commissioning'] }" in MODE,
        "the shared scope table no longer describes /api/solar-grid/status")

# 401 is told apart from a real fault, and neither produces a broken page.
require("error.status = response.status;" in APP,
        "the shared fetch helper discards the HTTP status, so 401 cannot be told from 500")
require(APP.count("error.status === 401") >= 3,
        "the gate, confirmation and declaration paths must each handle 401 explicitly")
require("requires an authenticated engineering session. Nothing was changed." in APP,
        "a rejected declaration must say that nothing changed")
require("has not told this browser whether its commanded inverters are real equipment" in APP,
        "an unauthenticated user must be told that the scope is unknown, not shown a blank")

# Unknown is not 'fine'. None of these may default to a reassuring value.
require("commissioningGate: null" in APP and "solarGridStatus: null" in APP
        and "writeConfirmation: null" in APP,
        "these reads must start unknown rather than at a default that reads as healthy")
for reset in ("state.commissioningGate = null;", "state.writeConfirmation = null;",
              "state.solarGridStatus = null;"):
    require(reset in APP,
            f"a failed read must clear its state rather than leave stale data on screen: {reset}")

# The polling must not be the 2 s status cadence: these are engineering endpoints
# on a device with ten open sockets in total.
require("window.setInterval(refreshCommissioningGate, 10000);" in APP,
        "the gate is polled too aggressively for an engineering endpoint")
require("window.setInterval(refreshWriteConfirmation, 5000);" in APP,
        "the confirmation poll interval is not declared")
require("state.gateSignature" in APP and "state.confirmSignature" in APP,
        "the lists are rebuilt on every poll, which re-runs the access layer's "
        "unguarded DOM observer for no change")


# ------------------------------------------------------------ CI registration

require("tests/lab_control_ui_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")

print("lab control UI source contract: PASS")
