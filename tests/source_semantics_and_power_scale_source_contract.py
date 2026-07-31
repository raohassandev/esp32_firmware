"""
The owner-stated facts this product turns on, pinned in the source.

  1. EM500 register 0x2100 carries the tariff/digital input.
     Non-zero means GENERATOR. Zero means GRID.
  2. Huawei power limit: register 40125, scale 10 (45 % is the word 450).
     Solis power limit: scale 100 (100 % is the word 10000).

Every assertion below runs against COMMENT-STRIPPED source, so a register address
or scale factor quoted in prose -- and this file's neighbours quote a great many,
including the superseded 0x2160 and the wrong 45 -- can never satisfy a contract
about what the code does. The stripping is checked by its own self-test at the
bottom rather than assumed.

These are cross-artifact contracts. The behaviour itself is proved by
tests/source_transition_e2e_test.c and tests/inverter_power_scale_test.c; this
file exists so the numbers cannot be quietly changed in one place only.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


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
    'int a = 1; /* raw_units_per_percent = 999.0f */ // power_limit_address = 12345\n'
    'const char *s = "/* not a comment */";\n')
require("999.0f" not in _probe, "block comments must be stripped")
require("12345" not in _probe, "line comments must be stripped")
require("/* not a comment */" in _probe, "string literals must survive stripping")
require("int a = 1;" in _probe, "code must survive stripping")

ENGINE = squeeze(code("components/source_detection/source_detection_engine.c"))
CONFIG_H = squeeze(code("components/source_detection/include/source_detection_config.h"))
CONFIG = squeeze(code("components/source_detection/source_detection_config.c"))
CONTROL = squeeze(code("components/control_engine/control_engine.c"))
SOURCE_MODE = squeeze(code("components/control_engine/source_mode.c"))
POLICY = squeeze(code("components/control_engine/power_control_policy.c"))
PROFILES = code("components/inverter_manager/inverter_profiles.c")
SIM = squeeze(strip_c_comments(
    (ROOT / "tools/soltrix_modbus_simulator.js").read_text(encoding="utf-8")))
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

# ---------------------------------------------------------------------------
# 1. The EM500 tariff register and its semantics.
# ---------------------------------------------------------------------------

require("SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT 0x2100u" in CONFIG_H,
        "the tariff register default must be 0x2100 in code, not only in prose; "
        "0x2160 is superseded and returns illegal data address on the real meters")
require("0x2160" not in CONFIG_H,
        "the superseded register must not appear as code in the config header")

# Non-zero = GENERATOR, zero = GRID. Asserted as the actual conditional so an
# inversion cannot pass by keeping both token names present somewhere in the file.
require(re.search(
    r"single_raw_value\s*==\s*0U\s*\?\s*SOURCE_STATE_GRID\s*:\s*SOURCE_STATE_GENERATOR",
    ENGINE),
    "the single-input rule must read: zero is GRID, any non-zero word is "
    "GENERATOR. An inverted mapping would leave PV uncurtailed on a generator")
require("policy->single_grid_value == 0U" in ENGINE,
        "the non-zero-is-generator rule must be gated on a zero commissioned grid "
        "value, so a site with a bespoke non-zero mapping keeps strict equality")
require("SOURCE_REASON_UNKNOWN_INPUT_VALUE" in ENGINE,
        "a word matching neither commissioned value on a non-bitmask site must "
        "still fail closed rather than be guessed")

# Fail-closed remains fail-closed.
require("result.control_allowed = result.state != SOURCE_STATE_UNKNOWN;" in ENGINE,
        "an unresolved source must never be allowed to command")
require("result.fail_closed = !result.control_allowed;" in ENGINE,
        "fail_closed must be the exact negation of control_allowed")
require("SOURCE_REASON_DEBOUNCE_PENDING" in ENGINE and
        ">= policy->debounce_ms" in ENGINE,
        "a source transition must satisfy the commissioned debounce interval")
require("config->debounce_ms = 0U" in CONFIG and
        "config->stale_timeout_ms = 0U" in CONFIG,
        "site timing must stay uncommissioned rather than invented")

# ---------------------------------------------------------------------------
# 2. Detection reaches the control engine and decides curtailment.
# ---------------------------------------------------------------------------

require("source_detection_get_status(&detection)" in CONTROL,
        "the control loop must consume the source-detection status")
require("detection.state == SOURCE_STATE_GRID" in CONTROL and
        "MEASURED_SOURCE_GRID" in CONTROL,
        "a detected grid source must reach the control loop as MEASURED_SOURCE_GRID")
require("detection.state == SOURCE_STATE_GENERATOR" in CONTROL and
        "MEASURED_SOURCE_GENERATOR" in CONTROL,
        "a detected generator source must reach the control loop as "
        "MEASURED_SOURCE_GENERATOR")
require("!detection.fail_closed" in CONTROL and "!detection.transition_pending" in CONTROL,
        "a fail-closed or still-debouncing detection must not be treated as fresh")
require(re.search(
    r"case MEASURED_SOURCE_GENERATOR:\s*result\.mode = SOURCE_MODE_GENERATOR_ONLY;",
    SOURCE_MODE),
    "a measured generator source must map to SOURCE_MODE_GENERATOR_ONLY")
require(re.search(
    r"case MEASURED_SOURCE_GRID:\s*result\.mode = SOURCE_MODE_GRID_ONLY;", SOURCE_MODE),
    "a measured grid source must map to SOURCE_MODE_GRID_ONLY")
require("input->generator_safe_limit_kw < maximum" in POLICY and
        "output.curtailed_by_generator = true" in POLICY,
        "on a generator the safe-PV ceiling must clamp the setpoint and say so")
require("input->reverse_power_margin_kw" in SOURCE_MODE,
        "the reverse-power margin must remain part of the generator floor")

# ---------------------------------------------------------------------------
# 3. Power-limit scale factors, per the owner's statement.
# ---------------------------------------------------------------------------


def profile_block(profile_id: str) -> str:
    start = PROFILES.find(f'.id = "{profile_id}"')
    require(start != -1, f"profile {profile_id} is missing from the catalogue")
    end = PROFILES.find(".id = ", start + 1)
    return squeeze(PROFILES[start:end if end != -1 else len(PROFILES)])


huawei = profile_block("huawei.sun2000.pending")
require(".power_limit_address = 40125," in huawei,
        "Huawei must command the power limit at register 40125")
require(".raw_units_per_percent = 10.0f," in huawei,
        "Huawei must use scale 10: 45 % is the word 450, 100 % is 1000")
require(".power_limit_readback_address = 40125," in huawei,
        "Huawei must read the active limit back from 40125")
require(".power_limit_readback_scale = 0.1f," in huawei,
        "the Huawei readback scale must be the exact reciprocal of the command scale")

solis = profile_block("solis.commercial.pending")
require(".raw_units_per_percent = 100.0f," in solis,
        "Solis must use scale 100: 100 % is the word 10000")
require(".power_limit_readback_scale = 0.01f," in solis,
        "the Solis readback scale must be the exact reciprocal of the command scale")

# The two stated brands must not share a scale.
require(".raw_units_per_percent = 10.0f," not in solis,
        "Solis must not carry the Huawei scale")
require(".raw_units_per_percent = 100.0f," not in huawei,
        "Huawei must not carry the Solis scale")

# ---------------------------------------------------------------------------
# 4. No profile may become production-approved without physical evidence.
# ---------------------------------------------------------------------------

# Asserted on the designated initialiser, not on the bare token: the token also
# appears -- correctly -- in the write-permission rule and the label function,
# and an assertion that forbade it there would forbid the gate itself.
QUALIFICATION_INITIALISERS = re.findall(
    r"\.qualification\s*=\s*(INVERTER_PROFILE_QUALIFICATION_\w+)", squeeze(PROFILES))
require(QUALIFICATION_INITIALISERS,
        "the profile catalogue must declare a qualification for its entries")
require("INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" not in QUALIFICATION_INITIALISERS,
        "no shipped profile may claim production qualification without physical "
        "readback evidence; simulator agreement qualifies a profile for LAB use only")

for profile_id in ("huawei.sun2000.pending", "solis.commercial.pending"):
    block = profile_block(profile_id)
    require(".qualification = INVERTER_PROFILE_QUALIFICATION_DOCUMENTED," in block,
            f"{profile_id} carries no physical readback evidence, so it must stay "
            "DOCUMENTED and remain refused by the production write gate")

# The gate that enforces it must still be present and still keyed on the same
# qualification, or the assertions above would be describing a dead field.
require("profile->qualification == INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED"
        in squeeze(PROFILES),
        "the production write permission must remain keyed on the profile's "
        "qualification")

# ---------------------------------------------------------------------------
# 5. The lab rig must serve the tariff where the firmware polls for it.
# ---------------------------------------------------------------------------

sim_match = re.search(r"const EM500_SOURCE_ADDRESS = (0[xX][0-9a-fA-F]+);", SIM)
require(sim_match, "the simulator must define EM500_SOURCE_ADDRESS")
firmware_match = re.search(
    r"SOURCE_DETECTION_SINGLE_REGISTER_DEFAULT (0[xX][0-9a-fA-F]+)u?", CONFIG_H)
require(firmware_match, "the firmware must define the tariff register default")
require(int(sim_match.group(1), 16) == int(firmware_match.group(1), 16),
        "the simulator serves the tariff at a register the firmware never polls; "
        "every scenario would resolve to GRID and the curtailment path would never "
        "be exercised")

# ---------------------------------------------------------------------------
# 6. The proofs must run in CI.
# ---------------------------------------------------------------------------

for artefact in (
    "source_transition_e2e_test.c",
    "inverter_power_scale_test.c",
    "source_detection_engine_test.c",
    "inverter_write_permission_test.c",
    "source_semantics_and_power_scale_source_contract.py",
    "soltrix_modbus_simulator_test.js",
):
    require(artefact in WORKFLOW, f"CI must run {artefact}")

require(re.search(r"node tools/soltrix_modbus_simulator_test\.js", WORKFLOW),
        "the simulator test must be EXECUTED in CI, not merely syntax-checked")

print("source semantics and power-limit scale source contract passed")
