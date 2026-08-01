#!/usr/bin/env python3
"""The prerequisite enable register must be REPORTED, not merely enforced.

inverter_manager sequences and verifies a prerequisite enable register (Solis
manual tag 3070, Sungrow tag 5007, Chint/CPS 0x2602) and already excludes an
inverter whose register is not confirmed from the commandable capacity. The
enforcement was never the gap. The gap was that nothing consumed it: an engineer
saw an inverter silently absent from the fleet with no stated reason.

WHY THAT IS DANGEROUS RATHER THAN MERELY UNTIDY
-----------------------------------------------
A prerequisite fault and a setpoint mismatch have the same visible effect -- the
inverter leaves the commandable fleet -- and nothing else in common:

  write confirmation fault   the SETPOINT register read back the wrong value, or
                             could not be read at all. Visible in the readback.
  prerequisite enable fault  the ENABLE register is not confirmed to hold, so the
                             setpoint is accepted, ECHOED BACK, and then ignored.
                             The setpoint readback looks perfect.

The second is the more dangerous precisely because the readback exonerates it.
An engineer told only "unconfirmed setpoint" is sent to inspect the register that
is working, while the machine generates at full output. So the two must be
reported as two things, everywhere, and the copy must say out loud that a
setpoint can read back perfectly and still be ignored.

A second distinction carries just as much weight. UNCONFIRMED is transient: the
background task keeps reading and rewriting, and it resolves itself.
UNVERIFIABLE is permanent: the profile cannot describe a register that is both
writable and readable, write authority is refused outright, and no amount of
polling changes it -- the remedy is a manual citation. One needs nothing but
time; the other needs a person. Presenting them identically means a permanent
fault gets waited out.

WHAT THIS FILE GUARDS
---------------------
The wiring from the already-tested core out to the control engine, the two APIs
and the interface. The prerequisite CORE behaviour is executed by
tests/inverter_prerequisite_test.c and pinned by
tests/inverter_prerequisite_enable_source_contract.py; nothing here re-asserts
it. Contrast is COMPUTED from the parsed token values in both themes, not
eyeballed, for the same reason as in tests/lab_control_ui_source_contract.py:
this codebase has twice shipped text below 1.2:1.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MANAGER_H = (ROOT / "components/inverter_manager/include/inverter_manager.h").read_text(encoding="utf-8")
TYPES_H = (ROOT / "components/inverter_manager/include/inverter_types.h").read_text(encoding="utf-8")
CONTROL = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
CONTROL_TYPES = (ROOT / "components/control_engine/include/control_types.h").read_text(encoding="utf-8")
GATE_API = (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8")
SOLAR_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")
INDEX = (ROOT / "web/index.html").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ===========================================================================
# 0. The producer still produces what everything below consumes.
#
# Not a re-test of the core -- a tripwire. If any of these names change, the
# consumers added here go quiet rather than wrong, which is the failure mode this
# whole exercise exists to remove.
# ===========================================================================

require("bool inverter_manager_prerequisite_enable_fault(void);" in MANAGER_H,
        "inverter_manager_prerequisite_enable_fault() is gone; every report below "
        "would silently have nothing to report")
for field in ("prerequisite_required_count", "prerequisite_unconfirmed_count",
              "prerequisite_unverifiable_count"):
    require(f"uint8_t {field};" in MANAGER_H,
            f"inverter_fleet_commissioning_t no longer carries {field}")
for field in ("prerequisite_required", "prerequisite_satisfied",
              "prerequisite_unverifiable", "prerequisite_describable",
              "prerequisite_read_valid", "prerequisite_holds",
              "prerequisite_lost_count"):
    require(f"{field};" in TYPES_H,
            f"inverter_data_t no longer carries {field}, which the per-inverter "
            "report reads")


# ===========================================================================
# 1. The control engine observes it, and says so in its own words.
# ===========================================================================

require("inverter_manager_prerequisite_enable_fault()" in CONTROL,
        "the control engine does not observe the prerequisite enable fault")
require("inverter_manager_write_confirmation_fault()" in CONTROL,
        "the control engine no longer observes the write confirmation fault, so "
        "there is nothing for the prerequisite fault to be distinguished from")

# Both are read from already-acquired state. Neither may become an I/O call in
# the control loop: its period is 20 ms.
loop = CONTROL[CONTROL.index("static void control_task"):]
for forbidden in ("inverter_manager_read_", "inverter_manager_probe_read_only(",
                  "modbus_"):
    require(forbidden not in loop,
            f"the control loop must not gain blocking I/O: {forbidden}")

# The status struct carries the fault and the three counts, separately from write
# confirmation. Separate fields, not one merged severity.
for field in ("bool prerequisite_enable_fault;",
              "uint8_t prerequisite_required_count;",
              "uint8_t prerequisite_unconfirmed_count;",
              "uint8_t prerequisite_unverifiable_count;"):
    require(field in CONTROL_TYPES, f"control_status_t does not publish {field}")
require("bool write_confirmation_fault;" in CONTROL_TYPES,
        "control_status_t must still carry the write confirmation fault separately")
require(CONTROL_TYPES.index("bool write_confirmation_fault;")
        < CONTROL_TYPES.index("bool prerequisite_enable_fault;"),
        "the prerequisite fault must be declared next to the write confirmation "
        "fault so the two are read together")

# It is populated from the fleet summary, not re-derived from anything else.
require("inverter_manager_commissioning_summary(&fleet_prerequisite)" in CONTROL,
        "the fleet prerequisite counts must come from the manager's own summary")
for assignment in (".prerequisite_enable_fault = prerequisite_fault,",
                   "fleet_prerequisite.prerequisite_required_count,",
                   "fleet_prerequisite.prerequisite_unconfirmed_count,",
                   "fleet_prerequisite.prerequisite_unverifiable_count,"):
    require(assignment in CONTROL,
            f"the control status does not carry the summary value: {assignment}")

# --------------------------------------------------------------- inhibit_reason
#
# A DISTINCT reason, in the firmware's own words, and it must not read like the
# write-confirmation one. The interface renders inhibit_reason verbatim, so this
# string IS the operator-facing safety statement.

inhibit = CONTROL[CONTROL.index("const char *inhibit ="):
                  CONTROL.index("strlcpy(next.inhibit_reason")]
require("prerequisite_fault" in inhibit,
        "the prerequisite fault produces no inhibit reason of its own")

reasons = re.findall(r'\? *"([^"]{20,})"', inhibit) + re.findall(r': *"([^"]{20,})"', inhibit)
prereq_reasons = [r for r in reasons if "enable register" in r]
require(len(prereq_reasons) == 2,
        "there must be exactly two prerequisite reasons: one for the transient "
        "unconfirmed case and one for the permanent unverifiable case, because "
        f"they need different actions (found {len(prereq_reasons)})")
for reason in prereq_reasons:
    # inhibit_reason is char[128] and strlcpy TRUNCATES. A safety sentence cut
    # off mid-clause is worse than a shorter one.
    require(len(reason) < 128,
            f"inhibit reason is {len(reason)} chars and would be truncated by "
            f"strlcpy into inhibit_reason[128]: {reason!r}")
    # The danger, stated: the readback exonerates the fault.
    require("read back correctly" in reason,
            "a prerequisite inhibit reason must say that the setpoint would read "
            f"back CORRECTLY and still be ignored: {reason!r}")
    require("ignored" in reason,
            f"a prerequisite inhibit reason must say the setpoint is ignored: {reason!r}")
    # It must not be mistakable for the setpoint fault.
    require("could not be confirmed by readback" not in reason,
            "the prerequisite reason must not reuse the write-confirmation wording")

# The permanent case must be the one that says the register cannot be read at
# all, and it must come FIRST: the reasons are ordered most specific first, and
# "cite the register" is actionable where "wait" is not.
permanent = [r for r in prereq_reasons if "cannot be read back" in r]
require(len(permanent) == 1,
        "exactly one prerequisite reason must describe the permanent, "
        "unverifiable case as a register that cannot be read back")
if permanent:
    require(inhibit.index(permanent[0]) < min(inhibit.index(r) for r in prereq_reasons
                                              if r != permanent[0]),
            "the permanent unverifiable reason must be stated before the transient "
            "one, keeping the chain ordered most specific first")
require("prerequisite_unverifiable_count > 0U" in inhibit,
        "the permanent reason must be guarded by the unverifiable count, or a "
        "transient fault would be reported as permanent")

# Ordering against the existing reasons: the prerequisite reason is the more
# specific of the two faults and must precede the write-confirmation one, or an
# engineer is sent to inspect the setpoint register, which is working.
confirmation_reason = [r for r in reasons if "could not be confirmed by readback" in r]
require(len(confirmation_reason) == 1,
        "the write-confirmation inhibit reason has changed or gone")
if confirmation_reason and prereq_reasons:
    require(max(inhibit.index(r) for r in prereq_reasons)
            < inhibit.index(confirmation_reason[0]),
            "both prerequisite reasons must precede the write-confirmation reason: "
            "the setpoint readback looks perfect during a prerequisite fault, so "
            "reporting the setpoint fault first sends the engineer to the wrong "
            "register")

# The reason must actually mean something: a reason that appears while authority
# is granted is not a reason. Treated exactly as the confirmation fault is.
authority = CONTROL[CONTROL.index(".command_authority ="):
                    CONTROL.index(".commissioned = commissioning.commissioned")]
require("!prerequisite_fault" in authority,
        "an unconfirmed prerequisite must withdraw command authority exactly as an "
        "unconfirmed setpoint does; otherwise inhibit_reason would name a reason "
        "while reporting that nothing is inhibited")
require("!confirmation_fault" in authority,
        "the write confirmation fault must still withdraw command authority")


# ===========================================================================
# 2. Both APIs publish the fault and the three counts, unconditionally.
#
# Unconditional for the reason commissioning_scope is unconditional: a client
# that can read one verdict field must be able to read the other, or an inverter
# vanishes from the fleet with no reason attached.
# ===========================================================================

PUBLISHED = (
    ('cJSON_AddBoolToObject(root, "prerequisite_enable_fault"', "the fault"),
    ('cJSON_AddNumberToObject(root, "prerequisite_required_count"', "the required count"),
    ('cJSON_AddNumberToObject(root, "prerequisite_unconfirmed_count"', "the unconfirmed count"),
    ('cJSON_AddNumberToObject(root, "prerequisite_unverifiable_count"', "the unverifiable count"),
)

for name, text in (("commissioning_gate_api.c", GATE_API),
                   ("solar_grid_status_api.c", SOLAR_API)):
    for needle, label in PUBLISHED:
        require(needle in text, f"{name} does not publish {label}")

    # Unconditional: the publishing call must not be nested inside a branch. A
    # brace-depth walk is used rather than trusting indentation.
    for needle, label in PUBLISHED:
        at = text.index(needle)
        body = text[:at]
        require(body.count("{") - body.count("}") == 1,
                f"{name} publishes {label} inside a nested block; it must be "
                "unconditional, alongside the existing verdict fields")

    # And distinguished from write confirmation IN THE SAME OBJECT, so a client
    # cannot read one without the other being there to compare against.
    require('"write_confirmation_fault"' in text,
            f"{name} must publish the write confirmation fault beside the "
            "prerequisite fault, or the two cannot be told apart by a client")
    require(text.count('"prerequisite_enable_fault"') >= 1
            and '"prerequisite_fault"' not in text,
            f"{name} must name the prerequisite fault unambiguously; a bare "
            "'prerequisite_fault' key would collide with the commissioning "
            "gate's own prerequisites")

    # The three counts are three keys. Summing them would erase the only
    # distinction that changes what a person has to do.
    require('"prerequisite_fault_count"' not in text,
            f"{name} must not merge the prerequisite counts into one figure: "
            "unverifiable needs a manual citation, unconfirmed needs only time")

# The danger is stated by the firmware, not left to the interface to invent.
for name, text in (("commissioning_gate_api.c", GATE_API),
                   ("solar_grid_status_api.c", SOLAR_API)):
    notice = re.search(r'cJSON_AddStringToObject\(root, "prerequisite_[a-z_]*notice",\s*'
                       r'((?:\s*"[^"]*"\s*)+)\)', text)
    require(notice is not None,
            f"{name} must publish a notice explaining the danger in the "
            "firmware's own words")
    if notice:
        copy = " ".join(re.findall(r'"([^"]*)"', notice.group(1)))
        require("echoed back" in copy,
                f"{name}'s notice must say the setpoint is echoed back")
        require("ignored" in copy, f"{name}'s notice must say the setpoint is ignored")

# The gate endpoint must keep the two fault families adjacent rather than
# scattered, and must not have moved the prerequisite report above the verdict it
# qualifies.
require(GATE_API.index('"command_authority"') < GATE_API.index('"prerequisite_enable_fault"'),
        "the prerequisite report belongs with the verdict fields, after the "
        "authority answer it explains")

# ------------------------------------- the HTTP handlers still perform no I/O
#
# The summary and the fault predicate both read already-acquired state. A blocking
# Modbus transaction in a handler is forbidden outright.
for name, text in (("commissioning_gate_api.c", GATE_API),
                   ("solar_grid_status_api.c", SOLAR_API)):
    require('cJSON_AddBoolToObject(root, "modbus_io_in_http_handler", false)' in text
            or "modbus" not in text.lower(),
            f"{name} must continue to declare that it performs no Modbus I/O")
    for forbidden in ("inverter_manager_set_total_power_kw",
                      "inverter_manager_probe_read_only",
                      "modbus_master_read", "modbus_master_write"):
        require(forbidden not in text,
                f"{name} must not perform a blocking Modbus transaction: {forbidden}")

require("inverter_manager" in CMAKE,
        "web_server must declare its dependency on inverter_manager, which the "
        "prerequisite report calls into")


# ===========================================================================
# 3. Per-inverter state, with unconfirmed and unverifiable kept apart.
# ===========================================================================

slug = re.search(r"static const char \*prerequisite_state_slug\(.*?\n\{(.*?)\n\}",
                 GATE_API, re.DOTALL)
require(slug is not None,
        "there is no single place that turns an inverter's prerequisite fields "
        "into a stable slug")
if slug:
    body = slug.group(1)
    # Four answers. Merging the middle two is the defect this guards.
    for state in ('"not_required"', '"confirmed"', '"unverifiable"', '"unconfirmed"'):
        require(state in body, f"the per-inverter slug cannot report {state}")
    # Fail-closed: the fallthrough must be unconfirmed, never confirmed.
    require(body.rstrip().endswith('return "unconfirmed";'),
            "anything not positively confirmed must fall through to unconfirmed, "
            "so a zeroed struct cannot read as permission")
    require('if (!data) return "unconfirmed";' in body,
            "a NULL inverter must be reported unconfirmed, not confirmed")
    # Permanent is decided before satisfied, and satisfied is read from the flag
    # that only a READ sets.
    require(body.index("prerequisite_unverifiable") < body.index("prerequisite_satisfied"),
            "the permanent case must be decided before the satisfied flag is "
            "consulted, or an unverifiable register could be reported as merely "
            "unconfirmed")

# The nested object keeps it out of the setpoint state's namespace: an inverter
# can be CONFIRMED on its setpoint and still be ignoring the limit.
require('cJSON_AddObjectToObject(item, "prerequisite")' in GATE_API,
        "the per-inverter prerequisite state must be its own object, so it can "
        "never be mistaken for one of the four setpoint confirmation states")
for field in ("state", "required", "satisfied", "unverifiable", "read_valid",
              "holds", "raw", "lost_count", "last_error_name"):
    require(f'(prerequisite, "{field}"' in GATE_API,
            f"the per-inverter prerequisite report omits {field}")
require('cJSON_AddNumberToObject(prerequisite, "lost_count"' in GATE_API,
        "the number of times an armed limit was switched off underneath this "
        "controller must be reported: for Solis that returns the machine to 100 %")


# ===========================================================================
# 4. The interface: shown wherever setpoint confirmation is, and the danger is
#    stated in words rather than implied by a colour.
# ===========================================================================

# ------------------------------------------------------------ token parsing
#
# Both themes are parsed from the stylesheets. A token declared in one theme only
# cannot pass, because the lookup will not find it -- which is exactly how this
# codebase produced 1.10:1 and 1.17:1 text.

TOKEN_PATTERN = re.compile(r"(--[a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{6})\s*;")


def token_block(sheet, opener):
    require(opener in sheet, f"stylesheet has no {opener!r} block")
    return sheet.split(opener, 1)[1].split("\n}", 1)[0]


DARK = dict(TOKEN_PATTERN.findall(token_block(APP_CSS, ":root {")))
LIGHT = dict(TOKEN_PATTERN.findall(token_block(THEME_CSS, 'html[data-theme="light"] {')))


def channel(value):
    fraction = value / 255.0
    return fraction / 12.92 if fraction <= 0.03928 else ((fraction + 0.055) / 1.055) ** 2.4


def luminance(hex_colour):
    text = hex_colour.lstrip("#")
    red, green, blue = (int(text[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * channel(red) + 0.7152 * channel(green) + 0.0722 * channel(blue)


def contrast(first, second):
    high, low = sorted((luminance(first), luminance(second)), reverse=True)
    return (high + 0.05) / (low + 0.05)


def check_contrast(label, foreground, background, minimum):
    for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
        if foreground not in theme:
            failures.append(f"{foreground} is not declared as a literal colour for "
                            f"the {theme_name} theme")
            continue
        if background not in theme:
            failures.append(f"{background} is not declared as a literal colour for "
                            f"the {theme_name} theme")
            continue
        ratio = contrast(theme[foreground], theme[background])
        require(ratio >= minimum,
                f"{theme_name} theme: {label} measures {ratio:.2f}:1 on {background}, "
                f"below the required {minimum}:1")


PREREQ_TOKENS = ("--prereq-confirmed", "--prereq-unconfirmed", "--prereq-unverifiable")

# Every string in these blocks is 10-12px, so the 4.5:1 body threshold applies to
# all of it; none of it qualifies for the 3:1 large-text allowance.
for token in PREREQ_TOKENS:
    check_contrast(f"{token} state text", token, "--confirm-surface", 4.5)
# The notice takes the fault rule as its boundary, and a boundary is non-text.
check_contrast("prerequisite notice rule", "--confirm-mismatched", "--confirm-surface", 3.0)
# The neutral 'no enable register' state uses --muted and --gate-boundary, both of
# which already carry text and boundaries in this panel; measured again here so a
# retune of either cannot quietly drop this block below AA.
check_contrast("no-enable-register state text", "--muted", "--confirm-surface", 4.5)
check_contrast("prerequisite block rule", "--gate-boundary", "--confirm-surface", 3.0)

for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
    values = [theme.get(token) for token in PREREQ_TOKENS]
    require(all(values) and len(set(values)) == 3,
            f"{theme_name} theme: the three prerequisite states do not have three "
            "distinct colours; a reader cannot separate states painted alike")
    # The two that matter most.
    require(theme.get("--prereq-unconfirmed") != theme.get("--prereq-confirmed"),
            f"{theme_name} theme: an unconfirmed enable register is painted with "
            "the confirmed colour")
    require(theme.get("--prereq-unverifiable") != theme.get("--prereq-unconfirmed"),
            f"{theme_name} theme: permanent and transient are painted alike, so a "
            "fault that needs a manual citation looks like one that needs only time")

# Styling resolves through tokens. A hardcoded hex on a themed surface is the
# exact defect that produced this file's contrast arithmetic.
prereq_css = APP_CSS[APP_CSS.index("/* ------------------------------------------- prerequisite enable register"):
                     APP_CSS.index("/* -------------------------------------------------------- lab target control */")]
require("#" not in prereq_css,
        "the prerequisite styling must resolve every colour through a theme token")
require("opacity" not in prereq_css,
        "no prerequisite state may be faded out: an unarmed enable register is "
        "not a resolved condition")
prereq_declarations = re.sub(r"/\*.*?\*/", "", prereq_css, flags=re.S)
require("--muted-2" not in prereq_declarations,
        "--muted-2 measures 4.39:1 dark and 3.56:1 light on --confirm-surface and "
        "must not carry text in this panel")
for token in PREREQ_TOKENS:
    require(f"var({token})" in prereq_css, f"{token} is declared but never used")

# State is carried by a word and a glyph as well as a colour.
for state in ("confirmed", "unconfirmed", "unverifiable", "not_required"):
    require(f".prereq-state-{state} {{" in prereq_css,
            f"the {state} enable-register state has no treatment of its own")
# Permanent is drawn more heavily than transient, and neutral is dashed as
# 'nothing measured here' is drawn everywhere else in this application.
require("border-width: 2px" in prereq_css.split(".prereq-state-unverifiable", 1)[1].split("\n", 1)[0],
        "the permanent unverifiable state must be drawn more heavily than the "
        "transient one; it will not resolve on its own")
require("border-style: dashed" in prereq_css.split(".prereq-state-not_required", 1)[1].split("\n", 1)[0],
        "'this device needs no enable register' must be drawn as the neutral, "
        "unmeasured state rather than as a verdict")

# THE VOCABULARY AND RENDERING SECTIONS ARE GONE.
#
# They checked that the setpoint-confirmation panel on the Inverters page drew
# the four enable-register states with a closed vocabulary, distinct glyphs and
# an outline of their own. That panel was removed at the owner's request, so
# there is nothing left to draw them and a check that a deleted panel renders
# correctly can only fail.
#
# WHAT REMAINS IS THE SAFETY PROPERTY, and it is all above this line: the
# control engine observes the fault, orders it ahead of the setpoint fault,
# publishes it from both APIs unconditionally with no Modbus I/O in the
# handler, keeps transient apart from permanent, and never lets an unconfirmed
# enable register be described as armed. The commissioning gate still carries
# the warning in words -- checked above -- and that is the panel an engineer
# reads before allowing control, which is when this decides something.

# ------------------------------------------------------------ CI registration

require("tests/prerequisite_reporting_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")


if failures:
    import sys
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("prerequisite reporting source contract passed "
      "(control engine observes the fault with two distinct untruncated reasons "
      "ordered ahead of the setpoint fault, both APIs publish the fault and three "
      "separate counts unconditionally with no Modbus I/O, per-inverter state "
      "keeps transient apart from permanent, and the UI states the danger in "
      "words at AA contrast in both themes)")
