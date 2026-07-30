from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "components/inverter_manager/include/inverter_profile_decode.h").read_text(encoding="utf-8")
source = (root / "components/inverter_manager/inverter_profile_decode.c").read_text(encoding="utf-8")
cmake = (root / "components/inverter_manager/CMakeLists.txt").read_text(encoding="utf-8")

required = [
    "INVERTER_VALUE_U16",
    "INVERTER_VALUE_S16",
    "INVERTER_VALUE_U32",
    "INVERTER_VALUE_S32",
    # IEEE-754 float32, needed because some manufacturers document their dispatch
    # register as a float. Writing a percentage into one as an integer produces
    # ~7e-44 -- effectively zero output -- and the readback decodes the same bytes
    # the same wrong way and reports the command confirmed. See
    # tests/inverter_float_register_test.c, which executes the arithmetic, and
    # tests/inverter_float_register_source_contract.py for the structural half.
    "INVERTER_VALUE_FLOAT32",
    "inverter_profile_encode_value",
    "INVERTER_WORD_ORDER_AB",
    "INVERTER_WORD_ORDER_BA",
    "inverter_profile_decode_value",
    "inverter_profile_readback_matches",
]
for token in required:
    assert token in header or token in source, f"missing decoder token: {token}"

assert "(int16_t)registers[0]" in source
assert "((uint32_t)high << 16) | low" in source
assert "fabsf(requested_percent - readback_percent)" in source
assert '"inverter_profile_decode.c"' in cmake

print("inverter profile decoder source contract: PASS")
