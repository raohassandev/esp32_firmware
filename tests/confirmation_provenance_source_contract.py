#!/usr/bin/env python3
"""Confirmed must never be shown without what confirmed it.

WHY THIS FILE EXISTS
--------------------
Since plant-level logger control landed, a write can be confirmed by two
different kinds of evidence, and they are not equally strong. From
components/inverter_manager/include/inverter_write_confirmation.h:

  INVERTER_WRITE_PROOF_MEASURED_POWER       measured output was ABOVE the new
      limit before the command and at or below it after. The limit is
      DEMONSTRATED. No change in irradiance can lift a plant above a limit that
      is in force, so the opposite direction is unambiguous too.

  INVERTER_WRITE_PROOF_SETPOINT_READBACK    the setpoint register read back
      matching. On some devices that is the applied value. On the Huawei
      SmartLogger plant interface the register STORES the command and forwards it
      ("This interface stores data ..."), so reading it back returns the stored
      command, not the plant's achieved state -- and the logger may scale the
      commanded percentage by an "Adjustment coefficient" that has no register a
      Modbus client can read. This proves ACCEPTANCE and nothing more.

  INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM   output is at or below the limit but
      was ALREADY at or below it. Equally consistent with the limit being
      honoured and with the sun going in. The verdict is UNVERIFIED and
      limit_demonstrated is false.

inverter_data_t has carried write_proof, limit_demonstrated, ambiguous_count and
authority_lost_count since that work landed. None of it was published and no
interface showed it, while a panel told the operator that confirmed means the
setpoint matched -- which is now only one of two possibilities, and the weaker
one. An operator reading "confirmed" without knowing what confirmed it can
believe a plant is limited when only an echo was observed. That is the same
false-confirmation hazard the firmware refuses four brands over, reintroduced at
the last layer.

WHAT THIS FILE GUARDS
---------------------
  1. The producer still produces it. Tripwires only; the confirmation CORE is
     executed by tests/inverter_write_confirmation_test.c.
  2. The fleet ROLL-UP rule, which is pure and therefore EXECUTED by
     tests/write_provenance_test.c, compiled and run from here.
  3. Both endpoints publish the provenance UNCONDITIONALLY, beside the verdict,
     for the reason commissioning_scope is unconditional: a client that can read
     one must be able to read the other.
  4. The interface never shows a verdict without its evidence, distinguishes a
     demonstrated limit from an echo visually AND textually, and presents the
     ambiguous case as its own thing -- neither success nor failure.

Contrast is COMPUTED from the parsed token values in both themes rather than
asserted by eye, as in tests/lab_control_ui_source_contract.py and
tests/prerequisite_reporting_source_contract.py: this codebase has twice shipped
text below 1.2:1.
"""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CONFIRM_H = (ROOT / "components/inverter_manager/include/inverter_write_confirmation.h").read_text(encoding="utf-8")
CONFIRM_C = (ROOT / "components/inverter_manager/inverter_write_confirmation.c").read_text(encoding="utf-8")
TYPES_H = (ROOT / "components/inverter_manager/include/inverter_types.h").read_text(encoding="utf-8")
ROLLUP_H = (ROOT / "components/web_server/include/write_provenance.h").read_text(encoding="utf-8")
ROLLUP_C = (ROOT / "components/web_server/write_provenance.c").read_text(encoding="utf-8")
EMIT_C = (ROOT / "components/web_server/write_provenance_api.c").read_text(encoding="utf-8")
GATE_API = (ROOT / "components/web_server/commissioning_gate_api.c").read_text(encoding="utf-8")
SOLAR_API = (ROOT / "components/web_server/solar_grid_status_api.c").read_text(encoding="utf-8")
CMAKE = (ROOT / "components/web_server/CMakeLists.txt").read_text(encoding="utf-8")
APP = (ROOT / "web/app.js").read_text(encoding="utf-8")
APP_CSS = (ROOT / "web/app.css").read_text(encoding="utf-8")
THEME_CSS = (ROOT / "web/theme.css").read_text(encoding="utf-8")
INDEX = (ROOT / "web/index.html").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ===========================================================================
# 1. The producer still produces what everything below consumes.
#
# Not a re-test of the core. If any of these names change the consumers below go
# quiet rather than wrong, which is the failure mode this exercise removes.
# ===========================================================================

for symbol in ("INVERTER_WRITE_PROOF_NONE", "INVERTER_WRITE_PROOF_SETPOINT_READBACK",
               "INVERTER_WRITE_PROOF_MEASURED_POWER",
               "INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM"):
    require(symbol in CONFIRM_H, f"the proof vocabulary no longer declares {symbol}")
require("const char *inverter_write_proof_name(inverter_write_proof_t proof);" in CONFIRM_H,
        "the stable proof slug function is gone; every report below would have to "
        "invent its own spelling of the evidence")
