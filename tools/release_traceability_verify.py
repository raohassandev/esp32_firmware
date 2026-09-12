#!/usr/bin/env python3
"""Validate the final release evidence index for issue #91.

This tool does not create release evidence and never promotes partial/CI-only
results into a physical PASS. It validates one immutable release identity and
its required evidence bindings. Missing, stale, contradictory, cross-image, or
externally mismatched evidence fails closed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")

REQUIRED_RELEASE_LANES = (
    "industrial_ui",
    "generator_transition",
    "site_commissioning",
    "inverter_profiles",
    "ota",
    "fat_sat",
)

FINAL_EXACT_LANES = {
    "generator_transition",
    "site_commissioning",
    "ota",
    "fat_sat",
}

VALID_BINDING_MODES = {"exact", "governed_replay"}
EXECUTED_RECORD_STATUS = "EXECUTED_FINAL_RELEASE_EVIDENCE"


@dataclass
class ReleaseTraceabilityResult:
    passed: bool
    release_sha: str
    release_tree: str
    artifact_digest: str
    required_lanes: list[str]
    failures: list[str]


def _text(value: object, minimum: int = 1) -> bool:
    return len(str(value or "").strip()) >= minimum


def _sha40(value: object) -> bool:
    return bool(SHA40.fullmatch(str(value or "").strip().lower()))


def _sha256(value: object) -> bool:
    return bool(SHA256.fullmatch(str(value or "").strip().lower()))


def _dict(value: object) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _check_identity(record: dict[str, Any], failures: list[str]) -> tuple[str, str, str]:
    release = _dict(record.get("release"))
    release_sha = str(release.get("source_sha", "")).strip().lower()
    release_tree = str(release.get("tree_sha", "")).strip().lower()
    artifact_digest = str(release.get("artifact_digest", "")).strip().lower()

    if not _sha40(release_sha):
        failures.append("release:source_sha_invalid")
    if not _sha40(release_tree):
        failures.append("release:tree_sha_invalid")
    if not _text(release.get("artifact_name"), 4):
        failures.append("release:artifact_name_missing")
    if not _sha256(artifact_digest):
        failures.append("release:artifact_digest_invalid")
    if not _sha256(release.get("application_digest")):
        failures.append("release:application_digest_invalid")
    if not _sha256(release.get("config_digest")):
        failures.append("release:config_digest_invalid")
    if not _sha256(release.get("site_map_digest")):
        failures.append("release:site_map_digest_invalid")
    if not _sha256(release.get("profile_manifest_digest")):
        failures.append("release:profile_manifest_digest_invalid")
    if not _text(release.get("build_container"), 4):
        failures.append("release:build_container_missing")
    if release.get("rollback_enabled") is not True:
        failures.append("release:rollback_not_enabled")
    if release.get("compiled_sta_credentials_absent") is not True:
        failures.append("release:compiled_sta_credentials_absence_not_proven")
    if release.get("compiled_engineering_prefill_absent") is not True:
        failures.append("release:engineering_prefill_absence_not_proven")
    if release.get("bench_auth_bypasses_disabled") is not True:
        failures.append("release:bench_auth_bypasses_not_disabled")
    return release_sha, release_tree, artifact_digest


def _check_expected_identity(
    release_sha: str,
    release_tree: str,
    artifact_digest: str,
    expected_release_sha: str,
    expected_release_tree: str,
    expected_artifact_digest: str,
    failures: list[str],
) -> None:
    expected_sha = str(expected_release_sha or "").strip().lower()
    expected_tree = str(expected_release_tree or "").strip().lower()
    expected_artifact = str(expected_artifact_digest or "").strip().lower()

    if not _sha40(expected_sha):
        failures.append("expected:release_sha_invalid")
    elif release_sha != expected_sha:
        failures.append("release:source_sha_expected_mismatch")

    if not _sha40(expected_tree):
        failures.append("expected:release_tree_invalid")
    elif release_tree != expected_tree:
        failures.append("release:tree_sha_expected_mismatch")

    if not _sha256(expected_artifact):
        failures.append("expected:artifact_digest_invalid")
    elif artifact_digest != expected_artifact:
        failures.append("release:artifact_digest_expected_mismatch")


def _check_lane(
    lane_id: str,
    lane: dict[str, Any],
    release_sha: str,
    artifact_digest: str,
    failures: list[str],
) -> None:
    prefix = f"lane:{lane_id}"
    if lane.get("status") != "PASS":
        failures.append(f"{prefix}:status_not_pass")

    source_sha = str(lane.get("source_sha", "")).strip().lower()
    if not _sha40(source_sha):
        failures.append(f"{prefix}:source_sha_invalid")

    if not _text(lane.get("evidence_ref"), 4):
        failures.append(f"{prefix}:evidence_ref_missing")
    if not _sha256(lane.get("evidence_digest")):
        failures.append(f"{prefix}:evidence_digest_invalid")
    if lane.get("physical_or_authorized_evidence") is not True:
        failures.append(f"{prefix}:physical_or_authorized_evidence_not_true")
    if lane.get("validator_only") is not False:
        failures.append(f"{prefix}:validator_only_must_be_false")

    binding = _dict(lane.get("final_release_binding"))
    mode = str(binding.get("mode", "")).strip()
    if mode not in VALID_BINDING_MODES:
        failures.append(f"{prefix}:binding_mode_invalid")
        return

    if lane_id in FINAL_EXACT_LANES and mode != "exact":
        failures.append(f"{prefix}:final_lane_must_bind_exact")

    if mode == "exact":
        if source_sha != release_sha:
            failures.append(f"{prefix}:exact_source_sha_mismatch")
        bound_artifact = str(binding.get("artifact_digest", "")).strip().lower()
        if bound_artifact != artifact_digest:
            failures.append(f"{prefix}:exact_artifact_digest_mismatch")
    else:
        if lane_id != "industrial_ui":
            failures.append(f"{prefix}:governed_replay_only_allowed_for_ui")
        if binding.get("behavior_affecting_changes") is not False:
            failures.append(f"{prefix}:governed_replay_behavior_change_not_false")
        if not _text(binding.get("replay_ref"), 4):
            failures.append(f"{prefix}:governed_replay_ref_missing")
        if not _text(binding.get("approval_ref"), 4):
            failures.append(f"{prefix}:governed_replay_approval_ref_missing")
        if binding.get("final_release_checks_passed") is not True:
            failures.append(f"{prefix}:governed_replay_final_checks_not_passed")


def _check_inverter_profiles(
    lane: dict[str, Any], record: dict[str, Any], failures: list[str]
) -> None:
    prefix = "lane:inverter_profiles"
    profiles = lane.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        failures.append(f"{prefix}:profiles_missing")
        return

    seen: set[str] = set()
    for index, item in enumerate(profiles):
        if not isinstance(item, dict):
            failures.append(f"{prefix}:profile_invalid:{index}")
            continue
        pid = str(item.get("profile_id", "")).strip()
        pfx = f"{prefix}:{pid or index}"
        if not pid:
            failures.append(f"{pfx}:profile_id_missing")
            continue
        if pid in seen:
            failures.append(f"{pfx}:duplicate_profile")
        seen.add(pid)
        for key in ("manufacturer", "model", "firmware", "manual_revision", "manual_ref"):
            if not _text(item.get(key), 2):
                failures.append(f"{pfx}:{key}_missing")
        if item.get("production_approved") is not True:
            failures.append(f"{pfx}:production_approved_not_true")
        if item.get("bench_read_write_rollback_passed") is not True:
            failures.append(f"{pfx}:bench_read_write_rollback_not_passed")
        if not _text(item.get("signed_approval_ref"), 4):
            failures.append(f"{pfx}:signed_approval_ref_missing")
        if not _sha256(item.get("approval_digest")):
            failures.append(f"{pfx}:approval_digest_invalid")

    release = _dict(record.get("release"))
    lane_manifest = str(lane.get("profile_manifest_digest", "")).strip().lower()
    release_manifest = str(release.get("profile_manifest_digest", "")).strip().lower()
    if lane_manifest != release_manifest:
        failures.append(f"{prefix}:profile_manifest_digest_mismatch")


def _check_site_binding(record: dict[str, Any], failures: list[str]) -> None:
    release = _dict(record.get("release"))
    site = _dict(record.get("site"))
    if not _text(site.get("site_id"), 2):
        failures.append("site:site_id_missing")
    if not _text(site.get("configuration_identity"), 4):
        failures.append("site:configuration_identity_missing")
    if str(site.get("site_map_digest", "")).strip().lower() != str(
        release.get("site_map_digest", "")
    ).strip().lower():
        failures.append("site:site_map_digest_mismatch")
    if str(site.get("config_digest", "")).strip().lower() != str(
        release.get("config_digest", "")
    ).strip().lower():
        failures.append("site:config_digest_mismatch")
    if site.get("power_sign_used_as_source_authority") is not False:
        failures.append("site:power_sign_source_authority_not_forbidden")


def _check_optional_reva(record: dict[str, Any], failures: list[str]) -> None:
    scope = _dict(record.get("scope"))
    reva_required = scope.get("rev_a_hardware_coupled")
    if not isinstance(reva_required, bool):
        failures.append("scope:rev_a_hardware_coupled_missing")
        return
    reva = _dict(record.get("rev_a_hardware"))
    if not reva_required:
        if reva and reva.get("status") == "PASS":
            failures.append("rev_a_hardware:uncoupled_release_must_not_claim_pass")
        return

    if reva.get("status") != "PASS":
        failures.append("rev_a_hardware:status_not_pass")
    if reva.get("fabricator_dfm_accepted") is not True:
        failures.append("rev_a_hardware:fabricator_dfm_not_accepted")
    if reva.get("h4_physical_passed") is not True:
        failures.append("rev_a_hardware:h4_physical_not_passed")
    if not _sha40(reva.get("h2_checkpoint_sha")):
        failures.append("rev_a_hardware:h2_checkpoint_sha_invalid")
    if not _sha256(reva.get("provider_package_digest")):
        failures.append("rev_a_hardware:provider_package_digest_invalid")
    if not _text(reva.get("h4_evidence_ref"), 4):
        failures.append("rev_a_hardware:h4_evidence_ref_missing")
    if not _sha256(reva.get("h4_evidence_digest")):
        failures.append("rev_a_hardware:h4_evidence_digest_invalid")


def evaluate(
    record: dict[str, Any],
    expected_release_sha: str,
    expected_release_tree: str,
    expected_artifact_digest: str,
) -> ReleaseTraceabilityResult:
    failures: list[str] = []

    if record.get("record_status") != EXECUTED_RECORD_STATUS:
        failures.append("record_status_not_executed")

    release_sha, release_tree, artifact_digest = _check_identity(record, failures)
    _check_expected_identity(
        release_sha,
        release_tree,
        artifact_digest,
        expected_release_sha,
        expected_release_tree,
        expected_artifact_digest,
        failures,
    )
    _check_site_binding(record, failures)

    scope = _dict(record.get("scope"))
    required_lanes: list[str] = []
    for lane_id in REQUIRED_RELEASE_LANES:
        value = scope.get(lane_id)
        if value is not True:
            failures.append(f"scope:{lane_id}_must_be_true")
        else:
            required_lanes.append(lane_id)

    evidence = _dict(record.get("evidence"))
    for lane_id in REQUIRED_RELEASE_LANES:
        lane = evidence.get(lane_id)
        if not isinstance(lane, dict):
            failures.append(f"lane:{lane_id}:missing")
            continue
        _check_lane(lane_id, lane, release_sha, artifact_digest, failures)
        if lane_id == "inverter_profiles":
            _check_inverter_profiles(lane, record, failures)

    _check_optional_reva(record, failures)

    signoff = _dict(record.get("release_signoff"))
    if signoff.get("zero_critical_blockers") is not True:
        failures.append("signoff:zero_critical_blockers_not_true")
    if signoff.get("fat_sat_signed") is not True:
        failures.append("signoff:fat_sat_signed_not_true")
    if not _text(signoff.get("authorized_representative"), 3):
        failures.append("signoff:authorized_representative_missing")
    if not _text(signoff.get("signed_sat_ref"), 4):
        failures.append("signoff:signed_sat_ref_missing")
    if not _sha256(signoff.get("signed_sat_digest")):
        failures.append("signoff:signed_sat_digest_invalid")
    if not _text(signoff.get("release_evidence_package_ref"), 4):
        failures.append("signoff:release_evidence_package_ref_missing")
    if not _sha256(signoff.get("release_evidence_package_digest")):
        failures.append("signoff:release_evidence_package_digest_invalid")
    if signoff.get("unexecuted_template") is not False:
        failures.append("signoff:unexecuted_template_must_be_false")

    return ReleaseTraceabilityResult(
        passed=not failures,
        release_sha=release_sha,
        release_tree=release_tree,
        artifact_digest=artifact_digest,
        required_lanes=required_lanes,
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate final release traceability evidence")
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--expected-release-sha", required=True)
    parser.add_argument("--expected-release-tree", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        record = json.loads(args.manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"RELEASE TRACEABILITY FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("RELEASE TRACEABILITY FAIL: JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        expected_release_sha=args.expected_release_sha,
        expected_release_tree=args.expected_release_tree,
        expected_artifact_digest=args.expected_artifact_digest,
    )
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("RELEASE TRACEABILITY PASS" if result.passed else "RELEASE TRACEABILITY FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- release_sha={result.release_sha}")
        print(f"- release_tree={result.release_tree}")
        print(f"- artifact_digest={result.artifact_digest}")
        print(f"- required_lanes={','.join(result.required_lanes)}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
