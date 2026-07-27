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