for slug in ('return "none";', 'return "setpoint_readback";',
             'return "measured_power";', 'return "ambiguous_headroom";'):
    require(slug in CONFIRM_C, f"the proof slug the interface keys on has changed: {slug}")
require("bool limit_demonstrated;" in CONFIRM_H,
        "the verdict no longer carries limit_demonstrated, the only field that "
        "says a limit was shown to be in force")

for field in ("uint8_t write_proof;", "bool limit_demonstrated;",
              "uint32_t ambiguous_count;", "uint32_t authority_lost_count;",
              "float baseline_power_kw;", "bool baseline_valid;"):
    require(field in TYPES_H, f"inverter_data_t no longer carries {field}")


# ===========================================================================
# 2. The fleet roll-up: pure, weakest-first, and EXECUTED.
# ===========================================================================

require('"write_provenance.c"' in CMAKE and '"write_provenance_api.c"' in CMAKE,
        "the provenance sources are not compiled into web_server")

# Pure means pure. The whole point of the split is that the rule can be executed
# on the host instead of asserted about, and every one of these would break that.
for forbidden in ("cJSON", "esp_http_server", "ESP_LOG", "malloc(", "free(",
                  "portENTER_CRITICAL", "vTaskDelay", "modbus_",
                  "inverter_manager_"):
    require(forbidden not in ROLLUP_C,
            f"write_provenance.c must stay pure and host-compilable: {forbidden}")

# Weakest-first, the same rule inverter_write_state_worst() applies to the
# verdict. A fleet is only ever as well evidenced as its least well evidenced
# member, so the ranking must place a demonstrated limit above an echo above an
# ambiguous reading above nothing.
require("uint8_t write_provenance_proof_rank" in ROLLUP_C,
        "there is no single place that says how much each kind of evidence claims")
rank = ROLLUP_C[ROLLUP_C.index("uint8_t write_provenance_proof_rank"):
                ROLLUP_C.index("void write_provenance_accumulate")]
for proof, value in (("INVERTER_WRITE_PROOF_MEASURED_POWER", "3U"),
                     ("INVERTER_WRITE_PROOF_SETPOINT_READBACK", "2U"),
                     ("INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM", "1U"),
                     ("INVERTER_WRITE_PROOF_NONE", "0U")):
    require(f"case {proof}: return {value};" in rank,
            f"the evidence ranking no longer places {proof} at {value}")
require(rank.rstrip().endswith("return 0U;\n}") or "default: break;" in rank,
        "an unrecognised proof value must rank lowest; anything else lets a value "
        "this build cannot interpret out-rank a real one and raise the fleet's claim")

# limit_demonstrated is read from the firmware's own flag rather than re-derived
# from the proof, so the roll-up cannot disagree with the verdict that made it.
require("data->limit_demonstrated" in ROLLUP_C,
        "the roll-up must read the firmware's limit_demonstrated flag rather than "
        "infer a demonstrated limit from the proof value")
demonstrated = ROLLUP_C[ROLLUP_C.index("bool write_provenance_limit_demonstrated"):]
require("written_count == 0U" in demonstrated,
        "a fleet nothing has been written to must not report a demonstrated limit")
require("limit_demonstrated_count == rollup->written_count" in demonstrated,
        "the fleet may report a demonstrated limit only when EVERY written "
        "inverter demonstrated one; one echo among eleven demonstrations is still "
        "a plant whose limit is not demonstrated")

# The three evidence figures are three figures. Summing an echo into the
# demonstrated count produces a number that claims more than the evidence does.
for field in ("limit_demonstrated_count", "setpoint_echo_count", "ambiguous_now_count"):
    require(f"uint8_t {field};" in ROLLUP_H, f"the roll-up no longer reports {field}")
require("uint8_t proven_count;" not in ROLLUP_H and "uint8_t evidence_count;" not in ROLLUP_H,
        "the evidence figures must not be merged into one count")

# ------------------------------------------------------------------ unit test
#
# The rule is pure, so it is executed rather than asserted about.
with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "write_provenance_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "tests/support"),
        "-I", str(ROOT / "components/web_server/include"),
        "-I", str(ROOT / "components/inverter_manager/include"),
        str(ROOT / "tests/write_provenance_test.c"),
        str(ROOT / "components/web_server/write_provenance.c"),
        str(ROOT / "components/inverter_manager/inverter_write_confirmation.c"),
        "-lm", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)


# ===========================================================================
# 3. Both endpoints publish it, unconditionally, beside the verdict.
# ===========================================================================

