#!/usr/bin/env python3
"""Float32 register support: the structural properties a unit test cannot see.

tests/inverter_float_register_test.c EXECUTES the encoder and the decoder and is
the primary evidence that the arithmetic and the byte layout are right. It cannot
see two things, and both are safety properties:

  1. That the COMMAND PATH actually consults the profile. A perfect encoder that
     nobody calls with the profile's type and word order changes nothing. The bug
     being closed was precisely an unconditional `words[0] = raw >> 16` in
     inverter_manager.c -- the read path modelled word order and the write path
     did not, and that asymmetry silently swaps the halves of a float on any device
     documenting the other order.

  2. That non-finite handling stays a REFUSAL rather than a returned value, in
     every place a register becomes a number. A NaN percentage compares unequal to
     everything including itself. If one were returned instead of refused it would
     reach the readback comparison, fail it, and latch a confirmation fault on a
     perfectly healthy machine -- while an infinity encoded into a dispatch
     register would be handed to a 100 kW inverter before anything noticed.

The project's convention, which this file pins so it cannot drift: a non-finite
reading is "keep waiting", not "fault". modbus_decoder.c returns
ESP_ERR_INVALID_RESPONSE for it, inverter_write_confirmation.c refuses to count a
non-finite readback as a usable sample and therefore holds at PENDING until its
deadline, and the inverter decoder must do the same.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DECODE_H = (ROOT / "components/inverter_manager/include/inverter_profile_decode.h").read_text(encoding="utf-8")
DECODE_C = (ROOT / "components/inverter_manager/inverter_profile_decode.c").read_text(encoding="utf-8")
PROFILES_H = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
CONFIRM = (ROOT / "components/inverter_manager/inverter_write_confirmation.c").read_text(encoding="utf-8")
MODBUS = (ROOT / "components/modbus_tcp/modbus_decoder.c").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github/workflows/esp-idf-build.yml").read_text(encoding="utf-8")

failures = []


def require(condition, message):
    if not condition:
        failures.append(message)


# ---------------------------------------------------------------------------
# 1. The type and the word order exist, and the enum stays append-only.
# ---------------------------------------------------------------------------
require("INVERTER_VALUE_FLOAT32" in DECODE_H,
        "no IEEE-754 value type. Without it a percentage destined for a Float32 "
        "dispatch register is encoded as a plain integer: 50 lands as ~7e-44, i.e. "
        "effectively zero output, and the readback decodes the same bytes the same "
        "wrong way and reports the command CONFIRMED.")

# U16 must stay 0 so a zeroed description is the narrowest register, and FLOAT32
# must not be inserted ahead of an existing value: the status register description
# and every profile share this enum.
require("INVERTER_VALUE_U16 = 0" in DECODE_H,
        "INVERTER_VALUE_U16 must stay the zero value so a zeroed description means "
        "the narrowest, most conservative register rather than a 32-bit one")
order = re.search(r"typedef enum \{(.*?)\} inverter_value_type_t;", DECODE_H, re.S)
require(order is not None, "the value-type enum is no longer parseable")
if order:
    names = [n.strip().split("=")[0].strip() for n in order.group(1).split(",") if n.strip()]
    require(names[:4] == ["INVERTER_VALUE_U16", "INVERTER_VALUE_S16",
                          "INVERTER_VALUE_U32", "INVERTER_VALUE_S32"],
            f"the value-type enum was REORDERED ({names}). It must only be appended to: "
            "inverter_status_register_t and every profile share it, and a renumbering "
            "silently redefines what an already-written register description means.")
    require("INVERTER_VALUE_FLOAT32" in names,
            "INVERTER_VALUE_FLOAT32 must be a member of the value-type enum")

require("INVERTER_WORD_ORDER_AB = 0" in DECODE_H,
        "AB must stay the zero value: it is what the command encoder hardcoded before "
        "the profile field existed, so zero-means-AB is what keeps every previously "
        "written profile encoding identical bytes")


# ---------------------------------------------------------------------------
# 2. The COMMAND path consults the profile. This is the asymmetry that was the bug.
# ---------------------------------------------------------------------------
require("power_limit_type" in PROFILES_H and "power_limit_word_order" in PROFILES_H,
        "inverter_profile_t has no command-side type or word order. Word order was "
        "modelled on the readback side only; the encoder assumed AB unconditionally, so "
        "a manufacturer documenting the other order got the two halves of its value "
        "swapped -- and for a float that is a different order of magnitude.")

require("inverter_profile_encode_value" in DECODE_H and
        "inverter_profile_encode_value" in DECODE_C,
        "the encoder must be a shared, host-testable pure function, not private "
        "arithmetic inside the Modbus command path")

# Anchored on the DEFINITION, not the forward declaration: `[^;]*?` cannot cross
# the declaration's semicolon, so this cannot silently match the wrong function.
encode = re.search(r"static esp_err_t encode_command\([^;]*?\)\s*\n\{.*?\n\}", MANAGER, re.S)
require(encode is not None, "encode_command() is no longer parseable in inverter_manager.c")
if encode:
    body = encode.group(0)
    require("inverter_profile_encode_value" in body,
            "encode_command() does not call the shared encoder, so the command path and "
            "the readback path no longer share one word-order concept -- which is exactly "
            "the asymmetry that made a wrong value read back as the value requested")
    require("profile->power_limit_word_order" in body,
            "encode_command() ignores the profile's command word order")
    require("profile->power_limit_type" in body,
            "encode_command() ignores the profile's command type, so a Float32 register "
            "would be written as an integer")
    require("words[0] = (uint16_t)(raw >> 16)" not in body,
            "encode_command() still hardcodes big-endian word order")
    # A float needs two registers written together; function 0x06 cannot do that.
    require("INVERTER_VALUE_FLOAT32" in body and "!= 16U" in body,
            "encode_command() must refuse a float command that is not two registers with "
            "function 16. SolarEdge p.14: 'The two registers must be written together "
            "using Modbus function 16.'")

# The encoder and the decoder must be inverses through ONE shared word split, so
# they cannot drift apart. A mismatch would make a wrong value on the wire decode
# back as the value that was asked for: a self-confirming error.
require("split_words" in DECODE_C and "combine_words" in DECODE_C,
        "the word split and the word combine must both live in the decoder module, "
        "adjacent, so the write path and the read path cannot disagree about word order")
require("memcpy" in DECODE_C,
        "the float bit pattern must be moved with memcpy: type-punning through a float* "
        "is undefined behaviour and the optimiser is entitled to assume it never happens")


# ---------------------------------------------------------------------------
# 3. Non-finite is REFUSED, everywhere, with the project's error code.
# ---------------------------------------------------------------------------
require("if (!isfinite(decoded)) return ESP_ERR_INVALID_RESPONSE;" in DECODE_C,
        "a non-finite decoded value must be refused, not returned. NaN is a legitimate "
        "'not available' marker in a float register and it compares unequal to "
        "everything including itself; returning it would poison the readback comparison "
        "and could latch a confirmation fault on a healthy machine.")
require("isfinite(scaled)" in DECODE_C,
        "the SCALED result must be re-checked: a finite raw value times a finite scale "
        "can still overflow to infinity")
require("!isfinite(narrowed)" in DECODE_C,
        "the double->float narrowing in the encoder must be re-checked. A finite double "
        "can become an infinity as a float32, and encoding 0x7F800000 into a dispatch "
        "register hands an infinity to the inverter before anything can refuse it.")

# The convention this follows, pinned in the two places that already implement it
# so that changing one of them makes this test the place the disagreement surfaces.
require("ESP_ERR_INVALID_RESPONSE" in MODBUS,
        "modbus_decoder.c is the precedent for refusing a non-finite decode with "
        "ESP_ERR_INVALID_RESPONSE; the inverter decoder matches it")
require("finite_percent(evidence->readback_percent)" in CONFIRM,
        "inverter_write_confirmation.c must keep requiring a FINITE readback before a "
        "sample counts as usable. That is what makes a non-finite reading 'keep waiting' "
        "rather than 'fault': with no usable sample the verdict holds at PENDING until "
        "the deadline instead of declaring a MISMATCH.")
require("INVERTER_WRITE_PENDING" in CONFIRM and "INVERTER_WRITE_MISMATCHED" in CONFIRM,
        "the pending/mismatch distinction is what 'keep waiting' means; both verdicts "
        "must still exist")

# The acquisition path must not undo the refusal by accepting a decode error.
for guard in ("&readback_percent", "&power_kw"):
    require(guard in MANAGER,
            f"the acquisition path no longer decodes into {guard}; the non-finite refusal "
            "only protects anything if the caller checks the returned error")
require(MANAGER.count("if (err == ESP_OK && !isfinite(") >= 2,
        "both the active-power and the readback poll must keep their belt-and-braces "
        "non-finite check after decoding, so a decoder that ever regressed could not "
        "put a NaN into the control loop")


# ---------------------------------------------------------------------------
# 4. The new executable test is wired into CI, with real line continuations.
#
# A step written earlier in this project shipped with literal backslash-n instead
# of line continuations and was silently broken, so the shape of the step is
# checked rather than assumed.
# ---------------------------------------------------------------------------
require("tests/inverter_float_register_test.c" in WORKFLOW,
        "the float encode/decode unit test is not wired into CI. A host test that does "
        "not run is documentation.")
require("\\n" not in WORKFLOW.replace("\\\\n", ""),
        "the workflow contains a literal backslash-n. That exact mistake shipped a "
        "silently broken CI step in this project before: the shell receives one long "
        "line, the compile never happens, and the step passes.")

step = re.search(r"- name: [^\n]*float register[^\n]*\n\s+run: \|\n((?:\s+.*\n)+?)(?=      - name:|\Z)",
                 WORKFLOW, re.I)
require(step is not None,
        "the float register CI step is not present in the expected 'run: |' block form")
if step:
    body = step.group(1)
    require("gcc -std=c11 -Wall -Wextra -Werror" in body,
            "the float register test must be compiled with warnings as errors, like every "
            "other host test in this workflow")
    require("components/inverter_manager/inverter_profile_decode.c" in body,
            "the step must compile the real decoder, not a copy")
    require(body.rstrip().splitlines()[-1].strip().startswith("/tmp/"),
            "the step must EXECUTE the binary it built; compiling is not testing")


if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    raise SystemExit(f"{len(failures)} float register support contract failure(s)")

print("float register support contract passed (IEEE-754 type present, command path "
      "consults the profile's type and word order, encode/decode share one word split, "
      "non-finite input refused as 'keep waiting' on both paths, test wired into CI)")
