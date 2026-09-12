#!/usr/bin/env python3
"""Validate Rev-A H4 fabricated-prototype physical acceptance evidence.

This tool never creates physical evidence and never turns CAD/CI results into a
hardware PASS. It validates a record produced by an authorized physical
executor against the exact H2/provider/firmware identity supplied on the CLI.
Missing, ambiguous, contradictory, skipped-mandatory, cross-artifact, or unsafe
evidence fails closed.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


H4_TEST_IDS = (
    "power_12v",
    "power_24v",
    "input_current_thermal_observation",
    "reverse_polarity_protection",
    "usb_only_programming_boot",
    "usb_field_supply_backfeed",
    "rails_5v_3v3_under_load",
    "power_cycle_safe_relay_defaults",
    "native_usb_recovery_boot",
    "enclosure_service_access",
    "ethernet_modbus_tcp_sustained",
    "ethernet_reconnect_recovery",
    "rs485_a_tx_rx",
    "rs485_b_tx_rx",
    "dual_rs485_concurrent",
    "termination_bias_topology",
    "hmi_serial_recovery",
    "relay_safe_default_off",
    "relay_individual_no_nc_com",
    "relay_simultaneous_activity",
    "optional_features",
    "enclosure_fit_cable_clearance",
    "antenna_region_clearance",
    "thermal_soak",
    "thermal_mechanical_stability",
    "industrial_environmental_emc",
)

OPTIONAL_FEATURE_TEST = "optional_features"
EXTERNAL_LAB_TEST = "industrial_environmental_emc"
MANDATORY_TESTS = set(H4_TEST_IDS) - {OPTIONAL_FEATURE_TEST, EXTERNAL_LAB_TEST}
VALID_STATUS = {"PASS", "SKIPPED_NOT_POPULATED", "DEFERRED_EXTERNAL_LAB"}
FATAL_COUNTERS = ("wdt", "panic", "no_mem", "unexpected_reset", "resource_collapse")
HEX40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")


@dataclass
class H4Result:
    passed: bool
    h2_checkpoint_sha: str
    provider_artifact_digest: str
    firmware_sha: str
    firmware_artifact_digest: str
    board_serial: str
    tests_seen: list[str]
    failures: list[str]


def _text(value: object, minimum: int = 1) -> bool:
    return len(str(value or "").strip()) >= minimum


def _finite(value: object) -> bool:
    if isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def _timestamp(value: object) -> datetime | None:
    text = str(value or "").strip()
    if not text:
        return None
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        return None
    return parsed


def _refs(value: object, minimum: int = 1) -> bool:
    return (
        isinstance(value, list)
        and len(value) >= minimum
        and all(_text(item, 4) for item in value)
    )


def _dict(value: object) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _check_identity(
    record: dict[str, Any],
    expected_h2_sha: str,
    expected_provider_digest: str,
    expected_firmware_sha: str,
    expected_firmware_artifact_digest: str,
    failures: list[str],
) -> tuple[str, str, str, str, str]:
    h2 = str(record.get("h2_checkpoint_sha", "")).strip().lower()
    provider = str(record.get("provider_artifact_digest", "")).strip().lower()
    firmware = _dict(record.get("firmware"))
    fw_sha = str(firmware.get("sha", "")).strip().lower()
    fw_digest = str(firmware.get("artifact_digest", "")).strip().lower()
    expected_fw_digest = str(expected_firmware_artifact_digest or "").strip().lower()
    board = _dict(record.get("board_identity"))
    board_serial = str(board.get("board_serial", "")).strip()

    if not HEX40.fullmatch(h2):
        failures.append("h2_checkpoint_sha_invalid")
    if h2 != expected_h2_sha.strip().lower():
        failures.append("h2_checkpoint_sha_mismatch")
    if not SHA256.fullmatch(provider):
        failures.append("provider_artifact_digest_invalid")
    if provider != expected_provider_digest.strip().lower():
        failures.append("provider_artifact_digest_mismatch")
    if not HEX40.fullmatch(fw_sha):
        failures.append("firmware_sha_invalid")
    if fw_sha != expected_firmware_sha.strip().lower():
        failures.append("firmware_sha_mismatch")

    if not _text(record.get("provider_artifact_ref"), 4):
        failures.append("provider_artifact_ref_missing")
    if not _text(firmware.get("artifact_ref"), 4):
        failures.append("firmware_artifact_ref_missing")
    if not SHA256.fullmatch(fw_digest):
        failures.append("firmware_artifact_digest_invalid")
    if not SHA256.fullmatch(expected_fw_digest):
        failures.append("expected_firmware_artifact_digest_invalid")
    elif fw_digest != expected_fw_digest:
        failures.append("firmware_artifact_digest_mismatch")

    for key in ("pcb_lot", "pcba_lot", "board_serial", "bom_variant", "assembly_variant"):
        if not _text(board.get(key), 2):
            failures.append(f"board_identity_missing:{key}")
    substitutions = board.get("approved_substitutions")
    if not isinstance(substitutions, list):
        failures.append("approved_substitutions_not_list")

    return h2, provider, fw_sha, fw_digest, board_serial


def _check_fabricator(record: dict[str, Any], failures: list[str]) -> datetime | None:
    fab = _dict(record.get("fabricator"))
    if not _text(fab.get("name"), 2):
        failures.append("fabricator_name_missing")
    if fab.get("dfm_accepted") is not True:
        failures.append("fabricator_dfm_not_accepted")
    if not _text(fab.get("dfm_acceptance_ref"), 4):
        failures.append("fabricator_dfm_acceptance_ref_missing")
    if not SHA256.fullmatch(str(fab.get("dfm_acceptance_digest", "")).strip().lower()):
        failures.append("fabricator_dfm_acceptance_digest_invalid")
    accepted_at = _timestamp(fab.get("dfm_accepted_at"))
    if accepted_at is None:
        failures.append("fabricator_dfm_accepted_at_invalid")
    minima = _dict(fab.get("accepted_minima_mm"))
    expected = {"drill": 0.20, "hole_clearance": 0.18, "copper_edge": 0.25}
    for key, target in expected.items():
        if not _finite(minima.get(key)):
            failures.append(f"fabricator_minimum_missing:{key}")
        elif float(minima[key]) > target + 1e-9:
            failures.append(f"fabricator_minimum_does_not_accept_h2:{key}")
    return accepted_at


def _check_bench(
    record: dict[str, Any], failures: list[str]
) -> tuple[datetime | None, datetime | None]:
    bench = _dict(record.get("bench"))
    for key in ("executor", "supply_model", "enclosure_revision"):
        if not _text(bench.get(key), 2):
            failures.append(f"bench_missing:{key}")
    started = _timestamp(bench.get("started_at"))
    ended = _timestamp(bench.get("ended_at"))
    if started is None:
        failures.append("bench_started_at_invalid")
    if ended is None:
        failures.append("bench_ended_at_invalid")
    if started is not None and ended is not None and ended <= started:
        failures.append("bench_time_not_increasing")
    if not _finite(bench.get("current_limit_a")) or float(bench.get("current_limit_a", 0)) <= 0:
        failures.append("bench_current_limit_invalid")
    if not _refs(bench.get("instrument_refs"), 1):
        failures.append("bench_instrument_refs_missing")
    return started, ended


def _check_runtime(record: dict[str, Any], failures: list[str]) -> None:
    runtime = _dict(record.get("runtime"))
    counts = _dict(runtime.get("fatal_counts"))
    for key in FATAL_COUNTERS:
        value = counts.get(key)
        if not isinstance(value, int) or isinstance(value, bool) or value != 0:
            failures.append(f"runtime_fatal_count_nonzero_or_missing:{key}")
    if not _refs(runtime.get("serial_log_refs"), 1):
        failures.append("runtime_serial_log_refs_missing")


def _check_time_window(
    prefix: str,
    started: datetime | None,
    ended: datetime | None,
    bench_started: datetime | None,
    bench_ended: datetime | None,
    failures: list[str],
) -> None:
    if started is not None and bench_started is not None and started < bench_started:
        failures.append(f"{prefix}:started_before_bench")
    if ended is not None and bench_ended is not None and ended > bench_ended:
        failures.append(f"{prefix}:ended_after_bench")


def _check_pass_test(
    test: dict[str, Any],
    test_id: str,
    failures: list[str],
    bench_started: datetime | None = None,
    bench_ended: datetime | None = None,
    enforce_bench_window: bool = True,
) -> datetime | None:
    prefix = f"test:{test_id}"
    started = _timestamp(test.get("started_at"))
    ended = _timestamp(test.get("ended_at"))
    if started is None:
        failures.append(f"{prefix}:started_at_invalid")
    if ended is None:
        failures.append(f"{prefix}:ended_at_invalid")
    if started is not None and ended is not None and ended <= started:
        failures.append(f"{prefix}:time_not_increasing")
    if enforce_bench_window:
        _check_time_window(prefix, started, ended, bench_started, bench_ended, failures)
    if not _text(test.get("stimulus"), 8):
        failures.append(f"{prefix}:stimulus_missing")
    if not _text(test.get("expected"), 8):
        failures.append(f"{prefix}:expected_missing")
    if not _text(test.get("observed"), 8):
        failures.append(f"{prefix}:observed_missing")
    if not _refs(test.get("evidence_refs"), 1):
        failures.append(f"{prefix}:evidence_refs_missing")
    measurements = test.get("measurements")
    if not isinstance(measurements, dict):
        failures.append(f"{prefix}:measurements_not_object")
    elif not measurements:
        failures.append(f"{prefix}:measurements_empty")
    return ended


def _check_optional_test(
    test: dict[str, Any],
    failures: list[str],
    bench_started: datetime | None,
    bench_ended: datetime | None,
) -> datetime | None:
    status = str(test.get("status", "")).strip()
    prefix = f"test:{OPTIONAL_FEATURE_TEST}"
    populated = test.get("populated_features")
    absent = test.get("not_populated_features")
    if not isinstance(populated, list) or not isinstance(absent, list):
        failures.append(f"{prefix}:feature_lists_missing")
        return None
    if status == "PASS":
        if not populated:
            failures.append(f"{prefix}:pass_without_populated_features")
        return _check_pass_test(
            test,
            OPTIONAL_FEATURE_TEST,
            failures,
            bench_started,
            bench_ended,
        )
    if status == "SKIPPED_NOT_POPULATED":
        if populated:
            failures.append(f"{prefix}:skip_with_populated_features")
        if not absent:
            failures.append(f"{prefix}:skip_without_absent_features")
        if not _text(test.get("status_reason"), 8):
            failures.append(f"{prefix}:skip_reason_missing")
        if not _refs(test.get("evidence_refs"), 1):
            failures.append(f"{prefix}:population_evidence_missing")
        started = _timestamp(test.get("started_at"))
        ended = _timestamp(test.get("ended_at"))
        if started is None:
            failures.append(f"{prefix}:started_at_invalid")
        if ended is None:
            failures.append(f"{prefix}:ended_at_invalid")
        if started is not None and ended is not None and ended <= started:
            failures.append(f"{prefix}:time_not_increasing")
        _check_time_window(prefix, started, ended, bench_started, bench_ended, failures)
        return ended
    failures.append(f"{prefix}:invalid_status")
    return None


def _check_external_lab(test: dict[str, Any], failures: list[str]) -> datetime | None:
    status = str(test.get("status", "")).strip()
    prefix = f"test:{EXTERNAL_LAB_TEST}"
    if status == "PASS":
        ended = _check_pass_test(
            test,
            EXTERNAL_LAB_TEST,
            failures,
            enforce_bench_window=False,
        )
        if not _text(test.get("lab_name"), 2):
            failures.append(f"{prefix}:lab_name_missing")
        if not _text(test.get("report_ref"), 4):
            failures.append(f"{prefix}:report_ref_missing")
        if not SHA256.fullmatch(str(test.get("report_digest", "")).strip().lower()):
            failures.append(f"{prefix}:report_digest_invalid")
        return ended
    if status == "DEFERRED_EXTERNAL_LAB":
        if not _text(test.get("status_reason"), 12):
            failures.append(f"{prefix}:defer_reason_missing")
        if not _text(test.get("deferred_plan_ref"), 4):
            failures.append(f"{prefix}:deferred_plan_ref_missing")
        return None
    failures.append(f"{prefix}:invalid_status")
    return None


def _check_tests(
    record: dict[str, Any],
    failures: list[str],
    bench_started: datetime | None,
    bench_ended: datetime | None,
) -> tuple[list[str], datetime | None]:
    raw = record.get("tests")
    tests = raw if isinstance(raw, list) else []
    if not isinstance(raw, list):
        failures.append("tests_not_list")

    seen: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(tests):
        if not isinstance(item, dict):
            failures.append(f"test_invalid:{index}")
            continue
        test_id = str(item.get("id", "")).strip()
        if test_id not in H4_TEST_IDS:
            failures.append(f"test_unknown:{test_id or index}")
            continue
        if test_id in seen:
            failures.append(f"test_duplicate:{test_id}")
            continue
        seen[test_id] = item

    latest_end: datetime | None = None
    for test_id in H4_TEST_IDS:
        if test_id not in seen:
            failures.append(f"test_missing:{test_id}")
            continue
        test = seen[test_id]
        status = str(test.get("status", "")).strip()
        if status not in VALID_STATUS:
            failures.append(f"test:{test_id}:status_invalid")
            continue
        ended: datetime | None = None
        if test_id in MANDATORY_TESTS:
            if status != "PASS":
                failures.append(f"test:{test_id}:mandatory_not_pass")
            else:
                ended = _check_pass_test(
                    test,
                    test_id,
                    failures,
                    bench_started,
                    bench_ended,
                )
        elif test_id == OPTIONAL_FEATURE_TEST:
            ended = _check_optional_test(test, failures, bench_started, bench_ended)
        elif test_id == EXTERNAL_LAB_TEST:
            ended = _check_external_lab(test, failures)
        if ended is not None and (latest_end is None or ended > latest_end):
            latest_end = ended

    return sorted(seen), latest_end


def evaluate(
    record: dict[str, Any],
    expected_h2_sha: str,
    expected_provider_digest: str,
    expected_firmware_sha: str,
    expected_firmware_artifact_digest: str,
) -> H4Result:
    failures: list[str] = []

    if record.get("evidence_state") != "EXECUTED_PHYSICAL_EVIDENCE":
        failures.append("evidence_state_not_executed")

    h2, provider, firmware, firmware_digest, board_serial = _check_identity(
        record,
        expected_h2_sha,
        expected_provider_digest,
        expected_firmware_sha,
        expected_firmware_artifact_digest,
        failures,
    )
    dfm_accepted_at = _check_fabricator(record, failures)
    bench_started, bench_ended = _check_bench(record, failures)
    if (
        dfm_accepted_at is not None
        and bench_started is not None
        and dfm_accepted_at > bench_started
    ):
        failures.append("fabricator_dfm_accepted_after_bench_started")
    _check_runtime(record, failures)
    tests_seen, latest_test_end = _check_tests(
        record,
        failures,
        bench_started,
        bench_ended,
    )

    signoff = _dict(record.get("signoff"))
    if signoff.get("accepted") is not True:
        failures.append("signoff_not_accepted")
    if not _text(signoff.get("authorized_by"), 2):
        failures.append("signoff_authorized_by_missing")
    accepted_at = _timestamp(signoff.get("accepted_at"))
    if accepted_at is None:
        failures.append("signoff_accepted_at_invalid")
    latest_required = bench_ended
    if latest_test_end is not None and (latest_required is None or latest_test_end > latest_required):
        latest_required = latest_test_end
    if accepted_at is not None and latest_required is not None and accepted_at < latest_required:
        failures.append("signoff_before_testing_completed")
    if not _text(signoff.get("evidence_package_ref"), 4):
        failures.append("signoff_evidence_package_ref_missing")
    if not SHA256.fullmatch(str(signoff.get("evidence_package_digest", "")).strip().lower()):
        failures.append("signoff_evidence_package_digest_invalid")

    return H4Result(
        passed=not failures,
        h2_checkpoint_sha=h2,
        provider_artifact_digest=provider,
        firmware_sha=firmware,
        firmware_artifact_digest=firmware_digest,
        board_serial=board_serial,
        tests_seen=tests_seen,
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Rev-A H4 physical acceptance evidence")
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-h2-sha", required=True)
    parser.add_argument("--expected-provider-digest", required=True)
    parser.add_argument("--expected-firmware-sha", required=True)
    parser.add_argument("--expected-firmware-artifact-digest", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"REV-A H4 PHYSICAL EVIDENCE FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("REV-A H4 PHYSICAL EVIDENCE FAIL: JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        expected_h2_sha=args.expected_h2_sha,
        expected_provider_digest=args.expected_provider_digest,
        expected_firmware_sha=args.expected_firmware_sha,
        expected_firmware_artifact_digest=args.expected_firmware_artifact_digest,
    )
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("REV-A H4 PHYSICAL EVIDENCE PASS" if result.passed else "REV-A H4 PHYSICAL EVIDENCE FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- h2_checkpoint_sha={result.h2_checkpoint_sha}")
        print(f"- firmware_sha={result.firmware_sha}")
        print(f"- firmware_artifact_digest={result.firmware_artifact_digest}")
        print(f"- board_serial={result.board_serial}")
        print(f"- tests_seen={','.join(result.tests_seen)}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