FLEET_KEYS = (
    ('cJSON_AddStringToObject(root, "write_proof"', "what the fleet verdict rests on"),
    ('cJSON_AddBoolToObject(root, "limit_demonstrated"', "whether a limit was demonstrated"),
    ('cJSON_AddBoolToObject(root, "setpoint_echo_only"', "whether any confirmation is an echo"),
    ('cJSON_AddNumberToObject(root, "limit_demonstrated_count"', "the demonstrated count"),
    ('cJSON_AddNumberToObject(root, "setpoint_echo_count"', "the echo-only count"),
    ('cJSON_AddNumberToObject(root, "ambiguous_now_count"', "the ambiguous count"),
    ('cJSON_AddNumberToObject(root, "ambiguous_count"', "the cumulative ambiguous total"),
    ('cJSON_AddNumberToObject(root, "authority_lost_count"', "the authority-lost total"),
    ('cJSON_AddStringToObject(root, "limit_evidence_notice"', "the firmware's own notice"),
)
for needle, label in FLEET_KEYS:
    require(needle in EMIT_C, f"the fleet report omits {label}")

# The notice states the danger in the firmware's own words, so the interface does
# not have to invent safety copy.
notice = re.search(r'LIMIT_EVIDENCE_NOTICE\s*=\s*((?:\s*"[^"]*"\s*)+);', EMIT_C)
require(notice is not None,
        "the firmware must publish a notice explaining the two kinds of evidence "
        "in its own words")
if notice:
    # Whitespace-normalised: the notice is a wrapped C string literal, so asserting
    # the raw concatenation would break on a harmless re-wrap.
    copy = " ".join(" ".join(re.findall(r'"([^"]*)"', notice.group(1))).split())
    require("echo of a stored command" in copy,
            "the notice must say that a setpoint readback can be an echo of a "
            "stored command")
    require("proves acceptance only" in copy,
            "the notice must say the echo case proves acceptance only")
    require("above the new limit before the command and at or below it after" in copy.lower(),
            "the notice must state what a demonstrated limit actually requires")
    require("already at or below it" in copy.lower(),
            "the notice must state the ambiguous case: already below the limit")

# Per-inverter, the four fields the task names, plus the two measurements that let
# the ambiguous verdict be READ rather than taken on trust.
for needle, label in (
    ('cJSON_AddStringToObject(item, "write_proof"', "write_proof"),
    ('cJSON_AddBoolToObject(item, "limit_demonstrated"', "limit_demonstrated"),
    ('cJSON_AddNumberToObject(item, "ambiguous_count"', "ambiguous_count"),
    ('cJSON_AddNumberToObject(item, "authority_lost_count"', "authority_lost_count"),
    ('"measured_power_kw"', "the measurement the verdict was made from"),
    ('"baseline_power_kw"', "the pre-command baseline"),
    ('"baseline_valid"', "whether a baseline exists at all"),
):
    require(needle in EMIT_C, f"the per-inverter report omits {label}")

# A NULL inverter must publish the fail-closed answer rather than omit the keys:
# an absent key reads as "not applicable", which is not "could not be read".
null_branch = EMIT_C[EMIT_C.index("if (!data) {"):EMIT_C.index("cJSON_AddStringToObject(item, \"write_proof\",\n                            inverter_write_proof_name(\n                                (inverter_write_proof_t)data->write_proof));")]
require("INVERTER_WRITE_PROOF_NONE" in null_branch,
        "an unreadable inverter must report the proof that claims the least")
require('cJSON_AddBoolToObject(item, "limit_demonstrated", false)' in null_branch,
        "an unreadable inverter must never report a demonstrated limit")

# Unconditional at all three publishing sites. A brace-depth walk rather than
# trusting indentation.
SITES = (("commissioning_gate_api.c", GATE_API, 2),
         ("solar_grid_status_api.c", SOLAR_API, 1))
for name, text, expected in SITES:
    calls = [m.start() for m in re.finditer(r"write_provenance_add_fleet\(root, &provenance\);", text)]
    require(len(calls) == expected,
            f"{name} must publish the fleet provenance at {expected} site(s), "
            f"found {len(calls)}")
    for at in calls:
        body = text[:at]
        require(body.count("{") - body.count("}") == 1,
                f"{name} publishes the fleet provenance inside a nested block; it "
                "must be unconditional, beside the existing verdict fields")
    require("write_provenance_collect(&provenance);" in text,
            f"{name} must collect the roll-up from the manager's own snapshots")

# Beside the verdict, in the same object, so a client cannot read one without the
# other being there to compare against.
require(GATE_API.index('"fleet_state"') < GATE_API.index("write_provenance_add_fleet(root, &provenance);\n\n    uint8_t count"),
        "the fleet provenance must be published after the verdict it qualifies")
require(SOLAR_API.index('"write_confirmation"') < SOLAR_API.index("write_provenance_add_fleet"),
        "/api/solar-grid/status must publish the provenance beside its existing "
        "write_confirmation verdict")

