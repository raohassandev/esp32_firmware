#!/usr/bin/env python3
"""Brand register-map evidence contract (Solis, Growatt, Sungrow, Chint/CPS).

These four profiles are manual TRANSCRIPTIONS. Nothing about them has been
exercised against physical equipment, so what this contract protects is not
correctness of the numbers -- no test can establish that -- but the two
properties that make a transcription safe to carry in a shipped catalogue:

  1. Every transcribed map is ATTRIBUTABLE. A reviewer can find the document,
     page and row behind every value, in the profile comment and in
     docs/BRAND_REGISTER_EVIDENCE.md, and check it rather than trust it.
  2. Every transcribed map is CONFIRMABLE and UNPROMOTED. A command register
     always has a readback register, no profile claims production approval, and
     no profile smuggles in an operating-status address.

The catalogue-wide execution of the write gate lives in
tests/inverter_write_permission_test.c; this file is the source-evidence half.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

failures = []

def require(condition, message):
    if not condition:
        failures.append(message)

def block_for(profile_id):
    marker = f'.id = "{profile_id}"'
    if marker not in PROFILES:
        return None
    start = PROFILES.index(marker)
    end = PROFILES.find("\n    },", start)
    return PROFILES[start:end if end != -1 else len(PROFILES)]

# ---------------------------------------------------------------------------
# Per-profile: attributable, confirmable, unpromoted.
#
# The addresses are asserted here as exact PDU values so that a later edit
# cannot silently shift one. Each is stated with the manual tag it came from;
# where a manual uses 1-based tags the -1 conversion is part of the assertion.
# ---------------------------------------------------------------------------
BRANDS = {
    # profile id: (manual citation fragments, command PDU, readback PDU,
    #              active-power PDU, raw units per percent, readback scale)
    "solis.commercial.pending": (
        ["RS485_MODBUS", "5.6", "3052", "3051", "10000", "3070"],
        3051, 3051, 3004, "100.0f", "0.01f",
    ),
    "growatt.tl3x.documented": (
        ["TH-276-00", "Active P Rate", "0-100 or 255", "35"],
        3, 3, 35, "1.0f", "1.0f",
    ),
    "growatt.tlx.documented": (
        ["TH-276-00", "3023"],
        3, 3, 3023, "1.0f", "1.0f",
    ),
    "sungrow.string.documented": (
        ["V1.1.36", "5008", "5007", "little-endian", "Appendix 6"],
        5007, 5007, 5031, "10.0f", "0.1f",
    ),
    "chint.cps.sch100_125ktl.documented": (
        ["403X", "0x4035", "0x1001", "0x001D", "0x2708"],
        0x1001, 0x1001, 0x001D, "10.0f", "0.1f",
    ),
}

for profile_id, (citations, command, readback, active, raw_per_percent, rb_scale) in BRANDS.items():
    block = block_for(profile_id)
    require(block is not None, f"{profile_id} is not in the catalogue")
    if block is None:
        continue

    # --- unpromoted ---------------------------------------------------------
    require("INVERTER_PROFILE_QUALIFICATION_DOCUMENTED" in block,
            f"{profile_id} must stay DOCUMENTED: nothing here has been qualified on hardware")
    require("PRODUCTION_APPROVED" not in block,
            f"{profile_id} must not claim production approval")
    require("status_register" not in block and "status_address" not in block,
            f"{profile_id} must not configure an operating-status register: no manufacturer "
            "status code table is available and a guessed mapping is worse than UNKNOWN")
    require(".simulator_only = true" not in block,
            f"{profile_id} is a manufacturer map, not a simulator contract")

    # --- confirmable --------------------------------------------------------
    require(".has_power_limit = true" in block and ".has_power_limit_readback = true" in block,
            f"{profile_id} must pair its command register with a readback register; "
            "a command that cannot be confirmed must not be issuable")
    require(f".power_limit_address = {command}" in block or
            f".power_limit_address = 0x{command:04X}" in block,
            f"{profile_id} command register moved away from PDU {command}")
    require(f".power_limit_readback_address = {readback}" in block or
            f".power_limit_readback_address = 0x{readback:04X}" in block,
            f"{profile_id} readback register moved away from PDU {readback}")
    require(f".active_power_address = {active}" in block or
            f".active_power_address = 0x{active:04X}" in block,
            f"{profile_id} active-power register moved away from PDU {active}")
    require(f".raw_units_per_percent = {raw_per_percent}" in block,
            f"{profile_id} command scale moved away from {raw_per_percent}")
    require(f".power_limit_readback_scale = {rb_scale}" in block,
            f"{profile_id} readback scale moved away from {rb_scale}")

    # The write function code must be one the transport actually implements,
    # and the readback must be a read function code.
    write_fc = re.search(r"\.power_limit_function = (\d+)", block)
    require(write_fc is not None and write_fc.group(1) in ("6", "16"),
            f"{profile_id} uses a write function code the Modbus layer does not implement")
    read_fc = re.search(r"\.power_limit_readback_function = (\d+)", block)
    require(read_fc is not None and read_fc.group(1) in ("3", "4"),
            f"{profile_id} readback must use function code 3 or 4")

    # A percentage command must be bounded, and must not exceed 100% without
    # per-model evidence that the machine accepts overload scheduling.
    require(".minimum_percent = 0.0f" in block and ".maximum_percent = 100.0f" in block,
            f"{profile_id} must bound its command to 0-100%")

    # --- attributable -------------------------------------------------------
    require(".manual_reference" in block, f"{profile_id} carries no manual reference")
    if ".manual_reference" in block:
        reference_head = block.split(".manual_reference", 1)[1].split(",", 1)[0]
        # An EMPTY reference used to satisfy the check above, because the field
        # name was present. That passed a mutation test on 2026-08-11: the
        # sungrow reference was blanked and both contracts still reported success.
        # It mattered little while the evidence document existed to cross-check
        # against; the document has since been deleted, so this string is now the
        # only record of where the registers came from and it must actually say
        # something.
        quoted = reference_head.split('"')[1] if '"' in reference_head else ""
        require(len(quoted.strip()) >= 12,
                f"{profile_id} has an empty or unusably short manual reference "
                f"({quoted!r}); it is the only remaining record of the source")
        require("pending" not in reference_head,
                f"{profile_id} carries register addresses but no concrete manual reference")
    require("not qualified on hardware" in block,
            f"{profile_id} must say plainly in its manual reference that it is unqualified")
    for citation in citations:
        require(citation in block,
                f"{profile_id} does not cite {citation!r} in its transcription comment")

# ---------------------------------------------------------------------------
# Growatt's 1% command quantisation needs a tolerance that will not fault a
# perfectly accepted fractional setpoint. Half a step is 0.5%.
# ---------------------------------------------------------------------------
for profile_id in ("growatt.tl3x.documented", "growatt.tlx.documented"):
    block = block_for(profile_id)
    if block is None:
        continue
    tolerance = re.search(r"\.readback_tolerance_percent = ([0-9.]+)f", block)
    require(tolerance is not None and float(tolerance.group(1)) > 0.5,
            f"{profile_id} writes whole percent, so a tolerance <= 0.5% would fault "
            "an accepted setpoint that was merely rounded")

# ---------------------------------------------------------------------------
# Sungrow's word order was pinned to BA here, from the manual's worked example
# (0x01020304 transmits as 03,04,01,02), specifically so that a lab SIMULATOR
# emitting the opposite order could not "correct" it. That guard was right about
# simulators and wrong about this hardware.
#
# A real SG-series inverter, delivering 78-82 kW by its owner's account, reads:
#
#     PDU 5030 = 0     PDU 5031 = 1     PDU 5032 = 13930
#
# so the value starts at 5031 -- the manual's tag 5031 with NO offset, unlike
# Solis -- and the high word arrives first. AB gives 79.47 kW and tracks the
# plant. BA gives 912,916 kW. The same holds for the two neighbouring pairs at
# 5009 and 5017: every one of them is about a million kilowatts read as BA, and
# physically ordered as DC > apparent > active read as AB.
#
# Read at the old 5030/BA the pair was a constant [0, 1] that never moved, and
# 0.001 W/unit turned it into a steady 65.5 kW of solar on the operator's main
# screen while the machine delivered 80. A wrong reading that looked plausible.
#
# The clause is kept, inverted, and now cites the instrument rather than the
# document: a simulator still must not drive this value, and neither must a
# manual that the hardware contradicts.
# ---------------------------------------------------------------------------
sungrow = block_for("sungrow.string.documented")
if sungrow is not None:
    require(".active_power_word_order = INVERTER_WORD_ORDER_AB" in sungrow,
            "Sungrow's active power arrives high word first on real SG hardware: "
            "PDU 5031-5032 read [1, 13930] against a measured 78-82 kW, which is "
            "79.47 kW as AB and 912916 kW as BA. Do not restore BA from the manual's "
            "worked example without a machine that agrees with it")
    require(".active_power_address = 5031" in sungrow,
            "Sungrow's input registers take no -1 offset: PDU 5030 reads 0 and the "
            "value starts at 5031. Reading 5030 gave a constant [0, 1] that showed as "
            "a steady 65.5 kW while the inverter was delivering 80 kW")

# ---------------------------------------------------------------------------
# Growatt's documented minimum command period is 850 ms with a 1 s suggestion,
# so the poll period may not drop below it.
# ---------------------------------------------------------------------------
for profile_id in ("growatt.tl3x.documented", "growatt.tlx.documented"):
    block = block_for(profile_id)
    if block is None:
        continue
    poll = re.search(r"\.telemetry_poll_ms = (\d+)", block)
    require(poll is not None and int(poll.group(1)) >= 1000,
            f"{profile_id} must not poll faster than the manual's documented minimum "
            "command period (850 ms, suggestion 1 s)")

# ---------------------------------------------------------------------------
# No settle time is documented for any of these brands, so none may be asserted.
# A fabricated settle window either masks a real fault or invents one.
# ---------------------------------------------------------------------------
for profile_id in BRANDS:
    block = block_for(profile_id)
    if block is None:
        continue
    require(".power_limit_settle_ms =" not in block,
            f"{profile_id} asserts a settle time, but no manual documents one; it must be "
            "measured at commissioning, not invented")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(f"{len(failures)} brand register evidence contract failure(s)")

print("brand register-map evidence contract passed "
      "(Solis, Growatt TL3-X, Growatt TL-X, Sungrow, Chint/CPS)")
