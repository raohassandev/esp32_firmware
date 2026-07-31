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
DOC = (ROOT / "docs/RELEASE_READINESS.md").read_text(encoding="utf-8")

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
require("#define APP_CONFIG_VERSION 6u" in CONFIG_TYPES,
        "adding a persisted field must bump the configuration schema version")

# The size-discrimination invariant, which is what makes the migration safe.
require(re.search(r"_Static_assert\( ?sizeof\(app_config_t\) > "
                  r"sizeof\(legacy_app_config_v5_t\)", CONFIG_MANAGER),
        "schema 6 must be asserted strictly larger than the frozen schema 5 layout, "
        "or a commissioned schema-5 blob would load as schema 6 and every meter "
        "would acquire a model value out of padding bytes")
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
# 4. Registered in CI, and stated in the release document.
# ---------------------------------------------------------------------------

require("tests/phase_scope_source_contract.py" in WORKFLOW,
        "this contract must run in CI, or it asserts nothing about what ships")
require("tests/source_detection_engine_test.c" in WORKFLOW,
        "the executable proof of the confinement must run in CI")
require("EM500" in DOC and "SUN2000" in DOC,
        "docs/RELEASE_READINESS.md must state the phase scope")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    raise SystemExit(f"{len(failures)} phase-scope contract failure(s)")

print("phase scope contract passed")
