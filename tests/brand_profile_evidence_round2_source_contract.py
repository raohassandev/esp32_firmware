#!/usr/bin/env python3
"""Brand register-map evidence contract, round 2 (FoxESS, GoodWe, Knox/AISWEI).

Companion to tests/brand_profile_evidence_source_contract.py, which guards the
first round of transcriptions (Solis, Growatt, Sungrow, Chint/CPS). These three
profiles are manual TRANSCRIPTIONS too. Nothing about them has been exercised
against physical equipment, so what this contract protects is not correctness of
the numbers -- no source test can establish that -- but the properties that make a
transcription safe to carry in a shipped catalogue:

  1. ATTRIBUTABLE. Every value can be traced to a document, page and row, from the
     profile comment and from docs/BRAND_REGISTER_EVIDENCE_ROUND2.md, so a
     reviewer can check it rather than trust it.
  2. CONFIRMABLE and UNPROMOTED. A command register always has a readback
     register, no profile claims production approval, and no profile smuggles in
     an operating-status address.
  3. The REFUSALS stay refused, and stay explained. Five brands were examined and
     rejected; the reason each was rejected is a safety fact, and a later edit
     must not quietly populate one of them.

The catalogue-wide execution of the write gate lives in
tests/inverter_write_permission_test.c; this file is the source-evidence half.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
DECODE_H = (ROOT / "components/inverter_manager/include/inverter_profile_decode.h").read_text(encoding="utf-8")
EVIDENCE_PATH = ROOT / "docs/BRAND_REGISTER_EVIDENCE_ROUND2.md"

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
# The evidence document must exist and must identify every manual it transcribes.
# ---------------------------------------------------------------------------
require(EVIDENCE_PATH.is_file(), "docs/BRAND_REGISTER_EVIDENCE_ROUND2.md is missing")
EVIDENCE = EVIDENCE_PATH.read_text(encoding="utf-8") if EVIDENCE_PATH.is_file() else ""

for token in (
    "FoxESS-Modbus-Protocol-V1.05.03.00.pdf",
    "GoodWe_grid-tied_GT-series_Modubus_Protocol(8).pdf",
    "MB001_ASW GEN-Modbus-en_V2.1.5(2).pdf",
):
    require(token in EVIDENCE, f"the evidence document does not identify the source file {token}")

# The addressing convention is the trap this project has hit before. The document
# must state it per brand, and must quote the evidence rather than assert it.
for token in (
    "F7 03 7D 55 00 01 98 E0",        # GoodWe worked frame
    "0x7D55 == 32085",                 # ...and the arithmetic that proves it
    "remove 3x or 4x and subtract 1",  # Knox/AISWEI rule
    "0x03e8",                          # ...and its worked conversion
    "the first PDU address is 0",       # FoxESS -- the only argument available
    "DEDUCTION ONLY",                   # ...explicitly graded as weaker
):
    require(token in EVIDENCE,
            f"the evidence document does not quote the addressing evidence: {token!r}")

# It must record what the manuals do NOT say, rather than quietly filling it in.
for token in ("not documented", "left unset", "Settle time", "Comms-loss fail-safe",
              "require the physical machine"):
    require(token in EVIDENCE, f"the evidence document does not record the gaps: {token!r}")

# The two hazards that are invisible in a register number must be stated loudly.
require("Storage, does not support high-frequency write operations" in EVIDENCE,
        "the evidence document must quote GoodWe's flash-wear prohibition verbatim: a "
        "controller writing that register every control cycle destroys the inverter")
require("accept the write and echo the value back" in EVIDENCE,
        "the evidence document must explain the accept-and-echo failure that makes a "
        "prerequisite enable a refusal rather than an inconvenience")

# The brand-conflation correction must stay explained, or someone will merge them
# back together.
for token in ("two different manufacturers", "49007", "45403"):
    require(token in EVIDENCE,
            f"the evidence document must record why FoxESS and Knox are separate: {token!r}")


# ---------------------------------------------------------------------------
# Per-profile: attributable, confirmable, unpromoted.
#
# Addresses are asserted as exact PDU values so a later edit cannot silently
# shift one. Where a manual uses a non-PDU printed address the conversion is part
# of the assertion.
# ---------------------------------------------------------------------------
BRANDS = {
    # profile id: (citation fragments, command PDU, readback PDU,
    #              raw units per percent, readback scale, write FC)
    "foxess.commercial.pending": (
        ["V1.05.03.00", "2025-01-15", "49007", "Gain 10", "0.1%", "0x80"],
        49007, 49007, "10.0f", "0.1f", 6,
    ),
    "goodwe.commercial.pending": (
        ["GT series", "2023-08-25", "42407", "0x7D55", "32085", "42433",
         "Storage, does not support high-frequency write operations"],
        42407, 42407, "10.0f", "0.1f", 6,
    ),
    "knox.aiswei.asw.documented": (
        ["MB001_ASW GEN-Modbus-en_V2.1.5", "45403", "44001", "0x03e8", "31390"],
        5402, 5402, "100.0f", "0.01f", 6,
    ),
}

for profile_id, (citations, command, readback, raw_per_percent, rb_scale, write_fc) in BRANDS.items():
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
            "status code table has been verified and a guessed mapping is worse than UNKNOWN")
    require(".simulator_only = true" not in block,
            f"{profile_id} is a manufacturer map, not a simulator contract")

    # --- confirmable --------------------------------------------------------
    require(".has_power_limit = true" in block and ".has_power_limit_readback = true" in block,
            f"{profile_id} must pair its command register with a readback register; "
            "a command that cannot be confirmed must not be issuable")
    require(f".power_limit_address = {command}" in block,
            f"{profile_id} command register moved away from PDU {command}")
    require(f".power_limit_readback_address = {readback}" in block,
            f"{profile_id} readback register moved away from PDU {readback}")
    require(f".raw_units_per_percent = {raw_per_percent}" in block,
            f"{profile_id} command scale moved away from {raw_per_percent}")
    require(f".power_limit_readback_scale = {rb_scale}" in block,
            f"{profile_id} readback scale moved away from {rb_scale}")
    require(f".power_limit_function = {write_fc}" in block,
            f"{profile_id} write function code moved away from {write_fc}")

    read_fc = re.search(r"\.power_limit_readback_function = (\d+)", block)
    require(read_fc is not None and read_fc.group(1) in ("3", "4"),
            f"{profile_id} readback must use function code 3 or 4")

    # A percentage command must be bounded, and must not exceed 100% without
    # per-model evidence that the machine accepts overload scheduling. GoodWe's
    # manual permits 110%; that is not sufficient evidence.
    require(".minimum_percent = 0.0f" in block and ".maximum_percent = 100.0f" in block,
            f"{profile_id} must bound its command to 0-100%")

    # No settle time is documented for any of these brands, so none may be
    # asserted. A fabricated settle window either masks a real fault or invents one.
    require(".power_limit_settle_ms =" not in block,
            f"{profile_id} asserts a settle time, but no manual documents one; it must be "
            "measured at commissioning, not invented")

    # --- attributable -------------------------------------------------------
    require(".manual_reference" in block, f"{profile_id} carries no manual reference")
    if ".manual_reference" in block:
        reference_head = block.split(".manual_reference", 1)[1].split(",", 1)[0]
        require("pending" not in reference_head,
                f"{profile_id} carries register addresses but no concrete manual reference")
    require("not qualified on hardware" in block,
            f"{profile_id} must say plainly in its manual reference that it is unqualified")
    for citation in citations:
        require(citation in block,
                f"{profile_id} does not cite {citation!r} in its transcription comment")

    require(profile_id in EVIDENCE,
            f"{profile_id} is not covered by docs/BRAND_REGISTER_EVIDENCE_ROUND2.md")


# ---------------------------------------------------------------------------
# Knox/AISWEI documents a prerequisite enable, so it must be refused write
# authority. This is the highest-value assertion in the file.
# ---------------------------------------------------------------------------
knox = block_for("knox.aiswei.asw.documented")
if knox is not None:
    require(".requires_prerequisite_enable = true" in knox,
            "AISWEI printed register 44001 'Active power control function: 0 = Disable, "
            "1 = Enable' governs whether the setpoint at printed 45403 has any effect. "
            "45403 is an ordinary RW holding register and echoes the value back either "
            "way, so commanding it without 44001 enabled reports CONFIRMED for a limit "
            "the inverter is ignoring. That flag is the refusal and must not be removed.")
    require("44001" in knox,
            "the Knox profile must cite the prerequisite enable register it is refused for")

# FoxESS and GoodWe document no prerequisite enable. If a future edit sets the
# flag it must be because a register was found -- so the reasoning for NOT setting
# it is pinned in the evidence document, not here, and the profiles must stay
# consistent with the release table's derivation.
for profile_id in ("foxess.commercial.pending", "goodwe.commercial.pending"):
    block = block_for(profile_id)
    if block is None:
        continue
    require("PREREQUISITE ENABLE" in block.upper(),
            f"{profile_id} must state explicitly whether a prerequisite enable register "
            "was found, so silence is never mistaken for absence of evidence")


# ---------------------------------------------------------------------------
# The word order that no manual states must stay unstated.
#
# FoxESS 39134 and AISWEI printed 31371 both give a 32-bit active-power register
# whose address, type and scale are documented but whose WORD ORDER is not. A
# reversed word order turns 100 kW into a nonsense number, so active power is
# left unconfigured rather than guessed. Both single-register command paths are
# unaffected.
# ---------------------------------------------------------------------------
for profile_id in ("foxess.commercial.pending", "knox.aiswei.asw.documented"):
    block = block_for(profile_id)
    if block is None:
        continue
    require(".has_active_power = true" not in block,
            f"{profile_id} must not configure active power: neither manual states the word "
            "order of a 32-bit value, and this is exactly the field the project forbids "
            "interpolating. One read against a known output closes it.")

# GoodWe, by contrast, DOES document its word order, so it must be set and must
# not be 'corrected' later without new evidence.
goodwe = block_for("goodwe.commercial.pending")
if goodwe is not None:
    require(".has_active_power = true" in goodwe,
            "GoodWe documents address, type, scale AND word order for active power, so it "
            "must be configured")
    require(".active_power_word_order = INVERTER_WORD_ORDER_AB" in goodwe,
            "GoodWe p.4 states 'Long Integer Data ... from the most significant bit to the "
            "least significant bit' -- most significant word first, i.e. AB")
    require(".active_power_address = 32080" in goodwe,
            "GoodWe active-power register moved away from PDU 32080")
    require(".active_power_type = INVERTER_VALUE_S32" in goodwe,
            "GoodWe documents active power as S32")


# ---------------------------------------------------------------------------
# The REFUSALS. Five brands were examined and rejected, each for a reason that is
# a safety fact. A later edit must not quietly populate one, and the reason must
# stay recorded.
# ---------------------------------------------------------------------------
REFUSALS = {
    "solaredge": [
        "Float32",                       # firmware has no such type
        "little-endian word order",       # ...nor a command-side word order field
        "stops production and restarts",  # ...nor prerequisite sequencing
    ],
    "sma": [
        "no RO mirror",         # readback is not evidenced
        "Grid Guard",           # an undetermined unlock that expires on restart
    ],
    "solax": [
        "read-only",            # the only percentage register cannot be written
        "irreversible hardware damage",
    ],
    "saj": [
        "write-only",           # a command that can never be confirmed
        "801FH",
    ],
    "fronius": [
        "PLATINUM",             # the file is not a Fronius document at all
        "image-only",
    ],
}

for brand, tokens in REFUSALS.items():
    for token in tokens:
        require(token in EVIDENCE,
                f"the evidence document no longer records why {brand} was refused: {token!r}")

# No profile may exist for a refused brand. The check is on the profile id list so
# that adding one is a deliberate act that fails this test first.
profile_ids = set(re.findall(r'\.id = "([^"]+)"', PROFILES))
for brand in REFUSALS:
    offenders = sorted(pid for pid in profile_ids if pid.startswith(f"{brand}."))
    require(not offenders,
            f"a profile exists for {brand} ({offenders}), which round 2 refused. If a new "
            f"manual has closed the gap, update docs/BRAND_REGISTER_EVIDENCE_ROUND2.md and "
            f"this contract deliberately -- do not add the profile silently.")

# SolarEdge's refusal is a FIRMWARE limitation, so it must stay true that the
# firmware cannot represent a float. If a float type is ever added, SolarEdge
# becomes reachable and this contract should be revisited on purpose.
require("INVERTER_VALUE_FLOAT" not in DECODE_H,
        "a float value type now exists in inverter_profile_decode.h. SolarEdge was refused "
        "partly because it could not be represented; revisit docs/"
        "BRAND_REGISTER_EVIDENCE_ROUND2.md section 6.1 rather than leaving it refused for a "
        "reason that has stopped being true.")


if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    raise SystemExit(f"{len(failures)} round-2 brand register evidence contract failure(s)")

print("brand register-map evidence contract round 2 passed "
      "(FoxESS, GoodWe, Knox/AISWEI populated and attributable; "
      "SolarEdge, SMA, Solax, SAJ, Fronius refusals still recorded and still refused)")
