#!/usr/bin/env python3
"""Validate recorded Waveshare persistence/ARM physical evidence.

This tool does not perform saves, reboots, failure injection, or ARM actions.
It validates the evidence record after those operations are physically executed
on the exact already-soak-qualified candidate. Missing/false evidence fails.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


REQUIRED_CHECKS = (
    "save_readback_match",
    "reboot_restore_match",
    "repeated_persistence_stable",
    "interrupted_save_fail_closed",
    "reentry_authoritative",
    "arm_refuses_incomplete",
    "arm_only_after_qualified",
    "interfaces_responsive",
    "no_fatal_or_resource_collapse",
    "nvs_erase_absent",
    "full_flash_erase_absent",
)

REQUIRED_OPERATIONS = {
    "save_readback",
    "reboot_restore",
    "interrupted_save",
    "arm_gate",
}

REQUIRED_REFERENCES = (
    "serial_log_ref",
    "persistence_capture_ref",
)


@dataclass
class PersistenceArmResult:
    passed: bool
    candidate_sha: str
    artifact_digest: str
    operation_count: int
    operations_seen: list[str]
    checks: dict[str, bool]
    failures: list[str]


def evaluate(
    record: dict,
    expected_candidate_sha: str,
    expected_artifact_digest: str,
) -> PersistenceArmResult:
    failures: list[str] = []
    candidate_sha = str(record.get("candidate_sha", "")).strip()
    artifact_digest = str(record.get("artifact_digest", "")).strip().lower()

    if candidate_sha != expected_candidate_sha:
        failures.append("candidate_sha_mismatch")
    if artifact_digest != expected_artifact_digest.strip().lower():
        failures.append("artifact_digest_mismatch")
    if record.get("final_soak_passed") is not True:
        failures.append("final_soak_not_passed")

    raw_checks = record.get("checks")
    checks = raw_checks if isinstance(raw_checks, dict) else {}
    raw_evidence = record.get("evidence")
    evidence = raw_evidence if isinstance(raw_evidence, dict) else {}
    checked: dict[str, bool] = {}
    for key in REQUIRED_CHECKS:
        value = checks.get(key) is True
        checked[key] = value
        if not value:
            failures.append(f"check_not_passed:{key}")
        note = str(evidence.get(key, "")).strip()
        if len(note) < 8:
            failures.append(f"evidence_missing:{key}")

    raw_steps = record.get("steps")
    steps = raw_steps if isinstance(raw_steps, list) else []
    operations_seen: set[str] = set()
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            failures.append(f"step_invalid:{index}")
            continue
        operation = str(step.get("operation", "")).strip()
        if operation:
            operations_seen.add(operation)
        else:
            failures.append(f"step_operation_missing:{index}")
        if not str(step.get("observed_at", "")).strip():
            failures.append(f"step_timestamp_missing:{index}")
        if "before" not in step:
            failures.append(f"step_before_missing:{index}")
        if "after" not in step:
            failures.append(f"step_after_missing:{index}")

    for operation in sorted(REQUIRED_OPERATIONS - operations_seen):
        failures.append(f"operation_missing:{operation}")

    raw_refs = record.get("references")
    refs = raw_refs if isinstance(raw_refs, dict) else {}
    for key in REQUIRED_REFERENCES:
        if not str(refs.get(key, "")).strip():
            failures.append(f"reference_missing:{key}")

    return PersistenceArmResult(
        passed=not failures,
        candidate_sha=candidate_sha,
        artifact_digest=artifact_digest,
        operation_count=len(steps),
        operations_seen=sorted(operations_seen),
        checks=checked,
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate post-soak Waveshare persistence/ARM physical evidence"
    )
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-candidate-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"PERSISTENCE/ARM FAIL: evidence JSON unreadable: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("PERSISTENCE/ARM FAIL: evidence JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        expected_candidate_sha=args.expected_candidate_sha,
        expected_artifact_digest=args.expected_artifact_digest,
    )

    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("WAVESHARE PERSISTENCE/ARM PASS" if result.passed else "WAVESHARE PERSISTENCE/ARM FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- candidate_sha={result.candidate_sha}")
        print(f"- operations_seen={','.join(result.operations_seen)}")

    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
