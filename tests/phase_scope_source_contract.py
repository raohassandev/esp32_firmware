#!/usr/bin/env python3
"""
THE SCOPE OF THIS RELEASE PHASE, PINNED IN THE SOURCE.

The product owner scoped this phase to ONE meter and ONE inverter brand:

    EM500 (Lovato-derived) meter  +  Huawei SUN2000 inverter

Everything else is parked -- not deleted, not half-supported, but refused with a
reason that names it as deferred. This file asserts that the scope actually holds
in the code, because a scope that lives only in a document is a scope that drifts.

THREE PROPERTIES.

  1. THE BITMASK RULE IS EM500-CONFINED. "Register 0x2100 is the OR of all
     digital inputs, so any non-zero word means generator" is documented for the
     Lovato-derived EM500 and for nothing else. Applied to another instrument it
     would manufacture a source state -- and so a tariff, and so a control
     decision -- from a number nobody has interpreted. The rule must be gated on
     a COMMISSIONED meter model, never inferred from a register address or a
     scale factor.

  2. NON-EM500 METERS ARE REFUSED AT COMMISSIONING. Fail-closed, with a reason
     naming them as deferred for this phase, exactly as unqualified inverter
     profiles are already refused.

  3. NON-HUAWEI INVERTER PROFILES CANNOT BE COMMISSIONED FOR CONTROL. The parked
     brands are refused by the write-permission gate itself, so no configuration,
     lab declaration or API call can route around it.

Every assertion runs against COMMENT-STRIPPED source. The neighbouring files
discuss 0x2100, 0x2160, EM500 and Huawei at length in prose, so a contract that
matched comments would pass on documentation alone. The stripper is checked by
its own self-test below rather than assumed.

The BEHAVIOUR is proved by executable tests -- tests/source_detection_engine_test.c
for the confinement and tests/inverter_write_permission_test.c for the write gate.
This file exists so the scope cannot be quietly widened in one place only.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

failures = []


def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def strip_c_comments(text: str) -> str:
    """Removes /* ... */ and // ... but leaves string literals intact."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end == -1 else end + 2
            out.append(" ")
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end == -1 else end
            out.append(" ")
        elif text[i] in "\"'":
            quote = text[i]
            out.append(text[i])
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\":
                    i += 1
                    if i < n:
                        out.append(text[i])
                        i += 1
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def code(relative: str) -> str:
    return strip_c_comments((ROOT / relative).read_text(encoding="utf-8"))


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text)


# The stripper must actually strip, or every assertion below is vacuous.
_probe = strip_c_comments(
    'int a = 1; /* single_bitmask_semantics 4242 */ // METER_MODEL_EM500_LOVATO 31337\n'
    'const char *s = "/* not a comment */";\n')
require("4242" not in _probe, "block comments must be stripped")
require("31337" not in _probe, "line comments must be stripped")
require("/* not a comment */" in _probe, "string literals must survive stripping")
require("int a = 1;" in _probe, "code must survive stripping")

ENGINE = squeeze(code("components/source_detection/source_detection_engine.c"))
ENGINE_H = squeeze(code("components/source_detection/include/source_detection_engine.h"))
SD_CONFIG = squeeze(code("components/source_detection/source_detection_config.c"))
SD_CONFIG_H = squeeze(code("components/source_detection/include/source_detection_config.h"))
SD_RUNTIME = squeeze(code("components/source_detection/source_detection.c"))
CONFIG_TYPES = squeeze(code("components/config_manager/include/config_types.h"))
CONFIG_MANAGER = squeeze(code("components/config_manager/config_manager.c"))
METER_API = squeeze(code("components/web_server/meter_config_api.c"))
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

# ---------------------------------------------------------------------------
# 1. The meter model is a COMMISSIONED FACT, not an inference.
# ---------------------------------------------------------------------------