# Per-inverter provenance is attached to the row it qualifies.
require("write_provenance_add_inverter(item, &data);" in GATE_API,
        "the per-inverter provenance is not attached to the confirmation item, so "
        "a client could read a verdict with no evidence next to it")
require(GATE_API.index('inverter_write_state_name(\n                                    (inverter_write_state_t)data.write_confirmation)')
        < GATE_API.index("write_provenance_add_inverter(item, &data);"),
        "the per-inverter evidence belongs after the verdict it explains")

# No Modbus I/O in an HTTP handler, and none introduced by the new files.
for name, text in (("write_provenance_api.c", EMIT_C),
                   ("commissioning_gate_api.c", GATE_API),
                   ("solar_grid_status_api.c", SOLAR_API)):
    for forbidden in ("inverter_manager_set_total_power_kw",
                      "inverter_manager_probe_read_only",
                      "modbus_master_read", "modbus_master_write",
                      "modbus_tcp_", "vTaskDelay", "portENTER_CRITICAL"):
        require(forbidden not in text,
                f"{name} must not perform blocking work in an HTTP handler: {forbidden}")
require("inverter_manager_get_data(i, &data)" in EMIT_C,
        "the roll-up must read the background task's already-acquired snapshots")

# No new route. esp_http_server refuses registrations past max_uri_handlers and
# every registration here propagates the failure, so an overflow costs the whole
# web interface rather than one endpoint.
require("httpd_register_uri_handler" not in EMIT_C and ".uri =" not in EMIT_C,
        "the provenance must ride the existing endpoints; a new route spends a "
        "handler slot that tests/uri_handler_capacity_source_contract.py reserves")


# ===========================================================================
# 4. The interface: a verdict is never shown without its evidence.
# ===========================================================================

# ------------------------------------------------------------ token parsing
#
# Both themes are parsed from the stylesheets themselves. A token declared in one
# theme only cannot pass, because the lookup will not find it.

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


PROOF_TOKENS = ("--proof-measured", "--proof-echo", "--proof-ambiguous", "--proof-none")

# Every string in these blocks is 10-12px, so the 4.5:1 body threshold applies to
# all of it; none of it qualifies for the 3:1 large-text allowance.
for token in PROOF_TOKENS:
    check_contrast(f"{token} evidence text", token, "--confirm-surface", 4.5)
# The notice takes the echo colour as its rule, and the contention block takes the
# fault colour. Both are non-text boundaries.
check_contrast("limit evidence notice rule", "--proof-echo", "--confirm-surface", 3.0)
check_contrast("contention block rule", "--confirm-mismatched", "--confirm-surface", 3.0)
# The row stripe is a non-text boundary in the same 3:1 class.
for token in ("--proof-measured", "--proof-echo", "--proof-ambiguous"):
    check_contrast(f"{token} row stripe", token, "--confirm-surface", 3.0)
check_contrast("evidence body text", "--muted", "--confirm-surface", 4.5)
check_contrast("contention body text", "--text", "--confirm-surface", 4.5)

for theme, theme_name in ((DARK, "dark"), (LIGHT, "light")):
    values = [theme.get(token) for token in PROOF_TOKENS]
    require(all(values) and len(set(values)) == 4,
            f"{theme_name} theme: the four kinds of limit evidence do not have four "
            "distinct colours; a reader cannot separate evidence painted alike")
    # THE pairing. A demonstrated limit and a stored-command echo painted alike is
    # the whole defect: the weaker of the two would read as the stronger.
    require(theme.get("--proof-measured") != theme.get("--proof-echo"),
            f"{theme_name} theme: a limit demonstrated by measurement and a "
            "setpoint echo are painted the same colour")
    # The echo must not borrow the success hue either. A green pill reading
    # 'Confirmed' is exactly how an accepted command becomes a believed limit.
    require(theme.get("--proof-echo") != theme.get("--confirm-confirmed"),
            f"{theme_name} theme: the setpoint-echo evidence is painted with the "
            "confirmed success colour, which reads as stronger than it is")
    require(theme.get("--proof-echo") != theme.get("--gate-met"),
            f"{theme_name} theme: the setpoint-echo evidence borrows a success hue")
    # Ambiguous is neither success nor failure and must not borrow either.
    require(theme.get("--proof-ambiguous") not in (theme.get("--confirm-confirmed"),
                                                   theme.get("--confirm-mismatched")),
            f"{theme_name} theme: the ambiguous state borrows a success or fault "
            "colour; it is neither")

