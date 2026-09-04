#!/usr/bin/env python3
"""Validate exact-image Industrial UI Waveshare physical acceptance evidence.

This tool cannot observe the LCD or touchscreen. It combines the existing
serial/resource soak validator with explicit human/bench observations for the
new Industrial UI workflow. CI can validate evidence completeness; it cannot
create physical PASS.
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from waveshare_final_acceptance import evaluate as evaluate_waveshare


REQUIRED_VISUAL_FLAGS = (
    "overview_layout_ok",
    "grid_layout_ok",
    "solar_layout_ok",
    "alarms_layout_ok",
    "readiness_layout_ok",
    "engineering_hierarchy_ok",
    "commissioning_layout_ok",
    "service_layout_ok",
    "dark_theme_readable",
    "light_theme_readable",
)

REQUIRED_TOUCH_FLAGS = (
    "menu_touch_ok",
    "route_navigation_touch_ok",
    "alarm_control_touch_ok",
    "operator_protected_routes_blocked",
    "engineering_routes_reachable",
    "summary_shortcuts_correct",
    "alarm_interaction_ok",
)

REQUIRED_RUNTIME_FLAGS = (
    "unrelated_api_responsive",
    "history_events_responsive",
    "browser_lockout_absent",
)

# Keep the original display/touch physical observations in the evidence record
# too; the new product acceptance extends rather than weakens that gate.
BASE_PHYSICAL_FLAGS = {
    "alarms_opened": True,
    "touch_responsive": True,
    "sweep_absent": True,
    "reload_absent": True,
    "tear_or_corruption_absent": True,
}


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
    base_waveshare: dict
    failures: list[str]


def _required_string(observations: dict, key: str, expected: str, failures: list[str]) -> str:
    value = str(observations.get(key, "")).strip().lower()
    wanted = str(expected).strip().lower()
    if not value:
        failures.append(f"missing_identity:{key}")
    elif value != wanted:
        failures.append(f"identity_mismatch:{key}")
    return value


def _flags(observations: dict, keys: tuple[str, ...], prefix: str, failures: list[str]) -> dict[str, bool]:
    values: dict[str, bool] = {}
    for key in keys:
        passed = observations.get(key) is True
        values[key] = passed
        if not passed:
            failures.append(f"{prefix}_not_passed:{key}")
    return values


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

    candidate_sha = _required_string(observations, "candidate_sha", expected_candidate_sha, failures)
    tree_sha = _required_string(observations, "tree_sha", expected_tree_sha, failures)
    artifact_digest = _required_string(observations, "artifact_digest", expected_artifact_digest, failures)
    application_sha256 = _required_string(
        observations, "application_sha256", expected_application_sha256, failures
    )

    visual = _flags(observations, REQUIRED_VISUAL_FLAGS, "visual", failures)
    touch = _flags(observations, REQUIRED_TOUCH_FLAGS, "touch", failures)
    runtime = _flags(observations, REQUIRED_RUNTIME_FLAGS, "runtime", failures)

    # Reuse the established Waveshare serial/soak gate. The five common physical
    # flags are intentionally required again here so a product-UX PASS cannot
    # bypass display stability or basic touch evidence.
    base_observations = dict(observations)
    for key in BASE_PHYSICAL_FLAGS:
        base_observations.setdefault(key, False)

    base = evaluate_waveshare(
        serial_text,
        base_observations,
        expected_candidate_sha=expected_candidate_sha,
        expected_artifact_digest=expected_artifact_digest,
        min_page_cycles=min_page_cycles,
        min_runtime_seconds=min_runtime_seconds,
        min_soak_samples=min_soak_samples,
        min_dma_free=min_dma_free,
    )
    if not base.passed:
        failures.extend(f"waveshare:{item}" for item in base.failures)

    # Physical acceptance needs explicit evidence that both access roles were
    # exercised on this exact image, not merely inferred from source contracts.
    roles = observations.get("roles_exercised")
    if not isinstance(roles, list) or {str(item).lower() for item in roles} != {"operator", "engineering"}:
        failures.append("roles_exercised_must_equal_operator_and_engineering")

    routes = observations.get("routes_exercised")
    required_routes = {
        "dashboard", "meters", "inverters", "alarms", "readiness",
        "engineering", "commissioning", "system",
    }
    route_set = {str(item).lower() for item in routes} if isinstance(routes, list) else set()
    missing_routes = sorted(required_routes - route_set)
    if missing_routes:
        failures.append("routes_missing:" + ",".join(missing_routes))

    return IndustrialUiResult(
        passed=not failures,
        candidate_sha=candidate_sha,
        tree_sha=tree_sha,
        artifact_digest=artifact_digest,
        application_sha256=application_sha256,
        visual=visual,
        touch=touch,
        runtime=runtime,
        base_waveshare=asdict(base),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate Industrial UI v1 exact-image Waveshare physical acceptance evidence"
    )
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
    except (OSError, json.JSONDecodeError) as exc:
        print(f"INDUSTRIAL UI ACCEPTANCE FAIL: observations JSON unreadable: {exc}", file=sys.stderr)
        return 2
    if not isinstance(observations, dict):
        print("INDUSTRIAL UI ACCEPTANCE FAIL: observations JSON must be an object", file=sys.stderr)
        return 2

    try:
        serial_text = args.serial_log.read_text(errors="replace")
    except OSError as exc:
        print(f"INDUSTRIAL UI ACCEPTANCE FAIL: serial log unreadable: {exc}", file=sys.stderr)
        return 2

    result = evaluate(
        serial_text,
        observations,
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
        print(f"- candidate_sha={result.candidate_sha}")
        print(f"- tree_sha={result.tree_sha}")
        print(f"- page_cycles={result.base_waveshare.get('page_cycles')}")
        serial = result.base_waveshare.get("serial", {})
        print(f"- observed_runtime_seconds={serial.get('observed_runtime_seconds')}")
        print(f"- soak_samples={serial.get('soak_samples')}")
        print(f"- minimum_checked_dma_free={serial.get('minimum_checked_dma_free')}")

    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