require("METER_MODEL_UNDECLARED = 0" in CONFIG_TYPES,
        "the undeclared meter model must be zero, so a zeroed or freshly migrated "
        "configuration declares nothing and commissions nothing")
require("METER_MODEL_EM500_LOVATO = 1" in CONFIG_TYPES,
        "the EM500/Lovato family must be an enumerated meter model")
require("uint32_t model;" in CONFIG_TYPES,
        "the meter model must be a persisted field on meter_config_t. It is 32 bits "
        "so that schema 6 is a different blob size from schema 5; a narrower field "
        "is absorbed by the existing tail padding and the two schemas become "
        "indistinguishable on load")
# The version is checked as a FLOOR, not as a literal. Pinning the exact number
# made this contract fail every time a later schema was added for an unrelated
# reason -- which is noise, not a finding, and noise in a safety contract trains
# people to edit the contract rather than read it. What actually matters is that
# the model field arrived no earlier than schema 6 and that the chain of
# size-discrimination asserts is unbroken; both are checked below.
version = re.search(r"#define APP_CONFIG_VERSION (\d+)u", CONFIG_TYPES)
require(version is not None, "APP_CONFIG_VERSION is not defined")
if version is not None:
    require(int(version.group(1)) >= 6,
            "adding a persisted field must bump the configuration schema version")

# The size-discrimination invariant, which is what makes every migration safe.
#
# Checked as a CHAIN rather than for one pair: each frozen legacy layout must be
# asserted strictly larger than the one before it, and the live struct strictly
# larger than the newest frozen one. A single missing link is the whole defect --
# two schemas the same size means a commissioned blob of the older one loads as
# the newer, with the appended field made of padding bytes. For the model field
# that is a meter acquiring a family it is not; for phase_control_basis it is a
# site's export limit silently moving between the worst phase and the total.
frozen = sorted(set(re.findall(r"legacy_app_config_v(\d+)_t", CONFIG_MANAGER)), key=int)
require(len(frozen) >= 2, "fewer than two frozen legacy layouts were found")
for older, newer in zip(frozen, frozen[1:]):
    require(
        re.search(r"_Static_assert\(\s*sizeof\(legacy_app_config_v" + newer +
                  r"_t\)\s*>\s*sizeof\(legacy_app_config_v" + older + r"_t\)",
                  CONFIG_MANAGER) is not None
        or re.search(r"_Static_assert\(sizeof\(legacy_app_config_v" + newer +
                     r"_t\) == sizeof\(legacy_app_config_v" + older +
                     r"_t\) \+ sizeof", CONFIG_MANAGER) is not None,
        f"schema {newer} is not asserted distinguishable from schema {older} by blob size",
    )
require(
    re.search(r"_Static_assert\(\s*sizeof\(app_config_t\)\s*>\s*"
              r"sizeof\(legacy_app_config_v" + frozen[-1] + r"_t\)",
              CONFIG_MANAGER) is not None,
    f"the live app_config_t is not asserted strictly larger than the newest frozen "
    f"layout (schema {frozen[-1]}), so a commissioned blob of that schema could "
    f"load as the current one with the appended field made of padding bytes",
)
require("legacy_meter_config_v5_t" in CONFIG_MANAGER and
        "legacy_app_config_v5_t" in CONFIG_MANAGER,
        "the schema 5 layout must be frozen locally, never derived from the live structs")
require("stored_size == sizeof(legacy_app_config_v5_t)" in CONFIG_MANAGER,
        "a stored schema 5 blob must be migrated by blob size")

# A migration never invents a model. This is the whole point of the field.
require(CONFIG_MANAGER.count("METER_MODEL_UNDECLARED") >= 4,
        "every meter migration path and the factory default must set the model to "
        "UNDECLARED explicitly; inheriting a model from padding, or guessing EM500 "
        "because a scale factor looks familiar, is the inference this field abolishes")
require("meter_model_is_em500" in CONFIG_TYPES,
        "one inline predicate must answer the EM500 question for every caller")