# -------------------------------------------------------------- the styling
#
# Every colour resolves through a token declared in both themes. A hardcoded hex
# on a themed surface is the exact defect that produced this contrast arithmetic.
proof_css = APP_CSS[APP_CSS.index("/* --------------------------------------------- limit evidence (what confirmed it)"):
                    APP_CSS.index("/* Measurement provenance")]
require("#" not in proof_css,
        "the limit-evidence styling must resolve every colour through a theme token")
require("opacity" not in proof_css,
        "no evidence state may be faded out: 'nothing was proven' is not resolved, "
        "and a dimmed row reads as one that has been dealt with")
proof_declarations = re.sub(r"/\*.*?\*/", "", proof_css, flags=re.S)
require("--muted-2" not in proof_declarations,
        "--muted-2 measures 4.39:1 dark and 3.56:1 light on --confirm-surface and "
        "must not carry text in this panel")
for token in PROOF_TOKENS:
    require(f"var({token})" in proof_css, f"{token} is declared but never used")

# Each kind of evidence has a treatment of its own, and the treatments differ in
# more than hue: a demonstrated limit is the only one drawn as a settled fact.
for state in ("measured_power", "setpoint_readback", "ambiguous_headroom", "none"):
    require(f".proof-state-{state} {{" in proof_css,
            f"the {state} evidence has no treatment of its own")
measured_rule = proof_css.split(".proof-state-measured_power", 1)[1].split("\n", 1)[0]
echo_rule = proof_css.split(".proof-state-setpoint_readback", 1)[1].split("\n", 1)[0]
ambiguous_rule = proof_css.split(".proof-state-ambiguous_headroom", 1)[1].split("\n", 1)[0]
require("border-width: 2px" in measured_rule,
        "the demonstrated limit must be drawn more heavily than the evidence that "
        "claims less; it is the only one that is a settled fact")
require("border-style: dotted" in echo_rule,
        "a setpoint echo must not be drawn as a settled fact: acceptance is not a "
        "limit, and a solid rule would read as one")
require("border-width: 2px" not in echo_rule,
        "the echo must not be drawn as heavily as a demonstrated limit")
require("border-style: dashed" in ambiguous_rule,
        "the ambiguous state must take the 'not measurable' treatment used "
        "everywhere else in this application: it is neither success nor failure")

# The row marking is a THIRD channel. The row border already carries the setpoint
# verdict and the outline already carries the enable register; a mismatched
# setpoint, an unarmed enable register and a mere echo can all be true at once and
# all three must stay visible.
for row_rule, expected in ((".confirm-row.proof-measured", "7px"),
                           (".confirm-row.proof-echo", "4px"),
                           (".confirm-row.proof-ambiguous", "2px")):
    require(row_rule in proof_css, f"{row_rule} has no styling, so it carries no meaning")
    declarations = proof_css.split(row_rule, 1)[1].split("}", 1)[0]
    require("box-shadow" in declarations,
            f"{row_rule} must mark the row on a channel of its own")
    require("border-color" not in declarations and "border-left:" not in declarations,
            f"{row_rule} overwrites the row border, which carries the setpoint "
            "verdict; a mismatched setpoint and a mere echo can both be true")
    require("outline" not in declarations,
            f"{row_rule} takes the outline channel, which already carries the "
            "enable-register state")
    # The stripe width tracks how much the evidence claims, so the row marking is
    # not carried by hue alone.
    require(expected in declarations,
            f"{row_rule} must carry its weight as well as its hue ({expected})")

# ------------------------------------------------------------- the vocabulary

require("const WRITE_PROOF_STATES = Object.freeze({" in APP,
        "there is no closed vocabulary for the kinds of limit evidence")
proof_states = APP[APP.index("const WRITE_PROOF_ORDER"):APP.index("function writeProofMeta")]
for name in ("measured_power", "setpoint_readback", "ambiguous_headroom", "none"):
    require(f"{name}: Object.freeze({{" in proof_states,
            f"limit-evidence state missing: {name}")

# Three independent vocabularies. None of the three may share a label or a glyph
# with either of the others: the dangerous reading is a verdict word standing in
# for an evidence word.
write_states = APP[APP.index("const WRITE_CONFIRMATION_STATES"):
                   APP.index("function writeStateMeta")]
prereq_states = APP[APP.index("const PREREQUISITE_STATE_ORDER"):
                    APP.index("function prerequisiteStateMeta")]
labels = {}
marks = {}
for tag, text in (("verdict", write_states), ("enable register", prereq_states),
                  ("limit evidence", proof_states)):
    labels[tag] = set(re.findall(r"label: '([^']+)'", text))
    marks[tag] = set(re.findall(r"mark: '([^']+)'", text))
require(len(labels["limit evidence"]) == 4,
        "each kind of limit evidence needs its own label; four kinds with fewer "
        "than four labels cannot be told apart")
