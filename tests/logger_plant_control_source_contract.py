#!/usr/bin/env python3
"""Plant-level control at the Huawei SmartLogger, and the honesty of the
measured-power confirmation it depends on.

Commanding the plant at the logger was deliberately NOT implemented for a long
time, and the reason was specific: reading the plant percentage register back
returns the value the logger STORED, not the plant's achieved state, and an
undocumented "Adjustment coefficient" means a commanded 80 % need not deliver
80 %. A profile confirming on that echo would report `confirmed` for a limit that
is not in force -- the same false-confirmation defect that already required
refusing four brands.

So confirmation had to be able to close on MEASURED power. That is where this
contract earns its place, because measured power is a weak witness in exactly one
direction and it is easy to get wrong in a way that looks fine:

    Output BELOW a commanded limit is equally consistent with
        (a) the limit being honoured, and
        (b) the sun going in.

A controller that reports (b) as `confirmed` has invented a limit it never
demonstrated, and every layer above it -- including the operator -- is then told
the plant is under control when it is not. This contract EXECUTES the real
evaluator against that ambiguity rather than describing it, because a Python
mirror of a safety rule is one more thing that can drift out of step with the
firmware, and that drift has already happened once on this project.

It also pins the register values against their citations, because a plant-level
write is one write that moves an entire PV array.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
PROFILES_H = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
CONFIRM_H = (ROOT / "components/inverter_manager/include/inverter_write_confirmation.h").read_text(encoding="utf-8")
CONFIRM_C = (ROOT / "components/inverter_manager/inverter_write_confirmation.c").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
TYPES = (ROOT / "components/inverter_manager/include/inverter_types.h").read_text(encoding="utf-8")

PROFILE_ID = "huawei.smartlogger.plant"

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ---------------------------------------------------------------------------
# 1. Execute the real rules: the catalogue and the confirmation evaluator
# ---------------------------------------------------------------------------

PROBE = ROOT / "tests/support/logger_plant_control_probe.c"


def run_probe():
    gcc = shutil.which("gcc")
    if not gcc:
        raise AssertionError("gcc is required: this contract executes the real "
                             "confirmation rule rather than reimplementing it")
    with tempfile.TemporaryDirectory() as tmp:
        binary = pathlib.Path(tmp) / "probe"
        subprocess.run(
            [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "tests/support"),
             "-I", str(ROOT / "components/inverter_manager/include"),
             str(PROBE),
             str(ROOT / "components/inverter_manager/inverter_profiles.c"),
             str(ROOT / "components/inverter_manager/inverter_status.c"),
             str(ROOT / "components/inverter_manager/inverter_write_confirmation.c"),
             "-lm", "-o", str(binary)],
            check=True, capture_output=True)
        out = subprocess.run([str(binary)], check=True, capture_output=True,
                             text=True).stdout
    profiles, cases = {}, {}
    for line in out.splitlines():
        if not line.strip():
            continue
        fields = line.split("\t")
        if fields[0] == "PROFILE":
            profiles[fields[1]] = {
                "qualification": fields[2].strip().lower(),
                "authority": fields[3].strip(),
                "production": fields[4] == "1",
                "measured_mode": fields[5],
                "measured_described": fields[6] == "1",
                "tolerance_kw": float(fields[7]),
                "tolerance_pct": float(fields[8]),
                "authority_described": fields[9] == "1",
                "authority_function": int(fields[10]),
                "authority_address": int(fields[11]),
                "authority_expected": int(fields[12]),
                "command_interval_ms": int(fields[13]),
                "settle_ms": int(fields[14]),
            }
        elif fields[0] == "CASE":
            cases[fields[1]] = {
                "state": fields[2],
                "demonstrated": fields[3] == "1",
                "proof": fields[4],
                "safe_zero": fields[5] == "1",
                "settled": fields[6] == "1",
            }
    return profiles, cases


compiled, cases = run_probe()

# --- The crux, executed. ---------------------------------------------------

# A limit IS demonstrated when output was above it before the command and at or
# below it after. Nothing weaker may ever produce this verdict.
require(cases["demonstrated"]["state"] == "confirmed",
        "a limit demonstrated by measurement must confirm; got "
        f"{cases['demonstrated']}")
require(cases["demonstrated"]["demonstrated"] and
        cases["demonstrated"]["proof"] == "measured_power",
        "a measured demonstration must be reported as such, so that 'confirmed' "
        f"is never read without knowing what confirmed it; got {cases['demonstrated']}")

# Falling irradiance must NOT read as a confirmed limit. This single assertion is
# the reason this file exists.
for case in ("falling_irradiance", "no_baseline", "ambiguous_with_matching_echo",
             "full_output"):
    verdict = cases[case]
    require(verdict["state"] == "unverified",
            f"{case}: measured output below a commanded limit that was ALREADY "
            "below it proves nothing -- it is equally consistent with the limit "
            "being honoured and with the sun going in -- so the verdict must be "
            f"unverified, not '{verdict['state']}'")
    require(not verdict["demonstrated"],
            f"{case}: claims a demonstrated limit from evidence that cannot "
            "distinguish an honoured limit from falling irradiance")
    require(verdict["proof"] == "ambiguous_headroom",
            f"{case}: the reason must be reported as ambiguous_headroom so an "
            f"operator can tell it from an unknown state; got {verdict['proof']}")
    # And it must not demand the safe fallback: driving PV to zero whenever
    # irradiance dips below the commanded limit is worse than the ambiguity.
    require(not verdict["safe_zero"],
            f"{case}: an ambiguous verdict must not demand a safe zero, or the "
            "plant is driven to zero on every cloud")

# A matching echo must never rescue the ambiguity for this command target.
require(cases["ambiguous_with_matching_echo"]["proof"] == "ambiguous_headroom",
        "a matching setpoint readback promoted an ambiguous measurement; for a "
        "stored-command echo that is the false confirmation this whole path "
        "exists to prevent")

# The direction that IS unambiguous, and the one that protects the generator.
require(cases["above_limit"]["state"] == "mismatched" and
        cases["above_limit"]["safe_zero"],
        "output above the commanded limit past the settle window must be a "
        "mismatch demanding the safe fallback: no change in irradiance can lift a "
        f"plant above a limit that is in force; got {cases['above_limit']}")

# Contention: another master owning plant scheduling outranks a good measurement.
require(cases["contention"]["state"] == "mismatched" and
        cases["contention"]["safe_zero"],
        "a foreign scheduling authority after our own command must fault even "
        f"when the measurement would have confirmed; got {cases['contention']}")
require(cases["authority_held"]["state"] == "confirmed",
        "holding scheduling authority must not block an otherwise demonstrated "
        f"limit; got {cases['authority_held']}")

# Incompletely described measured evidence is refused, never reverted to the echo.
for case in ("no_tolerance_stated", "no_capacity"):
    require(cases[case]["state"] == "unverified" and cases[case]["safe_zero"],
            f"{case}: incompletely described measured evidence must be refused "
            "outright; falling back to the setpoint echo would turn a "
            f"transcription slip into a false confirmation; got {cases[case]}")

# Zeroed state, the permanent invariant of this module.
require(cases["zeroed"]["state"] == "unverified" and
        not cases["zeroed"]["demonstrated"] and
        cases["zeroed"]["proof"] == "none",
        f"a zeroed evidence struct must never read as confirmed; got {cases['zeroed']}")
require("INVERTER_WRITE_UNVERIFIED = 0" in CONFIRM_H,
        "UNVERIFIED must stay the zero value so zeroed state is never confirmed")
require("INVERTER_MEASURED_CONFIRM_NONE = 0" in CONFIRM_H,
        "the measured-confirmation mode must default to NONE, so a profile "
        "written before this existed keeps exactly the behaviour it had")
require("INVERTER_WRITE_PROOF_NONE = 0" in CONFIRM_H,
        "the proof enum must default to NONE, which claims the least")

# ---------------------------------------------------------------------------
# 2. The profile exists, is honest about its qualification, and is described
#    completely enough for the evaluator to use
# ---------------------------------------------------------------------------

require(PROFILE_ID in compiled,
        f"{PROFILE_ID} is not in the compiled catalogue")

if PROFILE_ID in compiled:
    facts = compiled[PROFILE_ID]
    require(facts["qualification"] == "documented",
            f"{PROFILE_ID} must be Documented: nothing in it has been exercised "
            f"against a physical SmartLogger; got '{facts['qualification']}'")
    require(facts["measured_mode"] == "required",
            "the plant profile's confirmation must close on measured power and "
            "must never be able to confirm on the setpoint echo alone; mode is "
            f"'{facts['measured_mode']}'")
    require(facts["measured_described"],
            "the plant profile declares measured-power confirmation but does not "
            "describe it completely (an active-power register and a positive "
            "tolerance band are both required)")
    require(facts["tolerance_kw"] > 0.0 or facts["tolerance_pct"] > 0.0,
            "a zero tolerance band on a physical measurement is not a tolerance")
    # The documented minimum command interval, quoted in the profile: "the
    # adjustment value should be issued at intervals of not less than 1 seconds".
    require(facts["command_interval_ms"] == 1000,
            "the plant interface documents a minimum command interval of 1 s; "
            f"the profile states {facts['command_interval_ms']} ms")
    # The settle window must be at least that interval: the plant cannot have
    # settled faster than the interface may be re-commanded, and a window shorter
    # than the truth faults healthy equipment.
    require(facts["settle_ms"] >= 1000,
            "the settle window must be at least the documented 1 s command "
            "interval, or a plant still ramping down is judged a mismatch; got "
            f"{facts['settle_ms']} ms")
    # Contention detector, read-only.
    require(facts["authority_described"],
            "the plant profile must describe the scheduling-authority register, "
            "which is how a second master taking the plant over is detected")
    require(facts["authority_function"] in (3, 4),
            "the scheduling-authority register must be read with a read-only "
            f"function code; got {facts['authority_function']}")
    require(facts["authority_address"] == 40737,
            "the scheduling-authority register is 40737 'Active power control "
            "mode' (SL-MB PDF p.17 (9) SN54); got "
            f"{facts['authority_address']}")
    require(facts["authority_expected"] == 4,
            "40737 == 4 is 'Remote scheduling', the value that means this "
            f"controller owns the plant; got {facts['authority_expected']}")

# THE OWNER REMOVED THE QUALIFICATION LADDER, so profiles now do pass the
# production write gate. That was their decision, made at the plant.
#
# What this contract still holds is that a PLANT-LEVEL profile is not treated
# more leniently than a device-level one: whatever refuses one refuses the other
# for the same structural reason. Adding the SmartLogger profile must not have
# been the thing that opened the gate.
capable = sorted(pid for pid, f in compiled.items() if f["production"])
require(any("smartlogger" not in pid for pid in capable) or not capable,
        "the plant-level profile is the only one that can command, which would "
        "mean it was granted something the device-level profiles were not")

# ---------------------------------------------------------------------------
# 3. The register values, against their citations
# ---------------------------------------------------------------------------

start = PROFILES.find(f'.id = "{PROFILE_ID}"')
require(start != -1, f"{PROFILE_ID} block not found in the catalogue source")
block = PROFILES[start:PROFILES.find("    },", start)] if start != -1 else ""

EXPECTED = [
    # (source text, what it is and where it comes from)
    (".power_limit_address = 40428",
     "plant active-power percentage command, SL-MB PDF p.13 (5) SN18"),
    (".power_limit_function = 6",
     "FC06 write single, documented for 40428"),
    (".raw_units_per_percent = 10.0f",
     "40428 gain 10, i.e. percent x 10"),
    (".power_limit_type = INVERTER_VALUE_U16",
     "40428 is one U16 register"),
    (".power_limit_readback_address = 40802",
     "active scheduling percentage, SL-MB PDF p.18 (10) SN59"),
    (".power_limit_readback_type = INVERTER_VALUE_U32",
     "40802 is U32 over two registers, unlike the U16 command register"),
    (".power_limit_readback_words = 2", "40802 spans two registers"),
    (".power_limit_readback_scale = 1.0f",
     "40802 gain is 1 -- percent x 1, NOT x 10 like 40428"),
    (".active_power_address = 40525",
     "measured plant active power, SL-MB PDF p.13 (5) SN23"),
    (".active_power_type = INVERTER_VALUE_S32", "40525 is I32"),
    (".active_power_scale = 0.001f", "40525 gain 1000, so raw watts"),
    (".min_command_interval_ms = 1000",
     'the documented "not less than 1 seconds" adjustment interval'),
    (".command_authority_address = 40737",
     "active power control mode, the contention detector"),
]
for text, why in EXPECTED:
    require(text in block, f"{PROFILE_ID}: missing {text} ({why})")

# The gain trap must be called out where somebody editing the profile will read
# it. 40428 is percent x 10 and 40802 is percent x 1; one scale factor for both
# understates or overstates the plant limit by a factor of ten.
require("40802" in block and "gain 1" in block,
        "the profile must state that 40802 is gain 1, not 10 like 40428 -- an "
        "easy and dangerous mix-up")
require(re.search(r"40428", block) is not None,
        "the profile must cite the command register it writes")

# 42017 must appear nowhere in this profile. On the logger (unit 0) it is
# "SystemTime: year"; on an inverter it is the active-power gradient. A profile
# that writes it with the unit id left at the logger sets the plant clock.
require("42017" not in block,
        f"{PROFILE_ID} references 42017, which is 'SystemTime: year' on the "
        "logger and 'active power gradient' on an inverter -- the single most "
        "dangerous address collision in the SmartLogger analysis")

# Write-only action registers must not appear anywhere in the profile: the manual
# says of 40723 that "the data domain is not checked", 40724 renumbers devices and
# 40725 deletes inverters.
for forbidden in ("40200", "40201", "40202", "40203", "40204", "40723", "40724",
                  "40725", "42730", "42779"):
    require(forbidden not in re.sub(r"/\*.*?\*/", "", block, flags=re.S),
            f"{PROFILE_ID} configures {forbidden}, a write-only plant action "
            "register that must never be part of a control path")

# No operating-state register, for the same reason as every other profile: the
# logger's plant state words are province-specific and 40699 has inverted
# polarity, so a guessed state is worse than the unknown one.
require("status_register" not in block,
        f"{PROFILE_ID} configures an operating-status register; no manufacturer "
        "status address is hardcoded in this firmware")
require("PRODUCTION_APPROVED" not in block,
        f"{PROFILE_ID} must not claim production approval")

# ---------------------------------------------------------------------------
# 4. The model and the runtime wiring
# ---------------------------------------------------------------------------

for token, why in [
    ("inverter_measured_confirm_mode_t", "the measured-confirmation mode type"),
    ("INVERTER_MEASURED_CONFIRM_REQUIRED",
     "the mode where a setpoint echo can never confirm"),
    ("INVERTER_MEASURED_CONFIRM_CORROBORATING",
     "measured power in ADDITION to a readback, not only instead of it"),
    ("INVERTER_WRITE_PROOF_MEASURED_POWER", "what a demonstrated limit rests on"),
    ("INVERTER_WRITE_PROOF_AMBIGUOUS_HEADROOM",
     "the honest reason for refusing to confirm below a limit"),
    ("limit_demonstrated", "the flag that separates proof from consistency"),
    ("baseline_kw", "the pre-command measurement, without which nothing is provable"),
    ("baseline_before_write",
     "a baseline sampled after the command has already been affected by it"),
    ("measured_tolerance_kw", "a tolerance in kW"),
    ("measured_tolerance_percent_of_capacity", "a tolerance in percent of capacity"),
]:
    require(token in CONFIRM_H, f"the confirmation model must express {why} ({token})")

require("PURE." in CONFIRM_H and "esp_" not in CONFIRM_C,
        "the confirmation evaluator must stay pure and host-testable")

# The baseline has to be captured at write time. There is no other moment: after
# the command the pre-command output is gone.
require("baseline_power_kw" in TYPES and "baseline_sample_ms" in TYPES,
        "the runtime must keep the pre-command measurement and its own timestamp")
require("runtime->data.baseline_power_kw = runtime->data.measured_power_kw" in MANAGER,
        "the pre-command measured output must be captured where the write is "
        "recorded; there is no later moment at which it can be recovered")
require("evidence.measured_mode = profile ? profile->measured_power_confirm" in MANAGER,
        "the profile's declared measured mode must reach the evaluator")
require("INVERTER_MEASURED_CONFIRM_NONE" in MANAGER,
        "the measured mode must fall back to NONE only for a NULL profile, never "
        "as a repair for an incompletely described one")
require("poll_command_authority" in MANAGER,
        "the scheduling-authority register must actually be read")
require("inverter_profile_command_authority_described" in MANAGER and
        "inverter_profile_command_authority_described" in PROFILES_H,
        "the authority read must be gated on a read-only function code")
require("authority_lost_count" in TYPES,
        "losing scheduling authority to another master must be counted, because "
        "it is the failure the register is read for")

# The measured quantity is the profile's own active-power telemetry, so declaring
# a measured mode without an active-power register is meaningless.
require("profile->has_active_power" in PROFILES_H,
        "a measured-power confirmation must require an active-power register")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("logger plant control contract passed "
      f"({len(compiled)} profiles walked, {len(cases)} confirmation cases executed, "
      "0 production-capable profiles, falling irradiance does not confirm a limit)")
