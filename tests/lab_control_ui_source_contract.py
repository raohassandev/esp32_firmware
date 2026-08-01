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
     post-write observation supported the command - and since plant-level logger
     control landed, that observation is either a limit demonstrated by measured
     power or a setpoint readback that may be an echo of a stored command, so the
     panel must always say which. That provenance is guarded in detail by
     tests/confirmation_provenance_source_contract.py; this file pins only that
     the confirmed copy here no longer presents a setpoint match as the whole
     meaning. mismatched means a readback disagreed, which
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


# SECTIONS 2 AND 4 ARE GONE, and this is deliberate.
#
# The setpoint-confirmation panel and the lab-target control were removed from
# the Inverters page at the owner's request. Checking that a page renders a
# panel that no longer exists is a contract that can only ever fail, and
# keeping it as a skip teaches people to ignore this file.
#
# WHAT WAS REMOVED WAS THE DISPLAY, NOT THE GATE. The firmware clauses below
# and in the sections that remain are untouched: the controller still refuses
# to command through an unqualified profile, still reports lab_simulator_only
# rather than production while any commanded inverter is a declared simulator,
# still publishes the confirmation provenance, and still carries the shell
# banner that says so on every page. Those are the safety properties; the
# panels were one way of reading them.
#
# tests/confirmation_provenance_source_contract.py keeps the firmware side of
# the confirmation semantics.

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
# Ends at the next function, which is part of the gate rendering itself.
# The old anchor was confirmStatePill, which belonged to the removed
# confirmation panel -- an end anchor in unrelated code is an end anchor
# that disappears when that code does.
gate_render = APP[APP.index("function renderCommissioningGate"):
                  APP.index("function renderGateLimitEvidence")]
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
# The confirmation read is gone with its panel, so there is no longer a request
# to scope. What matters -- that no engineering endpoint is requested without a
# session -- is still checked above and by the shared scope table in
# web/product-mode.js.
require("{ path: '/api/solar-grid/status', routes: ['control', 'commissioning'] }" in MODE,
        "the shared scope table no longer describes /api/solar-grid/status")

# 401 is told apart from a real fault, and neither produces a broken page.
require("error.status = response.status;" in APP,
        "the shared fetch helper discards the HTTP status, so 401 cannot be told from 500")
require(APP.count("error.status === 401") >= 1,
        "no engineering read tells an expired session apart from a real fault, "
        "so a signed-out browser reports the controller as broken")
# The declaration write is gone with the lab-target panel, so there is no
# rejected declaration for the page to describe. The firmware still refuses the
# write without a session; that is checked against engineering_auth.h below.
require("has not told this browser whether its commanded inverters are real equipment" in APP,
        "an unauthenticated user must be told that the scope is unknown, not shown a blank")

# Unknown is not 'fine'. None of these may default to a reassuring value.
require("commissioningGate: null" in APP and "solarGridStatus: null" in APP,
        "these reads must start unknown rather than at a default that reads as healthy")
for reset in ("state.commissioningGate = null;", "state.solarGridStatus = null;"):
    require(reset in APP,
            f"a failed read must clear its state rather than leave stale data on screen: {reset}")

# The polling must not be the 2 s status cadence: these are engineering endpoints
# on a device with ten open sockets in total.
require("window.setInterval(refreshCommissioningGate, 10000);" in APP,
        "the gate is polled too aggressively for an engineering endpoint")
# The 5-second confirmation poll went with its panel. It was the most frequent
# engineering request this page made, and nothing renders its answer now.
require("refreshWriteConfirmation" not in APP,
        "the confirmation poll is back without a panel to render it, on a "
        "controller with ten sockets in total")
require("state.gateSignature" in APP,
        "the lists are rebuilt on every poll, which re-runs the access layer's "
        "unguarded DOM observer for no change")


# ------------------------------------------------------------ CI registration

require("tests/lab_control_ui_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")

print("lab control UI source contract: PASS")