require(len(marks["limit evidence"]) == 4,
        "each kind of limit evidence needs its own glyph, so evidence never "
        "depends on colour alone")
for other in ("verdict", "enable register"):
    require(not (labels["limit evidence"] & labels[other]),
            f"the limit-evidence and {other} vocabularies share a label: "
            f"{sorted(labels['limit evidence'] & labels[other])}")
    require(not (marks["limit evidence"] & marks[other]),
            f"the limit-evidence and {other} vocabularies share a glyph: "
            f"{sorted(marks['limit evidence'] & marks[other])}")

# What each kind of evidence says, in terms of what the firmware does.
measured_copy = proof_states[proof_states.index("measured_power: Object.freeze({"):
                             proof_states.index("setpoint_readback: Object.freeze({")]
require("above the new limit before the command and at or below it after" in measured_copy,
        "the demonstrated case must state the condition that demonstrates a limit")
require("falling irradiance cannot explain it" in measured_copy,
        "the demonstrated case must say WHY it is proof: the plant was generating "
        "above the limit and then was not")

echo_copy = proof_states[proof_states.index("setpoint_readback: Object.freeze({"):
                         proof_states.index("ambiguous_headroom: Object.freeze({")]
require("stores the command" in echo_copy,
        "the echo case must say the plant-level register stores the command")
require("adjustment coefficient" in echo_copy.lower(),
        "the echo case must say a commanded percentage may be scaled by a "
        "coefficient no Modbus client can read")
require("does not show that the limit is in force" in echo_copy,
        "the echo case must say outright that it does not show the limit is in force")
# It must not be dressed up as more than it is. Only the negated form of "shows
# the limit is in force" may appear, which is why the phrase itself is not banned
# here -- the assertion above pins the negation.
for overclaim in ("confirms the limit", "proves the limit", "demonstrates the limit"):
    require(overclaim not in echo_copy,
            f"the echo case must not read as stronger than it is: {overclaim!r}")

ambiguous_copy = proof_states[proof_states.index("ambiguous_headroom: Object.freeze({"):
                              proof_states.index("none: Object.freeze({")]
require("already at or below it before the command" in ambiguous_copy,
        "the ambiguous case must say the plant was ALREADY below the limit, which "
        "is the whole reason nothing was proven")
require("nothing has been proven" in ambiguous_copy,
        "the ambiguous case must say plainly that nothing has been proven")
require("neither success nor failure" in ambiguous_copy,
        "the ambiguous case must be presented as neither success nor failure")
require("it is not a fault" in ambiguous_copy,
        "the ambiguous case must say it is not a fault; the firmware deliberately "
        "does not demand a safe zero for it, because that would drive PV to zero "
        "every time irradiance fell below the commanded limit")
# Wording an operator can act on.
require("becomes Mismatched" in ambiguous_copy,
        "the ambiguous case must say what escalates it: output later rising above "
        "the limit")
require("treat the limit as not shown to be in force" in ambiguous_copy,
        "the ambiguous case must tell the reader what to do with it")

# Nothing may paraphrase, grade or soften a firmware safety statement.
for invention in ("PROOF_LABELS", "describeProof", "proofFriendly",
                  "proofSeverity", "'Probably limited'", "'Likely limited'"):
    require(invention not in APP,
            f"the interface paraphrases or grades a firmware safety statement: {invention}")

# --------------------------------------------- a verdict never appears alone

require("function proofPill" in APP, "there is no limit-evidence pill")
pill = APP[APP.index("function proofPill"):APP.index("function prerequisitePill")]
require("meta ? name : 'none'" in pill,
        "an unrecognised kind of evidence must fall back to the treatment that "
        "claims the least; falling back to the measured treatment would invent a "
        "demonstrated limit")
require("verbatim(name)" in pill,
        "an unrecognised kind of evidence must be labelled exactly as the "
        "controller sent it")

# The verdict pill and the evidence pill are appended together, in the same line.
require("head.append(title, confirmStatePill(name), proofPill(proofName));" in APP,
        "the verdict and its evidence must be rendered in the same line; a verdict "
        "pill on its own is the defect this panel exists to remove")
# The fleet badge carries both too. 'Fleet: Confirmed' alone is the same defect at
# fleet scale, and a fleet confirmed on an echo must not take the success tone.
# The window ends at the FIRST confirmFleetDetail write AFTER the badge, not the
# first in the file. There is now an earlier one in the no-report branch, and
# anchoring on it silently produced an empty window -- a contract that checks
# nothing while reporting that everything it checks is missing.
_badge_start = APP.index("setBadgeIfChanged('confirmFleetBadge',\n            `Fleet:")
badge = APP[_badge_start:
            APP.index("setTextIfChanged('confirmFleetDetail',", _badge_start)]