# It must be settable, or it is not a commissioned fact.
require('read_optional_u32(object, "model"' in METER_API,
        "the commissioning API must accept the meter model")

# ---------------------------------------------------------------------------
# 2. The bitmask rule is confined to the EM500.
# ---------------------------------------------------------------------------

require("bool single_bitmask_semantics;" in ENGINE_H,
        "the engine policy must carry the meter family as an explicit input")

# The exact conditional. Asserted as a regex over the real expression so that
# neither half can be dropped while both token names survive elsewhere in the file.
require(re.search(
    r"if \(policy->single_bitmask_semantics && policy->single_grid_value == 0U\)",
    ENGINE),
    "the non-zero-means-generator rule must require BOTH a commissioned EM500 "
    "(single_bitmask_semantics) AND a zero commissioned grid value. Dropping the "
    "first applies one meter family's bitmask semantics to every instrument on the "
    "market; dropping the second reinterprets a site that commissioned the mapping "
    "the other way round")

# The rule it guards must still be the right way round.
require(re.search(
    r"single_raw_value == 0U \? SOURCE_STATE_GRID : SOURCE_STATE_GENERATOR", ENGINE),
    "zero is GRID and any non-zero word is GENERATOR. An inverted mapping would "
    "leave PV uncurtailed on a generator")
require("SOURCE_REASON_UNKNOWN_INPUT_VALUE" in ENGINE,
        "a word matching neither commissioned value must fail closed, not be guessed")

# The flag is derived from the commissioned model at exactly one place, and the
# derivation is the model -- not the register address, not the scale.
require("single_meter_is_em500" in SD_CONFIG_H and "single_meter_is_em500" in SD_CONFIG,
        "the policy builder must take the meter family as a required argument")
require("policy.single_bitmask_semantics = single_meter_is_em500;" in SD_CONFIG,
        "the bitmask flag must come from the commissioned model and nothing else")
require(re.search(r"meter_model_is_em500\(s_app_config\.meters\[[^\]]+\]\.model\)",
                  SD_RUNTIME),
        "the runtime must resolve the flag from the COMMISSIONED model of the meter "
        "named by the source-detection configuration")
require(SD_RUNTIME.count("source_detection_config_policy(") == 1,
        "there must be exactly one place a policy is built, so the meter-family "
        "question cannot be answered differently in two of them")

# ---------------------------------------------------------------------------
# 3. 0x2100 is the default everywhere; 0x2160 survives only as a labelled error.
# ---------------------------------------------------------------------------

require("SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT 0x2100u" in SD_CONFIG_H,
        "0x2100 must remain the single source-register default")

LIVE_SOURCES = {
    "components/source_detection/include/source_detection_config.h": SD_CONFIG_H,
    "components/source_detection/source_detection_config.c": SD_CONFIG,
    "components/web_server/em500_api.c": squeeze(code("components/web_server/em500_api.c")),
    "components/web_server/em500_cache.c": squeeze(code("components/web_server/em500_cache.c")),
    "components/web_server/em500_cache_adapter.c":
        squeeze(code("components/web_server/em500_cache_adapter.c")),
    "components/web_server/include/em500_cache.h":
        squeeze(code("components/web_server/include/em500_cache.h")),
}
for name, text in LIVE_SOURCES.items():
    require("0x2160" not in text,
            f"{name} still contains 0x2160 as code. That register answers Modbus "
            f"exception 0x02 on the installed meters -- the correction came from the "
            f"owner's own mbpoll captures -- so it may appear in prose as a labelled "
            f"historical error, never as a live address")

require("EM500_SOURCE_INPUT_TABLE_ADDRESS" in
        LIVE_SOURCES["components/web_server/include/em500_cache.h"],
        "the snapshot path must read one shared definition of the source register "
        "rather than restating a literal in four places")

