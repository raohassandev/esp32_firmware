#!/usr/bin/env python3
"""Validate recorded secure-OTA real-controller qualification evidence.

This tool never uploads firmware, reboots a controller, manipulates partitions,
or observes hardware. It validates a record produced by an authorized physical
executor against one exact intended OTA-capable release identity. Missing,
ambiguous, cross-identity, or unsafe evidence fails closed.
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


REQUIRED_SCENARIOS = (
    "authenticated_upload",
    "invalid_image_rejection",
    "interrupted_upload",
    "power_loss_during_update",
    "partial_image_not_selected",
    "previous_slot_boot",
    "staged_explicit_reboot",
    "pending_verification_first_boot",
    "mark_valid_stabilization",
    "deliberate_rollback",
    "fail_closed_control",
    "nvs_persistence",
)

UNCHANGED_BOOT_TARGET_SCENARIOS = {
    "invalid_image_rejection",
    "interrupted_upload",
    "power_loss_during_update",
    "partial_image_not_selected",
}

NVS_CONTINUITY_SCENARIOS = {
    "power_loss_during_update",
    "previous_slot_boot",
    "staged_explicit_reboot",
    "pending_verification_first_boot",
    "mark_valid_stabilization",
    "deliberate_rollback",
    "nvs_persistence",
}

VALID_LIFECYCLE_STATES = {
    "idle",
    "staging",
    "staged",
    "pending_verification",
    "valid",
    "rollback",
    "previous_slot",
}

REQUIRED_FATAL_COUNTS = (
    "wdt",
    "panic",
    "no_mem",
    "unexpected_reset",
    "resource_collapse",
)

MIN_MARK_VALID_STABILIZATION_SECONDS = 30.0
GIT_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")


@dataclass
class OtaPhysicalResult:
    passed: bool
    release_source_sha: str
    release_tree_sha: str
    release_artifact_digest: str
    release_app_digest: str
    config_identity: str
    scenarios_seen: list[str]
    failures: list[str]


def _text(value: object, minimum: int = 1) -> bool:
    return len(str(value or "").strip()) >= minimum


def _git_sha(value: object) -> bool:
    return GIT_SHA_RE.fullmatch(str(value or "").strip().lower()) is not None


def _sha256(value: object) -> bool:
    return SHA256_RE.fullmatch(str(value or "").strip().lower()) is not None


def _timestamp(value: object) -> datetime | None:
    raw = str(value or "").strip()
    if not raw:
        return None
    try:
        parsed = datetime.fromisoformat(raw.replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo is not None else None


def _finite(value: object) -> bool:
    if isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def _partition(value: object) -> str:
    return str(value or "").strip()


def _same_nvs(before: object, after: object) -> bool:
    return before is not None and after is not None and before == after


def _check_common_scenario(
    scenario: dict[str, Any],
    expected_release_app_digest: str,
    previous_valid_partition: str,
    target_partition: str,
    failures: list[str],
) -> None:
    scenario_id = str(scenario.get("id", "")).strip()
    prefix = f"scenario:{scenario_id or 'missing'}"

    if scenario.get("outcome") != "pass":
        failures.append(f"{prefix}:outcome_not_pass")

    started = _timestamp(scenario.get("started_at"))
    ended = _timestamp(scenario.get("ended_at"))
    if started is None:
        failures.append(f"{prefix}:started_at_invalid")
    if ended is None:
        failures.append(f"{prefix}:ended_at_invalid")
    if started is not None and ended is not None and ended <= started:
        failures.append(f"{prefix}:time_not_increasing")

    running_before = _partition(scenario.get("running_partition_before"))
    running_after = _partition(scenario.get("running_partition_after"))
    boot_before = _partition(scenario.get("boot_partition_before"))
    boot_after = _partition(scenario.get("boot_partition_after"))
    for key, value in (
        ("running_partition_before", running_before),
        ("running_partition_after", running_after),
        ("boot_partition_before", boot_before),
        ("boot_partition_after", boot_after),
    ):
        if not value:
            failures.append(f"{prefix}:{key}_missing")

    lifecycle_before = str(scenario.get("lifecycle_before", "")).strip()
    lifecycle_after = str(scenario.get("lifecycle_after", "")).strip()
    if lifecycle_before not in VALID_LIFECYCLE_STATES:
        failures.append(f"{prefix}:lifecycle_before_invalid")
    if lifecycle_after not in VALID_LIFECYCLE_STATES:
        failures.append(f"{prefix}:lifecycle_after_invalid")

    if scenario.get("expected_control_state") != "blocked":
        failures.append(f"{prefix}:expected_control_not_blocked")
    if scenario.get("observed_control_state") != "blocked":
        failures.append(f"{prefix}:observed_control_not_blocked")
    if not _text(scenario.get("control_evidence_ref"), 4):
        failures.append(f"{prefix}:control_evidence_ref_missing")

    fatal_counts = scenario.get("fatal_counts")
    if not isinstance(fatal_counts, dict):
        failures.append(f"{prefix}:fatal_counts_missing")
    else:
        for key in REQUIRED_FATAL_COUNTS:
            value = fatal_counts.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value != 0:
                failures.append(f"{prefix}:fatal_count_nonzero_or_missing:{key}")

    reboot = scenario.get("reboot")
    if not isinstance(reboot, dict):
        failures.append(f"{prefix}:reboot_evidence_missing")
    else:
        count = reboot.get("count")
        if not isinstance(count, int) or isinstance(count, bool) or count < 0:
            failures.append(f"{prefix}:reboot_count_invalid")
        if not _text(reboot.get("reason"), 3):
            failures.append(f"{prefix}:reboot_reason_missing")

    refs = scenario.get("references")
    if not isinstance(refs, dict):
        failures.append(f"{prefix}:references_missing")
    else:
        if not _text(refs.get("serial_log_ref"), 4):
            failures.append(f"{prefix}:serial_log_ref_missing")
        if not _text(refs.get("api_hmi_ref"), 4):
            failures.append(f"{prefix}:api_hmi_ref_missing")

    if not _text(scenario.get("evidence_note"), 12):
        failures.append(f"{prefix}:evidence_note_missing")

    if scenario_id in UNCHANGED_BOOT_TARGET_SCENARIOS:
        if boot_before and boot_after and boot_after != boot_before:
            failures.append(f"{prefix}:incomplete_or_invalid_image_selected")
        if scenario.get("partial_or_invalid_selected") is not False:
            failures.append(f"{prefix}:partial_or_invalid_selected_not_false")

    if scenario_id in NVS_CONTINUITY_SCENARIOS:
        if not _same_nvs(scenario.get("nvs_before"), scenario.get("nvs_after")):
            failures.append(f"{prefix}:nvs_continuity_failed")

    if scenario_id == "authenticated_upload":
        if scenario.get("engineering_authenticated") is not True:
            failures.append(f"{prefix}:engineering_auth_not_proven")
        if scenario.get("upload_accepted") is not True:
            failures.append(f"{prefix}:valid_upload_not_accepted")
        if scenario.get("firmware_write_completed") is not True:
            failures.append(f"{prefix}:firmware_write_completion_not_proven")
        if scenario.get("staged_image_validated") is not True:
            failures.append(f"{prefix}:staged_image_validation_not_proven")
        staged_digest = str(scenario.get("staged_app_digest", "")).strip().lower()
        if staged_digest != expected_release_app_digest.lower():
            failures.append(f"{prefix}:staged_app_digest_mismatch")
        if boot_after and boot_after != target_partition:
            failures.append(f"{prefix}:validated_image_not_selected_as_target")

    elif scenario_id == "invalid_image_rejection":
        if scenario.get("engineering_authenticated") is not True:
            failures.append(f"{prefix}:engineering_auth_not_proven")
        if scenario.get("upload_rejected") is not True:
            failures.append(f"{prefix}:invalid_upload_not_rejected")
        if scenario.get("rejected_before_firmware_write") is not True:
            failures.append(f"{prefix}:rejection_before_firmware_write_not_proven")
        if scenario.get("rejected_before_boot_selection") is not True:
            failures.append(f"{prefix}:rejection_before_boot_selection_not_proven")

    elif scenario_id == "interrupted_upload":
        if scenario.get("transport_interrupted") is not True:
            failures.append(f"{prefix}:transport_interruption_not_proven")
        if scenario.get("upload_completed") is not False:
            failures.append(f"{prefix}:interrupted_upload_marked_complete")

    elif scenario_id == "power_loss_during_update":
        if scenario.get("controlled_power_loss") is not True:
            failures.append(f"{prefix}:controlled_power_loss_not_proven")
        if running_after and running_after != previous_valid_partition:
            failures.append(f"{prefix}:previous_valid_partition_not_running_after_power_loss")
        if boot_after and boot_after != previous_valid_partition:
            failures.append(f"{prefix}:previous_valid_partition_not_boot_target_after_power_loss")

    elif scenario_id == "partial_image_not_selected":
        if scenario.get("partial_image_present") is not True:
            failures.append(f"{prefix}:partial_image_presence_not_proven")
        if boot_after and boot_after != previous_valid_partition:
            failures.append(f"{prefix}:partial_image_changed_boot_target")

    elif scenario_id == "previous_slot_boot":
        if running_after and running_after != previous_valid_partition:
            failures.append(f"{prefix}:previous_slot_not_running")
        if boot_after and boot_after != previous_valid_partition:
            failures.append(f"{prefix}:previous_slot_not_boot_target")
        if scenario.get("previous_slot_boot_proven") is not True:
            failures.append(f"{prefix}:previous_slot_boot_not_proven")

    elif scenario_id == "staged_explicit_reboot":
        if boot_before and boot_before != target_partition:
            failures.append(f"{prefix}:staged_target_not_boot_target_before_reboot")
        if running_before and running_before != previous_valid_partition:
            failures.append(f"{prefix}:previous_partition_not_running_before_explicit_reboot")
        if scenario.get("staged_image_validated") is not True:
            failures.append(f"{prefix}:staged_image_validation_not_proven")
        if scenario.get("explicit_authenticated_reboot") is not True:
            failures.append(f"{prefix}:explicit_authenticated_reboot_not_proven")
        if running_after and running_after != target_partition:
            failures.append(f"{prefix}:target_partition_not_running_after_explicit_reboot")
        if boot_after and boot_after != target_partition:
            failures.append(f"{prefix}:target_partition_not_boot_target_after_explicit_reboot")

    elif scenario_id == "pending_verification_first_boot":
        if lifecycle_after != "pending_verification":
            failures.append(f"{prefix}:pending_verification_not_observed")
        if scenario.get("marked_valid") is not False:
            failures.append(f"{prefix}:first_boot_prematurely_marked_valid")
        if running_after and running_after != target_partition:
            failures.append(f"{prefix}:target_partition_not_running_pending_verification")
        if boot_after and boot_after != target_partition:
            failures.append(f"{prefix}:target_partition_not_boot_target_pending_verification")

    elif scenario_id == "mark_valid_stabilization":
        seconds = scenario.get("stabilization_seconds")
        if not _finite(seconds) or float(seconds) < MIN_MARK_VALID_STABILIZATION_SECONDS:
            failures.append(f"{prefix}:stabilization_too_short")
        if scenario.get("marked_valid") is not True:
            failures.append(f"{prefix}:mark_valid_not_proven")
        if lifecycle_after != "valid":
            failures.append(f"{prefix}:valid_lifecycle_not_observed")
        if running_after and running_after != target_partition:
            failures.append(f"{prefix}:validated_target_not_running")
        if boot_after and boot_after != target_partition:
            failures.append(f"{prefix}:validated_target_not_boot_target")

    elif scenario_id == "deliberate_rollback":
        if scenario.get("rollback_triggered") is not True:
            failures.append(f"{prefix}:rollback_trigger_not_proven")
        if scenario.get("previous_slot_recovered") is not True:
            failures.append(f"{prefix}:previous_slot_recovery_not_proven")
        if running_after and running_after != previous_valid_partition:
            failures.append(f"{prefix}:rollback_did_not_restore_previous_partition")
        if boot_after and boot_after != previous_valid_partition:
            failures.append(f"{prefix}:rollback_did_not_restore_previous_boot_target")

    elif scenario_id == "fail_closed_control":
        if scenario.get("uncertainty_states_exercised") is not True:
            failures.append(f"{prefix}:uncertainty_states_not_exercised")
        if scenario.get("unsafe_command_observed") is not False:
            failures.append(f"{prefix}:unsafe_command_not_explicitly_absent")

    elif scenario_id == "nvs_persistence":
        if scenario.get("nvs_erase_used") is not False:
            failures.append(f"{prefix}:nvs_erase_not_explicitly_absent")
        if scenario.get("full_flash_erase_used") is not False:
            failures.append(f"{prefix}:full_flash_erase_not_explicitly_absent")
        if scenario.get("authoritative_values_restored") is not True:
            failures.append(f"{prefix}:authoritative_values_not_restored")


def evaluate(
    record: dict[str, Any],
    expected_source_sha: str,
    expected_tree_sha: str,
    expected_artifact_digest: str,
    expected_app_digest: str,
    expected_config_identity: str,
) -> OtaPhysicalResult:
    failures: list[str] = []

    source_sha = str(record.get("release_source_sha", "")).strip().lower()
    tree_sha = str(record.get("release_tree_sha", "")).strip().lower()
    artifact_digest = str(record.get("release_artifact_digest", "")).strip().lower()
    app_digest = str(record.get("release_app_digest", "")).strip().lower()
    config_identity = str(record.get("config_identity", "")).strip()

    if not _git_sha(source_sha):
        failures.append("release_source_sha_invalid")
    if not _git_sha(tree_sha):
        failures.append("release_tree_sha_invalid")
    if not _sha256(artifact_digest):
        failures.append("release_artifact_digest_invalid")
    if not _sha256(app_digest):
        failures.append("release_app_digest_invalid")
    if not _text(config_identity, 8):
        failures.append("config_identity_invalid")

    if source_sha != expected_source_sha.strip().lower():
        failures.append("release_source_sha_mismatch")
    if tree_sha != expected_tree_sha.strip().lower():
        failures.append("release_tree_sha_mismatch")
    if artifact_digest != expected_artifact_digest.strip().lower():
        failures.append("release_artifact_digest_mismatch")
    if app_digest != expected_app_digest.strip().lower():
        failures.append("release_app_digest_mismatch")
    if config_identity != expected_config_identity:
        failures.append("config_identity_mismatch")

    previous_valid_partition = _partition(record.get("previous_valid_partition"))
    target_partition = _partition(record.get("target_partition"))
    if not previous_valid_partition:
        failures.append("previous_valid_partition_missing")
    if not target_partition:
        failures.append("target_partition_missing")
    if previous_valid_partition and target_partition and previous_valid_partition == target_partition:
        failures.append("previous_and_target_partition_not_distinct")

    if record.get("bootloader_rollback_enabled") is not True:
        failures.append("bootloader_rollback_not_proven")
    if not _text(record.get("partition_table_identity"), 4):
        failures.append("partition_table_identity_missing")
    if not _text(record.get("engineering_auth_ref"), 4):
        failures.append("engineering_auth_ref_missing")
    if not _text(record.get("fresh_ci_ref"), 4):
        failures.append("fresh_ci_ref_missing")
    if not _text(record.get("package_identity_ref"), 4):
        failures.append("package_identity_ref_missing")

    scenarios_raw = record.get("scenarios")
    scenarios = scenarios_raw if isinstance(scenarios_raw, list) else []
    seen: dict[str, dict[str, Any]] = {}
    for index, item in enumerate(scenarios):
        if not isinstance(item, dict):
            failures.append(f"scenario_invalid:{index}")
            continue
        scenario_id = str(item.get("id", "")).strip()
        if not scenario_id:
            failures.append(f"scenario_id_missing:{index}")
            continue
        if scenario_id in seen:
            failures.append(f"scenario_duplicate:{scenario_id}")
            continue
        seen[scenario_id] = item

    for required in REQUIRED_SCENARIOS:
        if required not in seen:
            failures.append(f"scenario_missing:{required}")

    for scenario_id in REQUIRED_SCENARIOS:
        scenario = seen.get(scenario_id)
        if scenario is not None:
            _check_common_scenario(
                scenario,
                expected_release_app_digest=app_digest,
                previous_valid_partition=previous_valid_partition,
                target_partition=target_partition,
                failures=failures,
            )

    return OtaPhysicalResult(
        passed=not failures,
        release_source_sha=source_sha,
        release_tree_sha=tree_sha,
        release_artifact_digest=artifact_digest,
        release_app_digest=app_digest,
        config_identity=config_identity,
        scenarios_seen=sorted(seen),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate secure OTA real-controller qualification evidence"
    )
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-source-sha", required=True)
    parser.add_argument("--expected-tree-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--expected-app-digest", required=True)
    parser.add_argument("--expected-config-identity", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"OTA PHYSICAL EVIDENCE FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("OTA PHYSICAL EVIDENCE FAIL: JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        expected_source_sha=args.expected_source_sha,
        expected_tree_sha=args.expected_tree_sha,
        expected_artifact_digest=args.expected_artifact_digest,
        expected_app_digest=args.expected_app_digest,
        expected_config_identity=args.expected_config_identity,
    )

    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("OTA PHYSICAL EVIDENCE PASS" if result.passed else "OTA PHYSICAL EVIDENCE FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- release_source_sha={result.release_source_sha}")
        print(f"- scenarios_seen={','.join(result.scenarios_seen)}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
