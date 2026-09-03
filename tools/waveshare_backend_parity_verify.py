#!/usr/bin/env python3
"""Validate recorded Waveshare backend parity/recovery physical evidence.

This tool does not observe hardware. It only validates a completed evidence
record produced after the exact candidate has already passed the uninterrupted
final soak. False/missing observations fail closed.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path


REQUIRED_CHECKS = (
    "status_values_consistent",
    "meter_values_consistent",
    "inverter_values_consistent",
    "alarm_event_consistent",
    "state_recovery_truthful",
    "network_recovery_clean",
    "hmi_responsive",
    "control_not_starved",
    "fail_closed_on_invalid",
    "no_fatal_or_resource_collapse",
)

REQUIRED_REFERENCES = (
    "serial_log_ref",
    "hmi_http_capture_ref",
)


@dataclass
class BackendParityResult:
    passed: bool
    candidate_sha: str
    artifact_digest: str
    observation_minutes: int
    checks: dict[str, bool]
    failures: list[str]


def _parse_timestamp(value: object) -> datetime | None:
    text = str(value or "").strip()
    if not text:
        return None
    try:
        return datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None


def evaluate(
    record: dict,
    expected_candidate_sha: str,
    expected_artifact_digest: str,
    min_observation_minutes: int = 30,
) -> BackendParityResult:
    failures: list[str] = []
    candidate_sha = str(record.get("candidate_sha", "")).strip()
    artifact_digest = str(record.get("artifact_digest", "")).strip().lower()

    if candidate_sha != expected_candidate_sha:
        failures.append("candidate_sha_mismatch")
    if artifact_digest != expected_artifact_digest.strip().lower():
        failures.append("artifact_digest_mismatch")
    if record.get("final_soak_passed") is not True:
        failures.append("final_soak_not_passed")

    try:
        observation_minutes = int(record.get("observation_minutes", 0))
    except (TypeError, ValueError):
        observation_minutes = 0
    if observation_minutes < min_observation_minutes:
        failures.append(
            f"observation_minutes={observation_minutes}<{min_observation_minutes}"
        )

    started = _parse_timestamp(record.get("started_at"))
    ended = _parse_timestamp(record.get("ended_at"))
    if started is None:
        failures.append("started_at_invalid")
    if ended is None:
        failures.append("ended_at_invalid")
    if started is not None and ended is not None:
        if ended <= started:
            failures.append("observation_time_not_increasing")
        elif (ended - started).total_seconds() < observation_minutes * 60:
            failures.append("timestamp_span_shorter_than_observation_minutes")

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

    raw_refs = record.get("references")
    refs = raw_refs if isinstance(raw_refs, dict) else {}
    for key in REQUIRED_REFERENCES:
        if not str(refs.get(key, "")).strip():
            failures.append(f"reference_missing:{key}")

    return BackendParityResult(
        passed=not failures,
        candidate_sha=candidate_sha,
        artifact_digest=artifact_digest,
        observation_minutes=observation_minutes,
        checks=checked,
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate post-soak Waveshare backend parity/recovery evidence"
    )
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-candidate-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--min-observation-minutes", type=int, default=30)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"BACKEND PARITY FAIL: evidence JSON unreadable: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("BACKEND PARITY FAIL: evidence JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        expected_candidate_sha=args.expected_candidate_sha,
        expected_artifact_digest=args.expected_artifact_digest,
        min_observation_minutes=args.min_observation_minutes,
    )

    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("WAVESHARE BACKEND PARITY PASS" if result.passed else "WAVESHARE BACKEND PARITY FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- candidate_sha={result.candidate_sha}")
        print(f"- observation_minutes={result.observation_minutes}")

    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
