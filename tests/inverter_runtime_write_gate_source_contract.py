#!/usr/bin/env python3
"""Runtime inverter write gate.

Pins down which inverters may be written to at all, and - since P0-9 - that the
write path itself issues the write and nothing more. Confirmation lives in the
background acquisition path and is covered by
tests/inverter_write_readback_source_contract.py.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "components/inverter_manager/inverter_manager.c").read_text(encoding="utf-8")
PROFILES = (ROOT / "components/inverter_manager/inverter_profiles.c").read_text(encoding="utf-8")

REQUIRED = [
    '#include "inverter_profiles.h"',
    '#include "inverter_write_confirmation.h"',
    'inverter_profile_allows_write',
    'write_allowed',
    'no online production-approved inverter profile is commandable',
    'runtime->data.online',
    'runtime->data.telemetry_valid',
    '!runtime->data.telemetry_stale',
    '!runtime->data.confirmation_fault',
    'identity_is_current(runtime, timestamp)',
    'recompute_commandable_capacity',
    'command_target_t targets[APP_MAX_INVERTERS]',
    'Build and validate the complete immutable fleet plan',
    'planned_total_kw + commanded_kw > target_kw + 0.01f',
    'write_profile_command',
    'INVERTER_COMMAND_MAX_ATTEMPTS 2U',
    'rollback_targets(targets, (uint8_t)(i + 1U))',
    'issue_safe_zero',
    'invalidate_identity(target->runtime)',
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
    'runtime->data.commanded_power_kw = commanded_kw;',
]

found = [token for token in FORBIDDEN_COMMAND_TOKENS if token in SOURCE]
if found:
    raise SystemExit(f"legacy or unconfirmed command-state path remains: {found}")

# The confirmation_fault latch must gate BOTH the advertised commandable
# capacity and the per-call target selection. Gating only one of them would let
# the control engine keep commanding an inverter it can no longer verify.
if SOURCE.count('!runtime->data.confirmation_fault') < 2:
    raise SystemExit(
        "confirmation_fault must exclude an inverter from both the commandable "
        "capacity and the command target selection"
    )

# The write path runs on the control task. It must not sleep and must not read.
COMMAND_PATH = SOURCE[SOURCE.index('esp_err_t inverter_manager_set_total_power_kw'):
                      SOURCE.index('inverter_write_state_t inverter_manager_fleet_write_confirmation')]
for forbidden in ('vTaskDelay', 'read_profile_block', 'read_limit_percent'):
    if forbidden in COMMAND_PATH:
        raise SystemExit(
            f"the inverter command path must not '{forbidden}': it runs on the "
            "control period and Modbus speed is the highest priority requirement"
        )

# The safe-zero path is allowed exactly one write per inverter and no sleep.
ROLLBACK = SOURCE[SOURCE.index('static bool rollback_targets'):
                  SOURCE.index('esp_err_t inverter_manager_set_total_power_kw')]
if 'vTaskDelay' in ROLLBACK:
    raise SystemExit("the safe-zero rollback must not sleep on the control period")

print("runtime inverter write-gate contract passed")
