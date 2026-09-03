#!/usr/bin/env python3
"""Lock generic inverter command width, scale, range and finite-value safety.

This does not approve any manufacturer profile. It only proves that when a
future profile is manually/physically qualified, the shared command engine
rejects malformed command metadata and handles declared 16/32-bit writes with
bounded Modbus function-code semantics.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
PROFILES_H = (ROOT / "components/inverter_manager/include/inverter_profiles.h").read_text(encoding="utf-8")
PROFILES_C = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")
RUNTIME_GATE = (ROOT / "tests/inverter_runtime_write_gate_source_contract.py").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Profile schema explicitly carries command width, scaling and legal range.
for token in (
    "uint8_t power_limit_words;",
    "float raw_units_per_percent;",
    "float minimum_percent;",
    "float maximum_percent;",
    "uint8_t power_limit_function;",
    "uint16_t power_limit_address;",
):
    require(token in PROFILES_H, f"command schema field missing: {token}")

# A production write requires a qualified profile and command readback; generic
# command support must never make pending/simulator profiles production-writable.
require("!profile->simulator_only" in PROFILES_C,
        "simulator profile could enter the production write path")
require("INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED" in PROFILES_C,
        "production approval gate is missing")
require("profile->has_power_limit_readback" in PROFILES_C,
        "production write admission does not require readback")

# The encoder rejects NaN/Inf, invalid scale/range, unsupported word widths and
# values that overflow the declared 16/32-bit command representation.
start = MANAGER.index("static esp_err_t encode_command")
end = MANAGER.index("static esp_err_t write_profile_command", start)
encode = MANAGER[start:end]
for token in (
    "!isfinite(percent)",
    "!isfinite(profile->raw_units_per_percent)",
    "profile->raw_units_per_percent <= 0.0f",
    "!isfinite(profile->minimum_percent)",
    "!isfinite(profile->maximum_percent)",
    "profile->maximum_percent < profile->minimum_percent",
    "profile->power_limit_words != 1U && profile->power_limit_words != 2U",
    "UINT16_MAX",
    "UINT32_MAX",
    "!isfinite(raw_value)",
    "llround(raw_value)",
):
    require(token in encode, f"command encoder safety missing: {token}")
require("words[0] = (uint16_t)(raw >> 16)" in encode and
        "words[1] = (uint16_t)raw" in encode,
        "32-bit command must emit deterministic high-word then low-word payload")
require("*word_count = 2U" in encode and "*word_count = 1U" in encode,
        "encoder must report the exact generated Modbus word count")

# The writer accepts FC16 for bounded multi-register commands and FC06 only for
# one word. A 32-bit value must never be silently truncated into FC06.
write_start = MANAGER.index("static esp_err_t write_profile_command")
write_end = MANAGER.index("static esp_err_t read_limit_percent", write_start)
write = MANAGER[write_start:write_end]
require("profile->power_limit_function == 16U" in write and
        "modbus_tcp_write_multiple" in write,
        "FC16 multiple-register command path is missing")
require("profile->power_limit_function == 6U && word_count == 1U" in write and
        "modbus_tcp_write_single" in write,
        "FC06 must remain restricted to one register")
require("ESP_ERR_NOT_SUPPORTED" in write,
        "unsupported function/width combinations must fail closed")

# The immutable fleet plan must encode every target before any field write, and
# the established transactional gate must keep readback/retry/rollback active.
require("Build and validate the complete immutable fleet plan" in MANAGER,
        "fleet commands are not fully planned before writes begin")
require("encode_command(runtime->profile, percent" in MANAGER,
        "fleet planner does not use the guarded command encoder")
for token in (
    "inverter_profile_readback_matches",
    "INVERTER_COMMAND_MAX_ATTEMPTS",
    "rollback_targets",
):
    require(token in RUNTIME_GATE, f"transactional write contract lost {token}")

print("Inverter command schema width/scaling safety contract passed")
