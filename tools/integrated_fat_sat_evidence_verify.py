#!/usr/bin/env python3
"""Fail-closed validator for final integrated PV-DG FAT/endurance/SAT evidence.

The validator checks completeness, exact release identity binding and internal
consistency of physical evidence submitted for Issue #83. It never runs plant
equipment, performs Modbus/OTA actions, signs SAT, or infers a physical PASS
from CI. An internally self-consistent record for the wrong release must fail.
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
EXECUTED_STATE = "EXECUTED_INTEGRATED_FAT_SAT_EVIDENCE"

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


@dataclass(frozen=True)
class ReleaseIdentity:
    firmware_sha: str
    firmware_tree_sha: str
    artifact_digest: str
    config_identity: str
    site_id: str
    site_map_digest: str
    profile_manifest_digest: str


@dataclass
class Result:
    passed: bool
    firmware_sha: str
    firmware_tree_sha: str
    artifact_digest: str
    config_identity: str
    site_id: str
    site_map_digest: str
    profile_manifest_digest: str
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


def canonical_digest(value: object) -> str:
    raw = str(value or "").strip().lower()
    if re.fullmatch(r"[0-9a-f]{64}", raw):
        return "sha256:" + raw
    return raw


def require_text(obj: dict[str, Any], keys: tuple[str, ...], prefix: str, failures: list[str], minimum: int = 2) -> None:
    for key in keys:
        if not text(obj.get(key), minimum):
            failures.append(f"{prefix}:{key}_missing")


def check_identity(
    record: dict[str, Any],
    expected: ReleaseIdentity,
    failures: list[str],
) -> ReleaseIdentity:
    identity = ReleaseIdentity(
        firmware_sha=str(record.get("firmware_sha", "")).strip().lower(),
        firmware_tree_sha=str(record.get("firmware_tree_sha", "")).strip().lower(),
        artifact_digest=canonical_digest(record.get("artifact_digest")),
        config_identity=str(record.get("config_identity", "")).strip(),
        site_id=str(record.get("site_id", "")).strip(),
        site_map_digest=canonical_digest(record.get("site_map_digest")),
        profile_manifest_digest=canonical_digest(record.get("profile_manifest_digest")),
    )

    if record.get("evidence_state") != EXECUTED_STATE:
        failures.append("evidence_state_not_executed")
    if not exact_sha(identity.firmware_sha):
        failures.append("firmware_sha_invalid")
    if not exact_sha(identity.firmware_tree_sha):
        failures.append("firmware_tree_sha_invalid")
    if not digest(identity.artifact_digest):
        failures.append("artifact_digest_invalid")
    if not text(identity.config_identity, 4):
        failures.append("config_identity_missing")
    if not text(identity.site_id, 2):
        failures.append("site_id_missing")
    if not digest(identity.site_map_digest):
        failures.append("site_map_digest_invalid")
    if not digest(identity.profile_manifest_digest):
        failures.append("profile_manifest_digest_invalid")

    expected_values = (
        ("firmware_sha", identity.firmware_sha, expected.firmware_sha, exact_sha),
        ("firmware_tree_sha", identity.firmware_tree_sha, expected.firmware_tree_sha, exact_sha),
        ("artifact_digest", identity.artifact_digest, canonical_digest(expected.artifact_digest), digest),
        ("config_identity", identity.config_identity, expected.config_identity, lambda v: text(v, 4)),
        ("site_id", identity.site_id, expected.site_id, lambda v: text(v, 2)),
        ("site_map_digest", identity.site_map_digest, canonical_digest(expected.site_map_digest), digest),
        ("profile_manifest_digest", identity.profile_manifest_digest, canonical_digest(expected.profile_manifest_digest), digest),
    )
    for key, actual, wanted, validator in expected_values:
        if not validator(wanted):
            failures.append(f"expected:{key}_invalid")
        elif actual != wanted:
            failures.append(f"{key}_expected_mismatch")

    approved_profiles_ref = str(record.get("approved_profiles_ref", "")).strip()
    if not text(approved_profiles_ref, 4):
        failures.append("approved_profiles_ref_missing")
    profiles = record.get("approved_profiles")
    if not isinstance(profiles, list) or not profiles:
        failures.append("approved_profiles_missing")
    else:
        seen: set[str] = set()
        for index, profile in enumerate(profiles):
            prefix = f"approved_profile:{index}"
            if not isinstance(profile, dict):
                failures.append(f"{prefix}:invalid")
                continue
            profile_id = str(profile.get("profile_id", "")).strip()
            if not text(profile_id, 2):
                failures.append(f"{prefix}:profile_id_missing")
            elif profile_id in seen:
                failures.append(f"approved_profile:{profile_id}:duplicate")
            else:
                seen.add(profile_id)
            require_text(
                profile,
                ("manufacturer", "model", "inverter_firmware", "manual_revision", "approval_ref"),
                f"approved_profile:{profile_id or index}",
                failures,
                2,
            )

    return identity


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


def check_scenario_identity(
    s: dict[str, Any],
    prefix: str,
    identity: ReleaseIdentity,
    failures: list[str],
) -> None:
    checks = (
        ("firmware_sha", str(s.get("firmware_sha", "")).strip().lower(), identity.firmware_sha),
        ("firmware_tree_sha", str(s.get("firmware_tree_sha", "")).strip().lower(), identity.firmware_tree_sha),
        ("artifact_digest", canonical_digest(s.get("artifact_digest")), identity.artifact_digest),
        ("config_identity", str(s.get("config_identity", "")).strip(), identity.config_identity),
        ("site_id", str(s.get("site_id", "")).strip(), identity.site_id),
        ("site_map_digest", canonical_digest(s.get("site_map_digest")), identity.site_map_digest),
        ("profile_manifest_digest", canonical_digest(s.get("profile_manifest_digest")), identity.profile_manifest_digest),
    )
    for key, actual, expected in checks:
        if actual != expected:
            failures.append(f"{prefix}:{key}_mismatch")


def check_scenario(s: dict[str, Any], group: str, identity: ReleaseIdentity, failures: list[str]) -> str:
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
        check_scenario_identity(s, prefix, identity, failures)
        return scenario_id
    if status != "pass":
        failures.append(f"{prefix}:status_not_pass")
        return scenario_id

    check_scenario_identity(s, prefix, identity, failures)
    start = aware_time(s.get("started_at"))
    end = aware_time(s.get("ended_at"))
    if start is None or end is None or end <= start:
        failures.append(f"{prefix}:timestamps_invalid")
    require_text(
        s,
        (
            "step",
            "source_provenance",
            "endpoint_unit_map_ref",
            "meter_role_map_ref",
            "source_contact_map_ref",
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
    measurements = s.get("measurements")
    if not isinstance(measurements, dict) or not measurements:
        failures.append(f"{prefix}:measurements_missing")
    command_values = s.get("command_readback_values")
    if not isinstance(command_values, dict) or not command_values:
        failures.append(f"{prefix}:command_readback_values_missing")
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


def check_group(
    record: dict[str, Any],
    group: str,
    required_ids: set[str],
    identity: ReleaseIdentity,
    failures: list[str],
) -> int:
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
        sid = check_scenario(item, group, identity, failures)
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
            value = fatal.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value != 0:
                failures.append(f"endurance_summary:fatal_count_nonzero_or_invalid:{key}")


def check_sat(record: dict[str, Any], identity: ReleaseIdentity, failures: list[str]) -> None:
    s = record.get("sat")
    if not isinstance(s, dict):
        failures.append("sat_missing")
        return
    if s.get("accepted") is not True:
        failures.append("sat:not_accepted")
    require_text(
        s,
        (
            "site_representative",
            "role",
            "signed_at",
            "signed_record_ref",
            "firmware_sha",
            "firmware_tree_sha",
            "artifact_digest",
            "config_identity",
            "site_id",
            "site_source_mapping_ref",
            "approved_profiles_ref",
        ),
        "sat",
        failures,
        3,
    )
    if not aware_time(s.get("signed_at")):
        failures.append("sat:signed_at_invalid")
    sat_checks = (
        ("firmware_sha", str(s.get("firmware_sha", "")).strip().lower(), identity.firmware_sha),
        ("firmware_tree_sha", str(s.get("firmware_tree_sha", "")).strip().lower(), identity.firmware_tree_sha),
        ("artifact_digest", canonical_digest(s.get("artifact_digest")), identity.artifact_digest),
        ("config_identity", str(s.get("config_identity", "")).strip(), identity.config_identity),
        ("site_id", str(s.get("site_id", "")).strip(), identity.site_id),
        ("site_map_digest", canonical_digest(s.get("site_map_digest")), identity.site_map_digest),
        ("profile_manifest_digest", canonical_digest(s.get("profile_manifest_digest")), identity.profile_manifest_digest),
    )
    for key, actual, expected in sat_checks:
        if actual != expected:
            failures.append(f"sat:{key}_mismatch")
    if not digest(canonical_digest(s.get("signed_record_digest"))):
        failures.append("sat:signed_record_digest_invalid")
    if s.get("no_behavior_change_after_evidence") is not True:
        failures.append("sat:post_evidence_behavior_change_not_forbidden")
    if s.get("physical_executor_attestation") is not True:
        failures.append("sat:physical_executor_attestation_missing")
    if s.get("site_acceptance_signature_present") is not True:
        failures.append("sat:site_signature_missing")


def evaluate(record: dict[str, Any], expected: ReleaseIdentity) -> Result:
    failures: list[str] = []
    identity = check_identity(record, expected, failures)
    check_prerequisites(record, failures)
    count = 0
    count += check_group(record, "grid", REQUIRED_GRID, identity, failures)
    count += check_group(record, "generator", REQUIRED_GENERATOR, identity, failures)
    count += check_group(record, "mixed", REQUIRED_MIXED, identity, failures)
    count += check_group(record, "modbus", REQUIRED_MODBUS, identity, failures)
    count += check_group(record, "ota", REQUIRED_OTA, identity, failures)
    check_endurance(record, failures)
    check_sat(record, identity, failures)

    if record.get("all_evidence_same_release_identity") is not True:
        failures.append("all_evidence_same_release_identity_not_proven")
    if record.get("simulator_or_ci_used_as_physical_substitute") is not False:
        failures.append("simulator_or_ci_physical_substitution_not_forbidden")
    if record.get("release_ready") is not True:
        failures.append("release_ready_not_true")
    if not text(record.get("release_verdict_reason"), 16):
        failures.append("release_verdict_reason_missing")

    return Result(
        passed=not failures,
        firmware_sha=identity.firmware_sha,
        firmware_tree_sha=identity.firmware_tree_sha,
        artifact_digest=identity.artifact_digest,
        config_identity=identity.config_identity,
        site_id=identity.site_id,
        site_map_digest=identity.site_map_digest,
        profile_manifest_digest=identity.profile_manifest_digest,
        scenario_count=count,
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate integrated PV-DG FAT, endurance and signed SAT evidence")
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-firmware-sha", required=True)
    parser.add_argument("--expected-firmware-tree-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--expected-config-identity", required=True)
    parser.add_argument("--expected-site-id", required=True)
    parser.add_argument("--expected-site-map-digest", required=True)
    parser.add_argument("--expected-profile-manifest-digest", required=True)
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

    expected = ReleaseIdentity(
        firmware_sha=str(args.expected_firmware_sha).strip().lower(),
        firmware_tree_sha=str(args.expected_firmware_tree_sha).strip().lower(),
        artifact_digest=canonical_digest(args.expected_artifact_digest),
        config_identity=str(args.expected_config_identity).strip(),
        site_id=str(args.expected_site_id).strip(),
        site_map_digest=canonical_digest(args.expected_site_map_digest),
        profile_manifest_digest=canonical_digest(args.expected_profile_manifest_digest),
    )
    result = evaluate(record, expected)
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("INTEGRATED FAT/SAT PASS" if result.passed else "INTEGRATED FAT/SAT FAIL")
        print(
            f"firmware_sha={result.firmware_sha} tree={result.firmware_tree_sha} "
            f"site={result.site_id} scenarios={result.scenario_count}"
        )
        for failure in result.failures:
            print(f"- {failure}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
