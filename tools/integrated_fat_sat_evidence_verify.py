#!/usr/bin/env python3
"""Fail-closed validator for final integrated PV-DG FAT/endurance/SAT evidence.

The validator checks completeness, identity binding and internal consistency of
physical evidence submitted for Issue #83. It never runs plant equipment,
performs Modbus/OTA actions, signs SAT, or infers a physical PASS from CI.
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

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^(?:sha256:)?[0-9a-f]{64}$")

REQUIRED_GRID = {
    "grid_zero_export",
    "grid_limited_export",
    "grid_minimum_import",
    "grid_load_rise_rejection",
    "grid_meter_stale_loss_recovery",
    "grid_inverter_loss_recovery",
}
REQUIRED_GENERATOR = {
    "generator_single",
    "generator_multiple",
    "generator_minimum_loading",
    "generator_reserve_reverse_power",
    "generator_load_rejection",
    "generator_meter_stale_loss",
    "generator_run_breaker_conflict",
}
REQUIRED_MIXED = {
    "mixed_grid_transfer_generator",
    "mixed_generator_transfer_grid",
    "mixed_island",
    "mixed_synchronism",
    "mixed_no_source_unknown_conflict_stale",
    "mixed_fresh_recovery_dwell",
}
REQUIRED_MODBUS = {
    "modbus_per_transaction_healthy",
    "modbus_persistent_healthy",
    "modbus_reconnect_on_error_healthy",
    "modbus_slow_slave_timeout",
    "modbus_dead_slave",
    "modbus_exception_preservation",
    "modbus_tcp_reset_reconnect",
    "modbus_gateway_restart",
    "modbus_repeated_connect_close",
    "modbus_multi_device_load",
    "modbus_resource_trend",
    "modbus_unrelated_services_responsive",
}
REQUIRED_OTA = {
    "ota_authenticated_upload",
    "ota_invalid_image_rejected_before_write",
    "ota_interrupted_upload",
    "ota_power_loss",
    "ota_partial_image_not_selected",
    "ota_previous_slot_boot",
    "ota_explicit_staged_reboot",
    "ota_pending_verification_first_boot",
    "ota_mark_valid_stabilization",
    "ota_deliberate_rollback",
    "ota_fail_closed_control",
    "ota_no_nvs_full_flash_erase",
}
FATAL_COUNT_KEYS = ("wdt", "panic", "no_mem", "unexpected_reset", "resource_collapse")


@dataclass
class Result:
    passed: bool
    firmware_sha: str
    artifact_digest: str
    config_identity: str
    scenario_count: int
    failures: list[str]


def text(value: object, minimum: int = 1) -> bool:
    return len(str(value or "").strip()) >= minimum


def number(value: object) -> bool:
    if isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def aware_time(value: object) -> datetime | None:
    try:
        parsed = datetime.fromisoformat(str(value or "").replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo is not None else None


def exact_sha(value: object) -> bool:
    return bool(SHA40.fullmatch(str(value or "").strip().lower()))


def digest(value: object) -> bool:
    return bool(SHA256.fullmatch(str(value or "").strip().lower()))


def require_text(obj: dict[str, Any], keys: tuple[str, ...], prefix: str, failures: list[str], minimum: int = 2) -> None:
    for key in keys:
        if not text(obj.get(key), minimum):
            failures.append(f"{prefix}:{key}_missing")


def check_identity(record: dict[str, Any], failures: list[str]) -> tuple[str, str, str]:
    firmware_sha = str(record.get("firmware_sha", "")).strip().lower()
    artifact_digest = str(record.get("artifact_digest", "")).strip().lower()
    config_identity = str(record.get("config_identity", "")).strip()
    if not exact_sha(firmware_sha):
        failures.append("firmware_sha_invalid")
    if not digest(artifact_digest):
        failures.append("artifact_digest_invalid")
    if not text(config_identity, 4):
        failures.append("config_identity_missing")
    if not text(record.get("site_id"), 2):
        failures.append("site_id_missing")
    return firmware_sha, artifact_digest, config_identity


def check_prerequisites(record: dict[str, Any], failures: list[str]) -> None:
    p = record.get("prerequisites")
    if not isinstance(p, dict):
        failures.append("prerequisites_missing")
        return
    required = (
        "waveshare_final_acceptance",
        "backend_parity_persistence_arm",
        "source_transition_physical",
        "site_source_commissioning",
        "inverter_profiles_production_approved",
        "ota_physical_qualification",
    )
    for key in required:
        item = p.get(key)
        prefix = f"prerequisite:{key}"
        if not isinstance(item, dict):
            failures.append(f"{prefix}:missing")
            continue
        if item.get("passed") is not True:
            failures.append(f"{prefix}:not_passed")
        require_text(item, ("evidence_ref", "identity"), prefix, failures, 4)

    profiles = p.get("inverter_profiles_production_approved")
    if isinstance(profiles, dict):
        ids = profiles.get("profile_ids")
        if not isinstance(ids, list) or not ids or not all(text(x, 2) for x in ids):
            failures.append("prerequisite:inverter_profiles_production_approved:profile_ids_missing")


def check_scenario(s: dict[str, Any], group: str, failures: list[str]) -> str:
    scenario_id = str(s.get("id", "")).strip()
    prefix = f"scenario:{group}:{scenario_id or 'missing'}"
    if not scenario_id:
        failures.append(f"scenario:{group}:id_missing")
        return ""
    status = str(s.get("status", "")).strip()
    if status == "not_supported":
        if scenario_id != "mixed_synchronism":
            failures.append(f"{prefix}:not_supported_only_allowed_for_synchronism")
        if not text(s.get("topology_ref"), 4) or not text(s.get("reason"), 8):
            failures.append(f"{prefix}:not_supported_proof_missing")
        return scenario_id
    if status != "pass":
        failures.append(f"{prefix}:status_not_pass")
        return scenario_id

    start = aware_time(s.get("started_at"))
    end = aware_time(s.get("ended_at"))
    if start is None or end is None or end <= start:
        failures.append(f"{prefix}:timestamps_invalid")
    require_text(
        s,
        (
            "step",
            "source_provenance",
            "meter_values_ref",
            "command_readback_ref",
            "serial_runtime_log_ref",
            "hmi_http_evidence_ref",
            "expected_state",
            "observed_state",
            "pass_reason",
        ),
        prefix,
        failures,
        3,
    )
    if s.get("fail_closed_when_required") is not True:
        failures.append(f"{prefix}:fail_closed_not_proven")
    if s.get("expected_observed_match") is not True:
        failures.append(f"{prefix}:expected_observed_mismatch")
    fatal = s.get("fatal_counts")
    if not isinstance(fatal, dict):
        failures.append(f"{prefix}:fatal_counts_missing")
    else:
        for key in FATAL_COUNT_KEYS:
            value = fatal.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value != 0:
                failures.append(f"{prefix}:fatal_count_nonzero_or_invalid:{key}")
    return scenario_id


def check_group(record: dict[str, Any], group: str, required_ids: set[str], failures: list[str]) -> int:
    matrices = record.get("matrices")
    if not isinstance(matrices, dict):
        failures.append("matrices_missing")
        return 0
    raw = matrices.get(group)
    if not isinstance(raw, list):
        failures.append(f"matrix:{group}_missing")
        return 0
    seen: set[str] = set()
    for item in raw:
        if not isinstance(item, dict):
            failures.append(f"matrix:{group}:invalid_entry")
            continue
        sid = check_scenario(item, group, failures)
        if sid in seen:
            failures.append(f"matrix:{group}:duplicate:{sid}")
        if sid:
            seen.add(sid)
    for sid in sorted(required_ids - seen):
        failures.append(f"matrix:{group}:required_missing:{sid}")
    return len(seen)


def check_endurance(record: dict[str, Any], failures: list[str]) -> None:
    e = record.get("endurance_summary")
    if not isinstance(e, dict):
        failures.append("endurance_summary_missing")
        return
    if e.get("passed") is not True:
        failures.append("endurance_summary_not_passed")
    for key in ("duration_seconds", "sample_count", "min_heap_free", "min_largest_block", "min_dma_free"):
        if not number(e.get(key)) or float(e.get(key, -1)) < 0:
            failures.append(f"endurance_summary:{key}_invalid")
    if float(e.get("duration_seconds", 0) or 0) <= 0 or int(float(e.get("sample_count", 0) or 0)) <= 0:
        failures.append("endurance_summary:no_runtime_samples")
    require_text(e, ("resource_trend_ref", "socket_lwip_resource_ref", "multi_device_load_ref"), "endurance_summary", failures, 4)
    if e.get("unrelated_control_web_responsive") is not True:
        failures.append("endurance_summary:unrelated_services_not_proven")
    fatal = e.get("fatal_counts")
    if not isinstance(fatal, dict):
        failures.append("endurance_summary:fatal_counts_missing")
    else:
        for key in FATAL_COUNT_KEYS:
            if fatal.get(key) != 0:
                failures.append(f"endurance_summary:fatal_count_nonzero_or_invalid:{key}")


def check_sat(record: dict[str, Any], firmware_sha: str, config_identity: str, failures: list[str]) -> None:
    s = record.get("sat")
    if not isinstance(s, dict):
        failures.append("sat_missing")
        return
    if s.get("accepted") is not True:
        failures.append("sat:not_accepted")
    require_text(
        s,
        ("site_representative", "role", "signed_at", "signed_record_ref", "firmware_sha", "config_identity", "approved_profile_set_identity"),
        "sat",
        failures,
        3,
    )
    if not aware_time(s.get("signed_at")):
        failures.append("sat:signed_at_invalid")
    if str(s.get("firmware_sha", "")).strip().lower() != firmware_sha:
        failures.append("sat:firmware_sha_mismatch")
    if str(s.get("config_identity", "")).strip() != config_identity:
        failures.append("sat:config_identity_mismatch")
    if s.get("no_behavior_change_after_evidence") is not True:
        failures.append("sat:post_evidence_behavior_change_not_forbidden")
    if s.get("physical_executor_attestation") is not True:
        failures.append("sat:physical_executor_attestation_missing")
    if s.get("site_acceptance_signature_present") is not True:
        failures.append("sat:site_signature_missing")


def evaluate(record: dict[str, Any]) -> Result:
    failures: list[str] = []
    firmware_sha, artifact_digest, config_identity = check_identity(record, failures)
    check_prerequisites(record, failures)
    count = 0
    count += check_group(record, "grid", REQUIRED_GRID, failures)
    count += check_group(record, "generator", REQUIRED_GENERATOR, failures)
    count += check_group(record, "mixed", REQUIRED_MIXED, failures)
    count += check_group(record, "modbus", REQUIRED_MODBUS, failures)
    count += check_group(record, "ota", REQUIRED_OTA, failures)
    check_endurance(record, failures)
    check_sat(record, firmware_sha, config_identity, failures)

    if record.get("all_evidence_same_release_identity") is not True:
        failures.append("all_evidence_same_release_identity_not_proven")
    if record.get("simulator_or_ci_used_as_physical_substitute") is not False:
        failures.append("simulator_or_ci_physical_substitution_not_forbidden")
    if record.get("release_ready") is not True:
        failures.append("release_ready_not_true")
    if not text(record.get("release_verdict_reason"), 16):
        failures.append("release_verdict_reason_missing")

    return Result(not failures, firmware_sha, artifact_digest, config_identity, count, failures)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate integrated PV-DG FAT, endurance and signed SAT evidence")
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"INTEGRATED FAT/SAT FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("INTEGRATED FAT/SAT FAIL: JSON root must be an object", file=sys.stderr)
        return 2
    result = evaluate(record)
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("INTEGRATED FAT/SAT PASS" if result.passed else "INTEGRATED FAT/SAT FAIL")
        print(f"firmware_sha={result.firmware_sha} scenarios={result.scenario_count}")
        for failure in result.failures:
            print(f"- {failure}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
