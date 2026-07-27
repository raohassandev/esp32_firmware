#!/usr/bin/env python3
from pathlib import Path

SOURCE = Path("components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
PROFILES = Path("components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

REQUIRED = [
    '#include "inverter_profiles.h"',
    'inverter_profile_allows_write',
    'write_allowed',
    'no online production-approved inverter profile is commandable',
    'runtime->data.online',
    'runtime->data.telemetry_valid',
    '!runtime->data.telemetry_stale',
    'runtime->data.identity_verified',
    'recompute_commandable_capacity',
    'runtime->profile->power_limit_address',
    'runtime->profile->raw_units_per_percent',
]

missing = [token for token in REQUIRED if token not in SOURCE]
if missing:
    raise SystemExit(f"runtime inverter write-gate contract missing: {missing}")

if '!profile->simulator_only' not in PROFILES:
    raise SystemExit("simulator-only profiles are not explicitly excluded from production writes")
if 'INVERTER_PROFILE_QUALIFICATION_PRODUCTION_APPROVED' not in PROFILES:
    raise SystemExit("production approval gate is missing")

# The legacy raw configuration fields must not be used by the command write path.
FORBIDDEN_COMMAND_TOKENS = [
    'runtime->config.power_limit_address',
    'runtime->config.power_limit_function',
    'runtime->config.raw_units_per_percent',
]

found = [token for token in FORBIDDEN_COMMAND_TOKENS if token in SOURCE]
if found:
    raise SystemExit(f"legacy raw-register command bypass remains: {found}")

print("inverter runtime write-gate source contract passed")