require("proofMeta ? proofMeta.label" in badge,
        "the fleet badge must name the evidence beside the verdict")
require("demonstrated ? 'good' : 'warning'" in badge,
        "a fleet confirmed on a setpoint echo must not take the success tone")
require("payload.limit_demonstrated === true" in APP,
        "limit_demonstrated must be read explicitly; truthiness would make a "
        "missing field read as a demonstrated limit")

require("function provenanceBlock" in APP,
        "nothing renders the per-inverter limit evidence")
require("provenanceBlock(entry)" in APP,
        "the per-inverter evidence is not attached to the confirmation row it "
        "qualifies, so a reader could see the verdict without it")
block = APP[APP.index("function provenanceBlock"):APP.index("function prerequisiteBlock")]
require("did not report what this verdict rests on" in block,
        "a verdict with no reported evidence must be stated as such, not silently "
        "omitted, and nothing on the row may then be read as a limit in force")
require("entry.limit_demonstrated === true ? 'yes' : 'no'" in block,
        "the per-inverter block must state whether the limit was demonstrated, "
        "read explicitly")
require("Baseline before the command" in block,
        "the pre-command baseline must be shown: without it a limit can never be "
        "demonstrated, only found consistent")
require("entry.baseline_valid === true" in block,
        "a missing baseline must read as none recorded, never as zero output")
# A null measurement must not be rendered as zero: zero output would itself look
# like a limit being obeyed.
require("function formatEvidenceKw" in APP,
        "there is no formatter that keeps a missing measurement missing")
kw = APP[APP.index("function formatEvidenceKw"):APP.index("function setTextIfChanged")]
require("value == null || !Number.isFinite(number) ? absent" in kw,
        "a missing measurement must stay missing; Number(null) is 0 and rendering "
        "it as 0.00 kW would look like a limit being obeyed")

# The row is marked by its evidence as well as by its verdict.
for marking in ("row.classList.add('proof-measured')", "row.classList.add('proof-echo')",
                "row.classList.add('proof-ambiguous')"):
    require(marking in APP, f"the row is not marked by its evidence: {marking}")

# Contention is its own finding and is not folded into a confirmation state.
require("proof-block-contention" in APP,
        "an authority loss must be stated in words rather than left as a counter")
require("will fight this controller" in block,
        "an authority loss must say what it means: another master is scheduling "
        "this plant")
# A lost count of zero means either "never taken" or "never checked", and those
# are not the same finding. Whether the target publishes the register at all must
# be stated, and the current holding must have an explicit unknown.
require("entry.authority?.supported === true" in block,
        "a zero authority-lost count is ambiguous unless the interface says "
        "whether the target publishes an authority register at all")
require("contention cannot be detected" in block,
        "a target with no authority register must be reported as one where "
        "contention cannot be detected, not as one with no contention")
require("'Not checked'" in block and "the register could not be read" in block,
        "whether this controller currently holds scheduling authority needs an "
        "explicit unknown; a boolean would make 'never read' look like 'no'")

# --------------------------------------- prerequisite fields already published
#
# These were published by the firmware and shown nowhere. With the enable state at
# "not confirmed" they are the difference between waiting for the next poll and
# waiting forever, which is the same transient-versus-permanent distinction
# tests/prerequisite_reporting_source_contract.py protects everywhere else.
prereq_block = APP[APP.index("function prerequisiteBlock"):
                   APP.index("/* All four kinds of limit evidence")]
require("prerequisite.write_issued === true" in prereq_block,
        "the per-inverter enable block must say whether the controller has even "
        "attempted to arm the register yet")
require("prerequisite.describable === true" in prereq_block,
        "the per-inverter enable block must say whether a writable and readable "
        "prerequisite can be described at all")
require('cJSON_AddBoolToObject(prerequisite, "describable"' in GATE_API
        and 'cJSON_AddBoolToObject(prerequisite, "write_issued"' in GATE_API,
        "the firmware no longer publishes the two enable-register facts the block "
        "above renders")

# --------------------------------------------- fleet rendering and the legends

require("function renderWriteProvenance" in APP,
        "nothing renders the fleet limit evidence")
require("renderWriteProvenance(payload)" in APP,
        "the fleet limit evidence is never rendered on the confirmation panel")
fleet_render = APP[APP.index("function renderWriteProvenance"):
                   APP.index("/* Fleet roll-up for the enable register")]
require("Unknown evidence is not a demonstrated limit" in fleet_render,
        "a missing evidence report must be stated as not demonstrated rather than "
        "left blank, which would read as nothing to worry about")
require("payload.setpoint_echo_only === true" in fleet_render,
        "the echo-only fleet answer must be read explicitly")
require("this fleet as accepted, not as limited" in fleet_render,
        "an echo-only fleet must be described as accepted rather than limited")