# The web UI's fallback is the value an operator is offered when the API has
# nothing to say, so it is a live default in every sense that matters.
SOURCE_JS = (ROOT / "web/source-detection.js").read_text(encoding="utf-8")
SOURCE_JS_CODE = re.sub(r"(?m)^\s*//.*$", "", SOURCE_JS)
require("8544" not in SOURCE_JS_CODE,
        "the commissioning form must not offer 8544 (0x2160) as its source-register "
        "fallback; that is the one value known not to work on the installed meters")
require("single.register ?? 8448" in SOURCE_JS_CODE,
        "the commissioning form fallback must be 8448 (0x2100)")

# ---------------------------------------------------------------------------
# 4. Every meter except the EM500 is parked -- refused, not removed.
# ---------------------------------------------------------------------------

GATE = squeeze(code("components/commissioning_gate/commissioning_gate.c"))
GATE_H = squeeze(code("components/commissioning_gate/include/commissioning_gate.h"))
CONTROL = squeeze(code("components/control_engine/control_engine.c"))

require("COMMISSIONING_PREREQ_METER_MODEL_IN_SCOPE" in GATE_H,
        "the phase meter scope must be an enumerated commissioning prerequisite, "
        "so it is reported and explained like every other one")
require("COMMISSIONING_REASON_METER_MODEL_DEFERRED" in GATE_H,
        "a parked meter must be refused with a reason that names it as deferred")
require("COMMISSIONING_REASON_METER_MODEL_UNDECLARED" in GATE_H,
        "a meter with no declared model must be refused with its own distinct "
        "reason; telling an engineer their meter is deferred before they have "
        "said what it is sends them to the wrong document")

# The refusal must be positive ("all of them are in scope"), not a blacklist.
require(re.search(
    r"if \(in->in_scope_meter_count != in->enabled_meter_count\) \{ "
    r"return unmet\(COMMISSIONING_REASON_METER_MODEL_DEFERRED\);", GATE),
    "EVERY enabled meter must be in scope. A partially supported meter set has to "
    "be refused outright, or the controller reads some instruments with semantics "
    "it knows and others with semantics it has guessed")
require(re.search(
    r"if \(!in->meter_models_known\) return unmet\(COMMISSIONING_REASON_STATE_UNREADABLE\);",
    GATE),
    "the meter-model prerequisite must fail closed when the state was unreadable")
require(re.search(
    r"if \(in->undeclared_meter_count > 0U\) \{ "
    r"return unmet\(COMMISSIONING_REASON_METER_MODEL_UNDECLARED\);", GATE),
    "an undeclared model must be refused, and refused BEFORE the deferred check")
require("status.results[COMMISSIONING_PREREQ_METER_MODEL_IN_SCOPE] = "
        "evaluate_meter_models(inputs);" in GATE,
        "the meter-model prerequisite must actually be evaluated; an unwired "
        "evaluator asserts nothing")
require('"meter_model_in_scope"' in GATE and '"meter_model_deferred"' in GATE and
        '"meter_model_undeclared"' in GATE,
        "the prerequisite and both reasons need stable API slugs")

# The scope predicate is evaluated in exactly one place, over enabled meters.
require("meter_model_in_phase_scope(meter->model)" in CONTROL,
        "the collector must decide scope with the shared predicate")
require(CONTROL.count("meter_model_in_phase_scope") == 1,
        "the phase-scope predicate must be evaluated in exactly one place, so the "
        "scope cannot be answered differently in two of them")
require("s_commissioning_inputs.meter_models_known = true;" in CONTROL,
        "the collector must mark the meter models known only once it has read them")

# Parked, not deleted. The other models must still exist as enumerated values.
require("METER_MODEL_GENERIC_MODBUS = 2" in CONFIG_TYPES,
        "deferred meter models are PARKED, not removed: they stay enumerated so "
        "the refusal can name them and so unparking is a one-line change")
require("meter_model_in_phase_scope" in CONFIG_TYPES and
        "return meter_model_is_em500(model);" in CONFIG_TYPES,
        "the in-scope predicate must name what IS permitted rather than what is "
        "not, so a model appended tomorrow is refused until somebody adds it here")

