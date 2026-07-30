#!/usr/bin/env python3
"""A base-loaded engine's setpoint may only be believed on commissioned evidence.

THE HOLE THIS CLOSES
--------------------
Base-load sharing computes the minimum-loading floor as

    sum(base setpoints) + sum_swing(rated) x max_swing(percent) / 100

The first term is not arithmetic, it is a claim about a governor: that each
base-loaded engine is holding the kW it was commissioned to hold. If that governor
drops out of kW control -- lost load-sharing line, switched to droop, reverted to
isochronous, put in manual -- the engine silently becomes a swing engine and the
floor is computed from a setpoint nobody is holding. The error is in the PERMISSIVE
direction: the controller believes the base engines are absorbing kW they are not and
allows more PV than the plant can carry. That is the reverse-power condition this
product exists to prevent.

The observation needed to detect it already existed -- measured_kw with sample_fresh
per engine. What was missing was a tolerance, and NO MANUAL, NAMEPLATE OR SITE
DOCUMENT IN THIS REPOSITORY STATES ONE.

WHAT THIS CONTRACT PINS
-----------------------
  * NO TOLERANCE IS INVENTED. No numeric literal is assigned to either tolerance
    field anywhere in the firmware. A default here would be a fabricated safety
    number, which is worse than a closed gate.
  * NOT CONFIGURED IS EXPLICIT AND FAILS CLOSED. Base-load sharing with a
    base-loaded engine is REFUSED until a tolerance exists, by the pure limit module
    and by the commissioning gate, with the same finding in both. The alternative --
    reporting the check unavailable and computing the floor anyway -- would leave a
    running plant on the assumption this change exists to remove.
  * UNKNOWN IS NOT CONFIRMATION. A missing, stale or non-finite measurement never
    reads as agreement.
  * A DRIFTING ENGINE IS NOT RECLASSIFIED. The module must not move the engine into
    the swing term and carry on; it knows the governor is not doing what it was
    commissioned to do and knows nothing about what it is doing instead.
  * THE FLOOR USES THE COMMISSIONED SETPOINT, not the measurement. The measurement is
    evidence about the setpoint, not a substitute for it.
  * THE CHECK IS FREE FOR EVERY OTHER PLANT. Isochronous sharing, a single-engine
    site, and base-load sharing over an all-swing fleet must not read the tolerance
    at all, and the 20 ms control loop must gain no blocking I/O.
  * THE REASONS ARE EXECUTED, NOT ASSERTED. Every new code is exercised by a host
    test that runs the real function against a brute-force reference.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLEET_H = (ROOT / "components/control_engine/include/generator_fleet_limit.h").read_text(encoding="utf-8")
FLEET_C = (ROOT / "components/control_engine/generator_fleet_limit.c").read_text(encoding="utf-8")
CONTROL_C = (ROOT / "components/control_engine/control_engine.c").read_text(encoding="utf-8")
GATE_H = (ROOT / "components/commissioning_gate/include/commissioning_gate.h").read_text(encoding="utf-8")
GATE_C = (ROOT / "components/commissioning_gate/commissioning_gate.c").read_text(encoding="utf-8")
SG_H = (ROOT / "components/solar_grid_config/include/solar_grid_config.h").read_text(encoding="utf-8")
SG_C = (ROOT / "components/solar_grid_config/solar_grid_config.c").read_text(encoding="utf-8")
FLEET_TEST = (ROOT / "tests/generator_fleet_limit_test.c").read_text(encoding="utf-8")
GATE_TEST = (ROOT / "tests/commissioning_gate_test.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


def section(source, start, end, label):
    if start not in source:
        failures.append(f"{label}: cannot find {start!r}")
        return ""
    begin = source.index(start)
    if end not in source[begin:]:
        failures.append(f"{label}: cannot find {end!r} after {start!r}")
        return source[begin:]
    return source[begin:source.index(end, begin)]


# ======================================================= the tolerance is commissioned
#
# Stated either absolutely or as a percentage of that engine's own rating, or both --
# the same "state either or both" shape inverter_write_confirmation.h already solves
# for measured_tolerance_kw / measured_tolerance_percent_of_capacity.

for token in ("base_load_tolerance_kw", "base_load_tolerance_percent_of_rating"):
    require(token in FLEET_H,
            f"the aggregate limit's input must carry {token}: a tolerance the module "
            f"cannot see is a check it cannot perform")
    require(token in SG_H,
            f"the persisted policy must carry {token}, or the tolerance cannot survive "
            f"a reboot and therefore cannot be commissioned")
require("generator_base_load_tolerance_kw" in GATE_H and
        "generator_base_load_tolerance_percent_of_rating" in GATE_H,
        "the commissioning gate must see the tolerance, or it cannot refuse a "
        "base-loaded plant that has none")

# Readable through accessors that report anything unusable as zero, so a corrupt stored
# value can never present itself as a commissioned tolerance.
for accessor in ("solar_grid_config_base_load_tolerance_kw",
                 "solar_grid_config_base_load_tolerance_percent"):
    require(f"{accessor}(" in SG_H and f"{accessor}(" in SG_C,
            f"{accessor}() must exist so no reader touches the stored field directly")
    require(f"{accessor}(&s_grid_config)" in CONTROL_C,
            f"the control engine must read the tolerance through {accessor}(), not from "
            f"the stored struct")


# ================================================================ nothing is invented
#
# The whole point of the change: the check is made POSSIBLE without a number being
# fabricated. A literal assigned to either field in firmware would be exactly that.

for name, text in (("generator_fleet_limit.c", FLEET_C),
                   ("commissioning_gate.c", GATE_C),
                   ("solar_grid_config.c", SG_C),
                   ("control_engine.c", CONTROL_C)):
    for assignment in re.findall(r"(?:\.|->|\b)(\w*base_load_tolerance\w*)\s*=\s*([^;\n]+)",
                                 text):
        field, value = assignment
        stripped = value.strip()
        allowed = (stripped in ("0.0f", "0.0F", "-1.0f")
                   or stripped.startswith("solar_grid_config_base_load_tolerance")
                   or stripped.startswith("error == ESP_OK")
                   or stripped.startswith("tolerance")
                   or stripped.startswith("in->")
                   or stripped.startswith("input->")
                   or stripped.startswith("config->"))
        require(allowed,
                f"{name} assigns {stripped!r} to {field}. A base-load setpoint "
                f"tolerance may only be zero (not commissioned) or read from the "
                f"commissioned configuration. No manual, nameplate or site document in "
                f"this repository states a figure, so a literal here is a fabricated "
                f"safety number")

# solar_grid_config_defaults() must leave it at the memset zero.
DEFAULTS = section(SG_C, "void solar_grid_config_defaults(", "\n}\n", "defaults")
require("base_load_tolerance" not in DEFAULTS,
        "solar_grid_config_defaults() must not put a tolerance in a fresh unit")


# ========================================================= not configured fails closed
#
# The decision, pinned in the code that acts on it: base-load sharing with a
# base-loaded engine is REFUSED without a tolerance, rather than being computed with
# the check reported as unavailable. Both layers must agree, or an engineer meets a
# controller holding PV at zero while the gate says commissioned.

require("GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET" in FLEET_H,
        "the aggregate limit must have its own reason for an uncommissioned tolerance; "
        "folding it into the setpoint-unknown reason would send an engineer to the "
        "wrong field")
require("return fail_closed(GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET);" in FLEET_C,
        "an uncommissioned tolerance must FAIL CLOSED. Skipping the check would leave "
        "the floor resting on an assumption nothing can test, and that assumption fails "
        "permissively")
require("COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET" in GATE_H and
        "COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET" in GATE_C,
        "the commissioning gate must refuse a base-loaded plant with no tolerance, with "
        "its own reason")

# The reason must carry an operator-facing sentence, not a slug, and it must say which
# direction the error runs in -- an engineer who reads it as an accuracy nicety will not
# treat it as a safety number.
GATE_MESSAGES = section(GATE_C, "REASON_MESSAGES[COMMISSIONING_REASON_COUNT] = {", "};",
                        "gate REASON_MESSAGES")
GATE_IDS = section(GATE_C, "REASON_IDS[COMMISSIONING_REASON_COUNT] = {", "};",
                   "gate REASON_IDS")
gate_slugs = re.findall(r'"([a-z_]+)"', GATE_IDS)
gate_messages = re.findall(r'"((?:[^"\\]|\\.)*)"', GATE_MESSAGES)
require(len(gate_slugs) == len(gate_messages),
        "every commissioning reason slug must have a message beside it, or one reason "
        "would be reported under another's sentence")
GATE_TEXT = dict(zip(gate_slugs, gate_messages))
require("generator_base_load_tolerance_unset" in GATE_TEXT,
        "the uncommissioned-tolerance reason must publish a slug")
sentence = GATE_TEXT.get("generator_base_load_tolerance_unset", "")
require(len(sentence) > 80,
        "the uncommissioned-tolerance reason must carry a full operator-facing sentence")
require("setpoint" in sentence and "more PV" in sentence,
        "that sentence must say what goes wrong -- the controller permitting more PV "
        "than the plant can carry -- not merely that a field is missing")

# The gate must ask for it ONLY where it has a referent, or every isochronous and
# single-engine site would be closed for a quantity that means nothing to them.
GATE_LIMITS = section(GATE_C, "static commissioning_prereq_result_t evaluate_generator_limits(",
                      "static commissioning_prereq_result_t evaluate_tuning(",
                      "evaluate_generator_limits")
require("base_loaded > 0U" in GATE_LIMITS,
        "the tolerance must be required only when an in-service engine is actually "
        "base-loaded: base-load sharing over an all-swing fleet has no setpoint to "
        "check and reduces exactly to isochronous")
require(GATE_LIMITS.index("COMMISSIONING_REASON_GENERATOR_NO_SWING_ENGINE") <
        GATE_LIMITS.index("COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET"),
        "a plant with nothing absorbing the swing must report THAT, not the tolerance: "
        "the more fundamental fault comes first")


# ============================================================ unknown is not agreement

require("GENERATOR_FLEET_BASE_LOAD_UNMEASURED" in FLEET_H,
        "a base-loaded engine with no usable measurement must have its own reason: "
        "silence about a governor is not evidence about it")
require("return fail_closed(GENERATOR_FLEET_BASE_LOAD_UNMEASURED);" in FLEET_C,
        "a missing, stale or non-finite measurement on a base-loaded engine must fail "
        "closed, never read as agreement")
BASE_BLOCK = section(FLEET_C, "GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET",
                     "base_load_total_kw += engine->base_load_kw;", "base-load checks")
for guard in ("!engine->metered", "!engine->sample_fresh", "!isfinite(engine->measured_kw)"):
    require(guard in BASE_BLOCK,
            f"the measurement guard must test {guard}; each is a different way for the "
            f"observation to be absent and all three must refuse")


# ==================================================== drift refuses, and does not adapt

require("GENERATOR_FLEET_BASE_LOAD_DRIFT" in FLEET_H,
        "disagreement beyond the tolerance must have its own reason code")
require("return fail_closed(GENERATOR_FLEET_BASE_LOAD_DRIFT);" in FLEET_C,
        "disagreement beyond the tolerance must fail closed: the floor's first term is "
        "not supported, so there is no floor to report")
require("fabsf(engine->measured_kw - engine->base_load_kw) > band_kw" in FLEET_C,
        "the comparison must be SYMMETRIC. Measuring below the setpoint is the "
        "permissive error; measuring above it is equally a governor not under kW "
        "control, and neither is agreement")
# The drifting engine must not be quietly moved into the swing term. If it were, this
# reason would be unreachable and the assignment below would appear in the base-load
# branch.
require("engine->role = GENERATOR_ENGINE_ROLE_SWING" not in FLEET_C,
        "the module must never reclassify a drifting base-loaded engine as a swing "
        "engine and carry on: it knows the governor left kW control and knows nothing "
        "about what it is doing instead, and droop, isochronous and manual give three "
        "different floors")
# The floor must be built from the commissioned setpoint, not from the measurement --
# otherwise the floor follows the very quantity it is meant to police.
require("base_load_total_kw += engine->base_load_kw;" in FLEET_C and
        "base_load_total_kw += engine->measured_kw" not in FLEET_C,
        "the floor must sum the COMMISSIONED setpoints. Summing measured power instead "
        "would make the floor track a drifting governor rather than detect it")


# ============================================== the band rule, stated once and executed

BAND = section(FLEET_C, "float generator_base_load_tolerance_band_kw(", "\n}\n", "band")
require("generator_base_load_tolerance_band_kw(" in FLEET_H,
        "the band rule must be exposed so a host test can execute it rather than a "
        "comment assert it")
require("percent_band_kw < band_kw" in BAND,
        "when both figures are commissioned the NARROWER band must win")
require("tolerance_percent_of_rating <= 100.0f" in BAND,
        "a percentage above the engine's whole rating is not a tolerance and must not "
        "be clamped into one")
require("tolerance_kw > 0.0f" in BAND,
        "a zero band on a physical measurement is not a tolerance, it is a bug, and "
        "must read as nothing commissioned")
# One implementation. A second copy would be a second answer.
require(FLEET_C.count("percent_band_kw") >= 1 and
        CONTROL_C.count("tolerance_band_kw") == 0,
        "the band must be computed in exactly one place; the control engine must copy "
        "the commissioned figures through and interpret nothing")

# The reason the rule differs from the inverter module's must be recorded, or the next
# reader will "fix" it into agreement.
require("inverter_write_confirmation" in FLEET_H,
        "the header must name the module whose 'wider band' rule this deliberately "
        "inverts, or the divergence reads as an inconsistency to be tidied away")
require("one side" in FLEET_H.lower() and "both sides" in FLEET_H.lower(),
        "the header must state WHY the rules differ: that band sits on both sides of "
        "its test, this one on a single comparison, so widening is purely permissive "
        "here")


# ======================================================= no cost to any other plant

# The 20 ms control loop must gain no blocking I/O. The tolerance is read from the
# already-cached configuration snapshot, exactly as the sharing mode is.
FLEET_INPUT = section(CONTROL_C, "generator_fleet_input_t fleet_input = {",
                      "fleet_limit = generator_fleet_limit_evaluate(&fleet_input);",
                      "fleet input")
require("solar_grid_config_base_load_tolerance_kw(&s_grid_config)" in FLEET_INPUT,
        "the tolerance must be supplied to the limit evaluation, or the check never runs")
for blocking in ("meter_manager_read_registers", "vTaskDelay", "nvs_get", "nvs_open"):
    require(blocking not in FLEET_INPUT,
            f"the control loop's fleet input must not call {blocking}: the loop period "
            f"is 20 ms and this path reads cached state only")

# The pure module must stay pure, so the whole rule is host-testable.
for forbidden in ("esp_log", "ESP_LOG", "freertos", "malloc", "nvs_"):
    require(forbidden not in FLEET_C,
            f"generator_fleet_limit.c must stay free of {forbidden}: it is executed by a "
            f"host unit test and called from the control loop")


# ============================================================= migration cannot lose it

require("legacy_solar_grid_config_v4_t" in SG_C,
        "the pre-tolerance layout must be a frozen snapshot, or a commissioned "
        "multi-engine plant falls back to defaults and loses every rating it holds")
require("_Static_assert(offsetof(solar_grid_config_t, base_load_tolerance_kw) ==" in SG_C,
        "the appended tolerance must be proven to leave schema 4 a byte-exact prefix")
require("Migrated Solar-Grid configuration schema 4" in SG_C,
        "a schema 4 unit must be migrated rather than replaced by defaults")
MIGRATION = section(SG_C, "if (error == ESP_OK && size == sizeof(legacy_solar_grid_config_v4_t))",
                    "Schema 3 predates", "schema 4 migration")
require("loaded.base_load_tolerance_kw = 0.0f;" in MIGRATION,
        "the upgrade must arrive with NO tolerance commissioned. Migrating a plausible "
        "figure to preserve the numbers would be writing a commissioning fact nobody "
        "supplied -- the same mistake as migrating the sharing mode to isochronous")


# ============================================ the reasons are executed, not asserted

# Slug table and enum must stay the same length, or a reason is reported under another's
# name. (Checked here as well as in the multi-engine contract because this change adds
# three at once.)
ENUM_BLOCK = section(FLEET_H, "typedef enum {\n    GENERATOR_FLEET_OK",
                     "} generator_fleet_reason_t;", "generator_fleet_reason_t")
# The `(?:\s*=\s*\d+)?` matters: GENERATOR_FLEET_OK is written `= 0,` to pin the
# success value, and a regex demanding a bare `NAME,` silently dropped it. That made
# the length check compare 13 enumerators against 14 slugs and fail on correct code --
# and, worse, it would have compared the wrong two sets even when the counts agreed.
enum_names = [n for n in re.findall(r"^\s{4}(GENERATOR_FLEET_[A-Z_]+)(?:\s*=\s*\d+)?\s*,",
                                    ENUM_BLOCK, re.MULTILINE)
              if n != "GENERATOR_FLEET_REASON_COUNT"]
IDS_BLOCK = section(FLEET_C, "REASON_IDS[GENERATOR_FLEET_REASON_COUNT] = {", "};",
                    "REASON_IDS")
slugs = re.findall(r'"([a-z_]+)"', IDS_BLOCK)
require(len(enum_names) == len(slugs),
        f"the fleet reason enum ({len(enum_names)}) and its slug table ({len(slugs)}) "
        f"must stay the same length")
for slug in ("base_load_tolerance_unset", "base_load_unmeasured",
             "base_load_setpoint_drift"):
    require(slug in slugs, f"the aggregate limit must publish the reason slug {slug}")

# Every new reason must be reached by a host test that runs the real function. A reason
# nothing exercises is a reason nothing proves.
for reason in ("GENERATOR_FLEET_BASE_LOAD_TOLERANCE_UNSET",
               "GENERATOR_FLEET_BASE_LOAD_UNMEASURED",
               "GENERATOR_FLEET_BASE_LOAD_DRIFT"):
    require(reason in FLEET_TEST,
            f"tests/generator_fleet_limit_test.c must exercise {reason} against the real "
            f"function")
require("COMMISSIONING_REASON_GENERATOR_BASE_LOAD_TOLERANCE_UNSET" in GATE_TEST,
        "tests/commissioning_gate_test.c must exercise the uncommissioned-tolerance "
        "refusal against the real gate")

# The brute-force reference must judge the base-load premise independently, or the test
# is comparing the module against a restatement of itself.
require("base_load_premise_supported" in FLEET_TEST,
        "the host test's brute-force reference must decide for itself whether the "
        "base-load premise is supported by the evidence; a reference that assumes the "
        "setpoint is held cannot detect a module that assumes the same thing")
# And the preservation claim must be proved by exact equality, not by inspection.
require("bit_for_bit_unchanged" in FLEET_TEST,
        "the host test must prove isochronous and single-engine results are bit-for-bit "
        "unchanged across every value the tolerance fields can hold")


# ================================================================== CI, or nothing
require("tests/base_load_drift_source_contract.py" in WORKFLOW,
        "this contract must be registered in the CI workflow to be worth anything")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("base-load setpoint drift contract passed "
      f"({len(slugs)} aggregate-limit reasons, {len(gate_slugs)} gate reasons; no "
      "tolerance is invented anywhere in the firmware)")
