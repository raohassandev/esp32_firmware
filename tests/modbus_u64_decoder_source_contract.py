from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "components/modbus_tcp/include/modbus_decoder.h").read_text(encoding="utf-8")
SOURCE = (ROOT / "components/modbus_tcp/modbus_decoder.c").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("modbus_decode_u64_be_scaled" in HEADER,
        "64-bit Modbus decoder is not exposed")
require("double *out_value" in HEADER,
        "energy decoder must preserve precision with double output")
require("register_count < 4U" in SOURCE,
        "energy decoder must require four registers")
require("(uint64_t)registers[0] << 48" in SOURCE,
        "highest Modbus word is not placed at bits 63..48")
require("(uint64_t)registers[1] << 32" in SOURCE,
        "second Modbus word is not placed at bits 47..32")
require("(uint64_t)registers[2] << 16" in SOURCE,
        "third Modbus word is not placed at bits 31..16")
require("(uint64_t)registers[3]" in SOURCE,
        "lowest Modbus word is missing")
require("(double)raw * scale + offset" in SOURCE,
        "scaled energy output is incorrect")
require("float *out_value" not in HEADER.split("modbus_decode_u64_be_scaled", 1)[1],
        "64-bit energy must not be returned through float")

# Independent known-vector check for the documented high-word-first layout.
registers = [0x0001, 0x0002, 0x0003, 0x0004]
raw = ((registers[0] << 48) |
       (registers[1] << 32) |
       (registers[2] << 16) |
       registers[3])
require(raw == 0x0001000200030004, "known U64 register vector is wrong")
require(raw * 0.01 == 2814835668.67204,
        "known scaled U64 energy vector is wrong")

print("64-bit Modbus energy decoder source contract passed")