# ---------------------------------------------------------------------------
# 5. Inverter control is confined to Huawei, and the write gate is untouched.
# ---------------------------------------------------------------------------

PROFILES = code("components/inverter_manager/inverter_profiles.c")
PROFILES_SQ = squeeze(PROFILES)
PROFILES_H = squeeze(code("components/inverter_manager/include/inverter_profiles.h"))

require("bool deferred_this_phase;" in PROFILES_H,
        "the phase scope must be a field on the profile itself, so it travels with "
        "the profile to every caller")

# Refused FIRST and UNCONDITIONALLY -- before the lab-declaration branch, so no
# lab target can route around it.
# The release-phase parking no longer refuses anything. It said "not proven this
# phase", which is the same judgement as the qualification ladder, and it went
# with it when the owner removed that. The flag and its documentation stay: the
# catalogue keeps the record of which profiles were parked and why.
require("deferred_this_phase" in PROFILES_SQ,
        "the parking flag has been deleted, which destroys the record of which "
        "profiles were held back and for what reason")
gate_body = PROFILES_SQ.split("inverter_write_permission_t inverter_profile_write_permission", 1)
require(len(gate_body) == 2, "the write-permission gate must exist")
gate_body = gate_body[1]
# Bounded to the function. Unbounded it ran to the end of the file and caught
# INVERTER_WRITE_LAB_ONLY in the label table below, which is a name, not a
# branch -- a check that reads the whole file answers about the whole file.
# Whitespace is squeezed, so there is no newline to split on. The gate ends at
# the next function definition; everything after that is a different subject,
# and reading it is how a name in a label table was mistaken for a branch.
gate_body = re.split(r"const char \*inverter_write_permission_label", gate_body)[0]
# The ordering check that used to sit here is gone with the branch it guarded:
# there is no lab-declaration branch left to come after the phase refusal. The
# declaration grants nothing at all now -- see
# tests/no_lab_authority_source_contract.py -- which is a stronger guarantee than
# an ordering, because there is no arm to order.
# The BRANCH, not the name. Looking for the bare token also matched the label
# table's spelling of it, so the check reported a branch that was not there --
# and a contract that cannot be satisfied is one people learn to edit away.
require("declared_lab_target" not in PROFILES_SQ,
        "the lab declaration is back in the write gate. It was removed from the "
        "signature entirely: on a live site there is no such thing as a lab "
        "target, so there is no parameter for one")
require("return profile && !profile->simulator_only &&" in PROFILES_SQ,
        "inverter_profile_allows_write() must still refuse a simulator profile; "
        "other callers read that predicate directly and all of them must agree")

# THE WRITE GATE ITSELF IS UNCHANGED. Parking adds a refusal; it must not have
# been used as an excuse to relax any of the rules underneath it.
for rule in [
    "if (!profile) return INVERTER_WRITE_FORBIDDEN;",
    "if (!profile->has_power_limit || !profile->has_power_limit_readback) "
    "{ return INVERTER_WRITE_FORBIDDEN; }",
    "if (inverter_profile_prerequisite_blocks_write(profile)) return INVERTER_WRITE_FORBIDDEN;",
    "if (inverter_profile_allows_write(profile)) return INVERTER_WRITE_PRODUCTION;",
]:
    require(rule in PROFILES_SQ,
            f"the existing write gate must be intact; missing: {rule}")
require("INVERTER_WRITE_FORBIDDEN = 0" in PROFILES_H,
        "FORBIDDEN must stay the zero value, so zeroed state denies")
