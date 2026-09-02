#!/usr/bin/env python3
"""Combine exact-image serial evidence with explicit physical observations.

This tool does NOT observe the LCD or touch by itself. A human records the
physical observations in a small JSON file while the exact flashed candidate is
running. The tool independently validates the captured serial/resource log and
only returns PASS when both evidence classes satisfy the release contract.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from waveshare_acceptance_check import analyse


@dataclass
class FinalResult:
    passed: bool
    candidate_sha: str
    artifact_digest: str
    page_cycles: int
    physical: dict[str, bool]
    serial: dict
    failures: list[str]


REQUIRED_PHYSICAL_FLAGS = (
    "alarms_opened",
    "touch_responsive",
    "sweep_absent",
    "reload_absent",
    "tear_or_corruption_absent",
)


def evaluate(
    serial_text: str,
    observations: dict,
    expected_candidate_sha: str,
    expected_artifact_digest: str,
    min_page_cycles: int = 20,
    min_runtime_seconds: int = 14_400,
    min_soak_samples: int = 240,
    min_dma_free: int = 20_000,
) -> FinalResult:
    failures: list[str] = []

    candidate_sha = str(observations.get("candidate_sha", "")).strip()
    artifact_digest = str(observations.get("artifact_digest", "")).strip().lower()
    expected_digest = expected_artifact_digest.strip().lower()
    if candidate_sha != expected_candidate_sha:
        failures.append("candidate_sha_mismatch")
    if artifact_digest != expected_digest:
        failures.append("artifact_digest_mismatch")

    try:
        page_cycles = int(observations.get("page_cycles", 0))
    except (TypeError, ValueError):
        page_cycles = 0
    if page_cycles < min_page_cycles:
        failures.append(f"page_cycles={page_cycles}<{min_page_cycles}")

    physical: dict[str, bool] = {}
    for key in REQUIRED_PHYSICAL_FLAGS:
        value = observations.get(key) is True
        physical[key] = value
        if not value:
            failures.append(f"physical_not_passed:{key}")

    serial_result = analyse(
        serial_text,
        min_dma_free=min_dma_free,
        min_soak_samples=min_soak_samples,
        min_runtime_seconds=min_runtime_seconds,
        min_dma_largest=0,
    )
    if not serial_result.passed:
        failures.extend(f"serial:{item}" for item in serial_result.failures)

    return FinalResult(
        passed=not failures,
        candidate_sha=candidate_sha,
        artifact_digest=artifact_digest,
        page_cycles=page_cycles,
        physical=physical,
        serial=asdict(serial_result),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate final Waveshare same-image physical + serial acceptance evidence"
    )
    parser.add_argument("serial_log", type=Path)
    parser.add_argument("observations_json", type=Path)
    parser.add_argument("--expected-candidate-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--min-page-cycles", type=int, default=20)
    parser.add_argument("--min-runtime-seconds", type=int, default=14_400)
    parser.add_argument("--min-soak-samples", type=int, default=240,
                        help="Default assumes the current one-minute Screen-soak cadence for >=4 h")
    parser.add_argument("--min-dma-free", type=int, default=20_000)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        observations = json.loads(args.observations_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"FINAL ACCEPTANCE FAIL: observations JSON unreadable: {exc}", file=sys.stderr)
        return 2
    if not isinstance(observations, dict):
        print("FINAL ACCEPTANCE FAIL: observations JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        args.serial_log.read_text(errors="replace"),
        observations,
        expected_candidate_sha=args.expected_candidate_sha,
        expected_artifact_digest=args.expected_artifact_digest,
        min_page_cycles=args.min_page_cycles,
        min_runtime_seconds=args.min_runtime_seconds,
        min_soak_samples=args.min_soak_samples,
        min_dma_free=args.min_dma_free,
    )

    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("FINAL WAVESHARE ACCEPTANCE PASS" if result.passed else "FINAL WAVESHARE ACCEPTANCE FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- candidate_sha={result.candidate_sha}")
        print(f"- page_cycles={result.page_cycles}")
        print(f"- observed_runtime_seconds={result.serial.get('observed_runtime_seconds')}")
        print(f"- soak_samples={result.serial.get('soak_samples')}")
        print(f"- minimum_checked_dma_free={result.serial.get('minimum_checked_dma_free')}")

    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