require("Weakest evidence held by any written inverter" in fleet_render,
        "the fleet must name the weakest evidence any written inverter holds: a "
        "fleet is only ever as well evidenced as its least well evidenced member")

require("function provenanceCountLine" in APP, "the fleet evidence counts are not shown")
counts = APP[APP.index("function provenanceCountLine"):APP.index("function renderWriteProvenance")]
require("acceptance, not a limit" in counts,
        "the echo count must say what it is: acceptance, not a limit")
require("nothing proven" in counts,
        "the ambiguous count must say that nothing was proven")
require(counts.count("payload?.") >= 4,
        "the evidence figures must be reported as separate figures, not summed")

require("function renderProofLegend" in APP,
        "the four kinds of limit evidence are never explained")
require("WRITE_PROOF_ORDER" in APP,
        "there is no declared order for the limit-evidence legend")

# The commissioning gate says whether control is permitted; it must also say what
# the confirmations behind that permission rest on.
require("function renderGateLimitEvidence" in APP,
        "the commissioning gate panel never reports the limit evidence")
gate_render = APP[APP.index("function renderGateLimitEvidence"):
                  APP.index("/* ------------------------------------------------ setpoint write confirmation")]
require("gate.limit_demonstrated === true" in gate_render,
        "the gate row must read limit_demonstrated explicitly")
require("gate.setpoint_echo_only === true" in gate_render,
        "the gate row must distinguish an echo-only fleet")
require("has not been shown to be in force" in gate_render,
        "the gate row must say outright that an accepted command is not a limit "
        "shown to be in force")
require("renderGateLimitEvidence(gate);" in APP,
        "the gate evidence row is never rendered")

# ------------------------------------------------------- markup and copy

require('id="proofNotice"' in INDEX,
        "the confirmation panel does not carry the limit-evidence warning")
require('id="proofLegend"' in INDEX,
        "the four kinds of limit evidence are never explained on the panel")
require('id="confirmProvenance"' in INDEX and 'id="confirmProvenanceCounts"' in INDEX,
        "the panel has nowhere to report the fleet evidence")
require('id="confirmProvenanceDetail"' in INDEX,
        "the panel has nowhere to render the controller's own notice verbatim")
require('id="gateLimitEvidence"' in INDEX,
        "the gate panel does not report the limit evidence as a row of its own")

# The danger is in the markup itself, so it is on screen before any poll returns
# and regardless of what happens to be latched.
require("Confirmed does not mean the limit was demonstrated." in INDEX,
        "the confirmation panel must state outright that confirmed is not the "
        "same claim as a demonstrated limit")
proof_notice = INDEX[INDEX.index('id="proofNotice"'):INDEX.index('id="confirmProvenanceCounts"')]
require("echo of a stored command" in proof_notice,
        "the panel must say that a plant-level readback is an echo of a stored command")
require("proves acceptance and nothing more" in proof_notice,
        "the panel must say the echo proves acceptance and nothing more")
require("adjustment coefficient" in proof_notice.lower(),
        "the panel must say a commanded percentage may be scaled by a coefficient "
        "no Modbus client can read")
require("shown with the evidence it rests on" in proof_notice,
        "the panel must state its own guarantee: every verdict carries its evidence")

# The controller's own wording, rendered as received rather than rewritten.
require("payload.limit_evidence_notice" in APP,
        "the controller's own limit-evidence notice is not rendered")
require("setNoticeLine('confirmProvenanceDetail'" in APP,
        "the controller's notice must be written as received rather than composed "
        "into a sentence of this interface's own")

# Neither an echo nor an ambiguous reading may ever be described as reassuring.
for phrase in ("no action required", "safe to ignore", "will clear itself",
               "harmless", "as good as confirmed", "close enough"):
    require(phrase not in INDEX.lower(),
            f"limit evidence must never be described as {phrase!r}")
    require(phrase not in proof_states.lower(),
            f"limit evidence must never be described as {phrase!r}")


# ------------------------------------------------------------ CI registration

require("tests/confirmation_provenance_source_contract.py" in WORKFLOW,
        "this contract is not registered in the build workflow")
require("tests/write_provenance_test.c" in WORKFLOW,
        "the roll-up unit test is not registered in the build workflow")


if failures:
    import sys
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("confirmation provenance source contract passed "
      "(the weakest-first fleet roll-up is pure and executed, both endpoints "
      "publish write_proof, limit_demonstrated and the ambiguous and "
      "authority-lost counts unconditionally beside the verdict with no Modbus "
      "I/O, a demonstrated limit and a stored-command echo are distinguishable in "
      "hue, weight, glyph and words, the ambiguous case is presented as neither "
      "success nor failure with an action attached, and no verdict is rendered "
      "without its evidence at AA contrast in both themes)")