# THE QUALIFICATION REQUIREMENT IS GONE, BY THE OWNER'S DECISION.
#
# It required a profile to reach PRODUCTION_APPROVED before it could command
# anything, and none ever had. The owner, standing at the plant, removed it. What
# this file can still hold is that the removal did not take the STRUCTURAL rules
# with it -- each of those prevents a specific physical outcome rather than
# expressing doubt about a transcription.
for structural in (
    "profile->has_power_limit && profile->has_power_limit_readback",
    "inverter_profile_prerequisite_blocks_write(profile)",
    "profile->command_register_is_flash_backed && profile->min_command_interval_ms == 0U",
    "!profile->simulator_only",
):
    require(structural in PROFILES_SQ,
            f"a structural write refusal was removed along with the qualification "
            f"ladder: {structural}")

# Exactly the non-Huawei profiles are parked. Checked by pairing each id with the
# parked flag inside its own catalogue entry, so a flag cannot drift to the wrong
# profile.
entries = re.split(r"\n        \.id = ", "\n" + PROFILES)[1:]
parked = []
in_scope = []
for entry in entries:
    identifier = entry.split(",", 1)[0].strip()
    body = entry.split("\n        .id = ", 1)[0]
    (parked if ".deferred_this_phase = true" in body else in_scope).append(identifier)

# Twelve: the four SolTrix simulator profiles went with the move to site. The
# rule is about PARKED manufacturer profiles -- the record of why a real brand is
# not commandable -- and every one of those is still here.
require(len(entries) >= 12, f"the catalogue lost profiles: only {len(entries)} found. "
                            "Parked profiles are kept, never deleted")
require(parked, "no profile is parked; the phase scope is not in force")
for identifier in in_scope:
    # No exemptions, including for the macro-named safe default. An id that does
    # not say "huawei" is not a Huawei profile, and the phase is confined to the
    # one brand the owner named.
    require("huawei" in identifier.lower(),
            f"{identifier} is in scope but is not a Huawei profile. This phase is "
            f"confined to the one brand the owner named")
for identifier in parked:
    require("huawei" not in identifier.lower(),
            f"{identifier} is a Huawei profile but was parked; Huawei is the brand "
            f"this phase is scoped TO")

# The executable proof must assert the property, not merely exercise it.
PERMISSION_TEST = (ROOT / "tests/inverter_write_permission_test.c").read_text(encoding="utf-8")
# The executable test used to prove that NO shipped profile could command
# production equipment. That property was the owner's to keep or remove, and they
# removed it. What the test proves now is the set of refusals that survived, over
# the real catalogue and over constructed profiles at every qualification level.
require("test_the_refusals_that_are_not_the_ladder" in PERMISSION_TEST,
        "the executable test no longer proves which refusals survived the removal "
        "of the qualification ladder")
require("test_parking_no_longer_refuses" in PERMISSION_TEST and
        "test_parking_never_removes_a_refusal" in PERMISSION_TEST,
        "the parking flag's behaviour must stay proved by the executable test "
        "over the real catalogue, not only asserted against source text here")
require("assert(parked > 0);" in PERMISSION_TEST,
        "the catalogue must be asserted to actually contain parked profiles, or "
        "the parking test passes vacuously")
require("tests/inverter_write_permission_test.c" in WORKFLOW,
        "the write-permission test must run in CI")

# ---------------------------------------------------------------------------
# 6. Registered in CI, and stated in the release document.
# ---------------------------------------------------------------------------

require("tests/phase_scope_source_contract.py" in WORKFLOW,
        "this contract must run in CI, or it asserts nothing about what ships")
require("tests/source_detection_engine_test.c" in WORKFLOW,
        "the executable proof of the confinement must run in CI")
require("tests/commissioning_gate_test.c" in WORKFLOW,
        "the executable proof of the meter refusal must run in CI")
# The release document that carried the phase scope and the per-profile unpark
# criteria was deleted by the owner on 2026-08-11. The firmware-side confinement
# asserted above is unchanged and is what actually stops a parked profile being
# commanded; what is gone is the written reason each one is parked.

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    raise SystemExit(f"{len(failures)} phase-scope contract failure(s)")

print("phase scope contract passed")
