#!/usr/bin/env python3
"""Validate exact-image Industrial UI Waveshare physical acceptance evidence.

This tool cannot observe the LCD, touchscreen, source contacts, Wi-Fi network,
or Modbus traffic by itself. It combines the established serial/resource soak
gate with explicit human/bench observations for the Industrial UI workflow.
CI validates evidence structure and fail-closed rules; it cannot create a
physical PASS.

The v2 acceptance surface intentionally covers the complete hardware workflow
carried by the current Waveshare integration candidate: native Network
commissioning, complete Grid/Generator/Transfer/Sync source mapping, alarm
filter/sort/acknowledgement behavior, and real board-to-bench-simulator Modbus
activity with monotonically increasing counters and decoded samples.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from waveshare_final_acceptance import evaluate as evaluate_waveshare

REQUIRED_VISUAL_FLAGS = (
    "overview_layout_ok", "grid_layout_ok", "solar_layout_ok",
    "alarms_layout_ok", "readiness_layout_ok", "engineering_hierarchy_ok",
    "commissioning_layout_ok", "service_layout_ok",
    "dark_theme_readable", "light_theme_readable",
)
REQUIRED_TOUCH_FLAGS = (
    "menu_touch_ok", "route_navigation_touch_ok", "alarm_control_touch_ok",
    "operator_protected_routes_blocked", "engineering_routes_reachable",
    "summary_shortcuts_correct", "alarm_interaction_ok",
)
REQUIRED_RUNTIME_FLAGS = (
    "unrelated_api_responsive", "history_events_responsive", "browser_lockout_absent",
)
REQUIRED_NETWORK_FLAGS = (
    "network_page_layout_ok",
    "network_scan_ok",
    "network_select_ok",
    "network_manual_ssid_ok",
    "network_password_entry_ok",
    "network_connect_ok",
    "network_restart_ok",
    "wifi_signal_indicator_ok",
    "network_engineering_lock_ok",
)
REQUIRED_SOURCE_COMMISSIONING_FLAGS = (
    "source_grid_mapping_roundtrip_ok",
    "source_gen1_mapping_roundtrip_ok",
    "source_gen2_mapping_roundtrip_ok",
    "source_gen3_mapping_roundtrip_ok",
    "source_transfer_mapping_roundtrip_ok",
    "source_sync_mapping_roundtrip_ok",
    "source_save_forces_auto_disabled",
    "source_status_not_inferred_from_kw_sign",
)
REQUIRED_ALARM_FLAGS = (
    "alarm_filter_all_ok",
    "alarm_filter_active_ok",
    "alarm_filter_unack_ok",
    "alarm_sort_priority_ok",
    "alarm_sort_state_ok",
    "alarm_sort_id_ok",
    "alarm_engineering_ack_ok",
    "alarm_operator_ack_refused",
)
REQUIRED_MODBUS_FLAGS = (
    "board_modbus_simulator_connected",
    "modbus_decoded_values_follow_simulator_changes",
)
REQUIRED_ROUTES = {
    "dashboard", "meters", "inverters", "alarms", "readiness",
    "engineering", "commissioning", "network", "system",
}


@dataclass
class ModbusEvidence:
    passed: bool
    request_count_before: int
    request_count_after: int
    success_count_before: int
    success_count_after: int
    decoded_sample_count: int
    flags: dict[str, bool]
    failures: list[str]


@dataclass
class IndustrialUiResult:
    passed: bool
    candidate_sha: str
    tree_sha: str
    artifact_digest: str
    application_sha256: str
    visual: dict[str, bool]
    touch: dict[str, bool]
    runtime: dict[str, bool]
    network: dict[str, bool]
    source_commissioning: dict[str, bool]
    alarms: dict[str, bool]
    modbus: dict
    base_waveshare: dict
    failures: list[str]


def _identity(observations: dict, key: str, expected: str, failures: list[str]) -> str:
    value = str(observations.get(key, "")).strip().lower()
    wanted = str(expected).strip().lower()
    if not value:
        failures.append(f"missing_identity:{key}")
    elif value != wanted:
        failures.append(f"identity_mismatch:{key}")
    return value


def _flags(observations: dict, keys: tuple[str, ...], prefix: str, failures: list[str]) -> dict[str, bool]:
    result: dict[str, bool] = {}
    for key in keys:
        passed = observations.get(key) is True
        result[key] = passed
        if not passed:
            failures.append(f"{prefix}_not_passed:{key}")
    return result


def _nonnegative_int(observations: dict, key: str, failures: list[str]) -> int:
    value = observations.get(key)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        failures.append(f"modbus_invalid_counter:{key}")
        return -1
    return value


def _evaluate_modbus(observations: dict) -> ModbusEvidence:
    failures: list[str] = []
    flags = _flags(observations, REQUIRED_MODBUS_FLAGS, "modbus", failures)
    req_before = _nonnegative_int(observations, "modbus_request_count_before", failures)
    req_after = _nonnegative_int(observations, "modbus_request_count_after", failures)
    ok_before = _nonnegative_int(observations, "modbus_success_count_before", failures)
    ok_after = _nonnegative_int(observations, "modbus_success_count_after", failures)
    decoded_samples = _nonnegative_int(observations, "modbus_decoded_sample_count", failures)

    if req_before >= 0 and req_after >= 0 and req_after <= req_before:
        failures.append("modbus_request_count_did_not_increase")
    if ok_before >= 0 and ok_after >= 0 and ok_after <= ok_before:
        failures.append("modbus_success_count_did_not_increase")
    if req_after >= 0 and ok_after >= 0 and ok_after > req_after:
        failures.append("modbus_success_count_exceeds_request_count")
    if decoded_samples >= 0 and decoded_samples < 3:
        failures.append("modbus_decoded_sample_count_below_3")

    return ModbusEvidence(
        passed=not failures,
        request_count_before=req_before,
        request_count_after=req_after,
        success_count_before=ok_before,
        success_count_after=ok_after,
        decoded_sample_count=decoded_samples,
        flags=flags,
        failures=failures,
    )


def evaluate(
    serial_text: str,
    observations: dict,
    *,
    expected_candidate_sha: str,
    expected_tree_sha: str,
    expected_artifact_digest: str,
    expected_application_sha256: str,
    min_page_cycles: int = 20,
    min_runtime_seconds: int = 14_400,
    min_soak_samples: int = 240,
    min_dma_free: int = 20_000,
) -> IndustrialUiResult:
    failures: list[str] = []
    candidate_sha = _identity(observations, "candidate_sha", expected_candidate_sha, failures)
    tree_sha = _identity(observations, "tree_sha", expected_tree_sha, failures)
    artifact_digest = _identity(observations, "artifact_digest", expected_artifact_digest, failures)
    application_sha256 = _identity(observations, "application_sha256", expected_application_sha256, failures)

    visual = _flags(observations, REQUIRED_VISUAL_FLAGS, "visual", failures)
    touch = _flags(observations, REQUIRED_TOUCH_FLAGS, "touch", failures)
    runtime = _flags(observations, REQUIRED_RUNTIME_FLAGS, "runtime", failures)
    network = _flags(observations, REQUIRED_NETWORK_FLAGS, "network", failures)
    source_commissioning = _flags(
        observations, REQUIRED_SOURCE_COMMISSIONING_FLAGS, "source_commissioning", failures
    )
    alarms = _flags(observations, REQUIRED_ALARM_FLAGS, "alarm", failures)

    modbus = _evaluate_modbus(observations)
    failures.extend(f"modbus:{item}" for item in modbus.failures)

    # The product UI gate extends, never replaces, the existing display/touch
    # gate. The base evaluator therefore still requires alarms/touch/no-sweep/
    # no-reload/no-corruption plus the exact candidate/digest and serial soak.
    base = evaluate_waveshare(
        serial_text,
        observations,
        expected_candidate_sha=expected_candidate_sha,
        expected_artifact_digest=expected_artifact_digest,
        min_page_cycles=min_page_cycles,
        min_runtime_seconds=min_runtime_seconds,
        min_soak_samples=min_soak_samples,
        min_dma_free=min_dma_free,
    )
    if not base.passed:
        failures.extend(f"waveshare:{item}" for item in base.failures)

    roles = observations.get("roles_exercised")
    role_set = {str(item).strip().lower() for item in roles} if isinstance(roles, list) else set()
    if role_set != {"operator", "engineering"}:
        failures.append("roles_exercised_must_equal_operator_and_engineering")

    routes = observations.get("routes_exercised")
    route_set = {str(item).strip().lower() for item in routes} if isinstance(routes, list) else set()
    missing = sorted(REQUIRED_ROUTES - route_set)
    if missing:
        failures.append("routes_missing:" + ",".join(missing))

    # A physical evidence file must record a finite RSSI observed after the
    # Network workflow reached connected state. This is evidence of a real
    # Wi-Fi result, not a magic threshold/quality assertion.
    wifi_rssi = observations.get("wifi_connected_rssi_dbm")
    if isinstance(wifi_rssi, bool) or not isinstance(wifi_rssi, (int, float)) or not math.isfinite(float(wifi_rssi)):
        failures.append("network_invalid:wifi_connected_rssi_dbm")

    return IndustrialUiResult(
        passed=not failures,
        candidate_sha=candidate_sha,
        tree_sha=tree_sha,
        artifact_digest=artifact_digest,
        application_sha256=application_sha256,
        visual=visual,
        touch=touch,
        runtime=runtime,
        network=network,
        source_commissioning=source_commissioning,
        alarms=alarms,
        modbus=asdict(modbus),
        base_waveshare=asdict(base),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate complete Industrial UI exact-image Waveshare physical evidence")
    parser.add_argument("serial_log", type=Path)
    parser.add_argument("observations_json", type=Path)
    parser.add_argument("--expected-candidate-sha", required=True)
    parser.add_argument("--expected-tree-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--expected-application-sha256", required=True)
    parser.add_argument("--min-page-cycles", type=int, default=20)
    parser.add_argument("--min-runtime-seconds", type=int, default=14_400)
    parser.add_argument("--min-soak-samples", type=int, default=240)
    parser.add_argument("--min-dma-free", type=int, default=20_000)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        observations = json.loads(args.observations_json.read_text(encoding="utf-8"))
        serial_text = args.serial_log.read_text(errors="replace")
    except (OSError, json.JSONDecodeError) as exc:
        print(f"INDUSTRIAL UI ACCEPTANCE FAIL: evidence unreadable: {exc}", file=sys.stderr)
        return 2
    if not isinstance(observations, dict):
        print("INDUSTRIAL UI ACCEPTANCE FAIL: observations JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        serial_text, observations,
        expected_candidate_sha=args.expected_candidate_sha,
        expected_tree_sha=args.expected_tree_sha,
        expected_artifact_digest=args.expected_artifact_digest,
        expected_application_sha256=args.expected_application_sha256,
        min_page_cycles=args.min_page_cycles,
        min_runtime_seconds=args.min_runtime_seconds,
        min_soak_samples=args.min_soak_samples,
        min_dma_free=args.min_dma_free,
    )
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("INDUSTRIAL UI PHYSICAL ACCEPTANCE PASS" if result.passed else "INDUSTRIAL UI PHYSICAL ACCEPTANCE FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        serial = result.base_waveshare.get("serial", {})
        print(f"- candidate_sha={result.candidate_sha}")
        print(f"- tree_sha={result.tree_sha}")
        print(f"- page_cycles={result.base_waveshare.get('page_cycles')}")
        print(f"- observed_runtime_seconds={serial.get('observed_runtime_seconds')}")
        print(f"- soak_samples={serial.get('soak_samples')}")
        print(f"- minimum_checked_dma_free={serial.get('minimum_checked_dma_free')}")
        print(
            "- modbus_requests="
            f"{result.modbus.get('request_count_before')}->{result.modbus.get('request_count_after')}"
        )
        print(
            "- modbus_successes="
            f"{result.modbus.get('success_count_before')}->{result.modbus.get('success_count_after')}"
        )
        print(f"- modbus_decoded_samples={result.modbus.get('decoded_sample_count')}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
