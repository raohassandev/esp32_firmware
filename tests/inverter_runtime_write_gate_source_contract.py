#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

REQUIRED = [
    '#include "inverter_profiles.h"',
    'inverter_profile_allows_write',
    'write_allowed',
    'no online production-approved inverter profile is commandable',
    'runtime->data.online',
    'runtime->data.telemetry_valid',
    '!runtime->data.telemetry_stale',
    'identity_is_current(runtime, timestamp)',
    'recompute_commandable_capacity',
    'command_target_t targets[APP_MAX_INVERTERS]',
    'const inverter_profile_t *profile = targets[i].profile',
    'profile->power_limit_address',
    'profile->raw_units_per_percent',
    'commanded_total_kw + commanded_kw > target_kw + 0.01f',
    'runtime->data.commanded_power_kw = commanded_kw;',
    'invalidate_identity(runtime)',
]

missing = [token for token in REQUIRED if token not in SOURCE]
if missing:
    raise SystemExit(f"runtime inverter write-gate contract missing: {missing}")

if '!profile->simulator_only' not in PROFILES:
    raise SystemExit("simulator-only profiles are not explicitly excluded from production writes")
if 'INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED' not in PROFILES:
    raise SystemExit("production approval gate is missing")

FORBIDDEN_COMMAND_TOKENS = [
    'runtime->config.power_limit_address',
    'runtime->config.power_limit_function',
    'runtime->config.raw_units_per_percent',
]

found = [token for token in FORBIDDEN_COMMAND_TOKENS if token in SOURCE]
if found:
    raise SystemExit(f"legacy raw-register command bypass remains: {found}")

with tempfile.TemporaryDirectory() as directory:
    binary = Path(directory) / "inverter_command_policy_test"
    subprocess.run([
        "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "components/inverter_manager/include"),
        str(ROOT / "tests/inverter_command_policy_test.c"),
        str(ROOT / "components/inverter_manager/inverter_command_policy.c"),
        "-lm", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

print("inverter runtime write-gate and command confirmation tests passed")
