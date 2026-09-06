"""Source contract for tools/bench_board_simulator_comm_test.py.

The runner is allowed to change physical bench meter configuration only when
explicitly invoked with --apply. It must use normal Engineering auth, keep
credentials out of argv/source, write the complete three-meter array, verify
that the configuration mutation forces automatic control disabled, and only
count real post-restart meter successes after strict Modbus TID/unit matching.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = (ROOT / "tools/bench_board_simulator_comm_test.py").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


# Normal auth only: no bypass enabling and no credential CLI argument.
require('/api/engineering/login' in TOOL, "runner must use the normal Engineering login API")
require('PVDG_ENGINEERING_PASSWORD' in TOOL, "Engineering password must come from an environment variable")
require('--password"' not in TOOL and "--password'" not in TOOL, "password must never be accepted as a CLI argument")
for forbidden in (
    'PVDG_BENCH_ENGINEERING_AUTH_BYPASS=y',
    'WAVESHARE_BENCH_NETWORK_AUTH_BYPASS=y',
    'CONFIG_PVDG_BENCH_ENGINEERING_AUTH_BYPASS',
):
    require(forbidden not in TOOL, f"runner must not enable a bench auth bypass: {forbidden}")

# Dry-run is the default; mutations require explicit --apply.
require('parser.add_argument("--apply", action="store_true"' in TOOL, "writes must require explicit --apply")
require('if not args.apply:' in TOOL, "default execution must stop after read-only preflight")

# Exact intended full-array simulator mapping.
for token in (
    'MeterTarget("Grid Meter", 31, 1, None, 58, 0, 0.00001)',
    'MeterTarget("GEN-1", 32, 2, 0, 58, 0, 0.00001)',
    'MeterTarget("GEN-2", 41, 2, 1, 0x0012, 1, 0.0001)',
    '"meters": meters',
    '/api/meters/config',
):
    require(token in TOOL, f"expected full-array meter mapping contract missing: {token}")

# Safety interlock and restart must be asserted, not assumed.
for token in (
    'saved.get("meter_count") != 3',
    'saved.get("control_enabled") is not False',
    'saved.get("restart_required") is not True',
    '/api/system/restart',
):
    require(token in TOOL, f"post-save safety/restart assertion missing: {token}")

# Strict MBAP ownership checks before direct simulator values are trusted.
for token in (
    'response_tid != tid',
    'response_unit != unit_id',
    'protocol_id != 0',
    'function != 3',
    'byte_count != expected_bytes',
):
    require(token in TOOL, f"strict Modbus response ownership check missing: {token}")

# Real board runtime evidence must be captured from the operator meter endpoint.
for token in (
    '/api/meters',
    '"success_count"',
    '"error_count"',
    '"active_power_kw"',
    '"success_delta"',
    '"error_delta"',
    '"production_physical_acceptance": False',
):
    require(token in TOOL, f"runtime evidence/reporting contract missing: {token}")

print("Bench board-simulator communication runner source contract passed")
