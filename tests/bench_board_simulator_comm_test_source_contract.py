"""Source contract for tools/bench_board_simulator_comm_test.py.

The runner is allowed to change physical bench meter configuration only when
explicitly invoked with --apply. It must use normal Engineering auth, keep
credentials out of argv/source, write the complete three-meter array, verify
that the configuration mutation forces automatic control disabled, and only
count real post-restart meter successes after strict Modbus TID/unit matching.
The operator meter API must also expose the runtime diagnostics required to
record the board<->simulator acceptance evidence without inventing serial data.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = (ROOT / "tools/bench_board_simulator_comm_test.py").read_text(encoding="utf-8")
DEVICE_API = (ROOT / "components/web_server/device_api.c").read_text(encoding="utf-8")
METER_TYPES = (ROOT / "components/meter_manager/include/meter_types.h").read_text(encoding="utf-8")


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

# Dry-run is the default; mutations require explicit --apply. Scenario evidence
# can be collected read-only after the full mapping has already been applied.
require('mode.add_argument("--apply", action="store_true"' in TOOL, "writes must require explicit --apply")
require('"--measure-only"' in TOOL, "runner needs a no-write scenario measurement mode")
require('if not args.apply and not args.measure_only:' in TOOL, "default execution must stop after read-only preflight")
require('default=181' in TOOL, "default timed observation must run for minutes, not a few seconds")

# Exact intended full-array simulator mapping.
for token in (
    'MeterTarget("Grid Meter", 31, 1, None, 58, 0, 0.00001)',
    'MeterTarget("GEN-1", 32, 2, 0, 58, 0, 0.00001)',
    'MeterTarget("GEN-2", 41, 2, 1, 0x0012, 1, 0.0001)',
    '"meters": meters',
    '/api/meters/config',
):
    require(token in TOOL, f"expected full-array meter mapping contract missing: {token}")

# Safety interlock and restart must be asserted, not assumed. Both apply and
# measure-only modes must refuse to run evidence collection with control on.
for token in (
    'saved.get("meter_count") != 3',
    'saved.get("control_enabled") is not False',
    'saved.get("restart_required") is not True',
    '/api/system/restart',
    'require_control_disabled(http)',
    'automatic control must stay disabled during simulator testing',
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

# The meter manager already tracks these values. The HTTP evidence surface must
# publish them directly rather than asking the bench operator to infer them.
for token in (
    'bool degraded;',
    'uint32_t last_response_time_ms;',
    'uint32_t current_poll_delay_ms;',
    'uint8_t recent_success_percent;',
    'uint32_t response_errors;',
):
    require(token in METER_TYPES, f"meter runtime diagnostic disappeared: {token}")

for token in (
    '"degraded", runtime_available && data.degraded',
    '"age_ms", health.has_data',
    '"last_response_time_ms", data.last_response_time_ms',
    '"current_poll_delay_ms", data.current_poll_delay_ms',
    '"recent_success_percent", data.recent_success_percent',
    '"response_errors", data.response_errors',
):
    require(token in DEVICE_API, f"/api/meters no longer exposes required evidence field: {token}")

# Real board runtime evidence must be captured and compared to a strict direct
# simulator probe. The result explicitly stays below full physical acceptance.
for token in (
    '/api/meters',
    '"success_count"',
    '"response_errors"',
    '"consecutive_failures"',
    '"recent_success_percent"',
    '"current_poll_delay_ms"',
    '"last_response_time_ms"',
    '"age_ms"',
    '"active_power_kw"',
    '"sustained_success_percent"',
    '"observation_duration_s"',
    '"first_success_observed_s_from_sampling_start"',
    '"simulator_expected_kw"',
    '"value_matches_simulator"',
    '"production_physical_acceptance": False',
):
    require(token in TOOL, f"runtime evidence/reporting contract missing: {token}")

print("Bench board-simulator communication runner source contract passed")
