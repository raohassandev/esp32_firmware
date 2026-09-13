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
    "inverter_profiles",
    "ota",
    "fat_sat",
}

VALID_BINDING_MODES = {"exact", "governed_replay"}
EXECUTED_RECORD_STATUS = "EXECUTED_FINAL_RELEASE_EVIDENCE"

RELEASE_DIGEST_FIELDS = (
    "artifact_digest",
    "application_digest",
    "config_digest",
    "site_map_digest",
    "profile_manifest_digest",
)


@dataclass
class ReleaseTraceabilityResult:
    passed: bool
    release_sha: str
    release_tree: str
    artifact_digest: str
    application_digest: str
    config_digest: str
    site_map_digest: str
    profile_manifest_digest: str
    site_id: str
    configuration_identity: str
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


def _norm(value: object) -> str:
    return str(value or "").strip().lower()


def _release_digests(record: dict[str, Any]) -> dict[str, str]:
    release = _dict(record.get("release"))
    return {field: _norm(release.get(field)) for field in RELEASE_DIGEST_FIELDS}


def _check_identity(
    record: dict[str, Any],
    failures: list[str],
) -> tuple[str, str, dict[str, str]]:
    release = _dict(record.get("release"))
    release_sha = _norm(release.get("source_sha"))
    release_tree = _norm(release.get("tree_sha"))
    digests = _release_digests(record)

    if not _sha40(release_sha):
        failures.append("release:source_sha_invalid")
    if not _sha40(release_tree):
        failures.append("release:tree_sha_invalid")
    if not _text(release.get("artifact_name"), 4):
        failures.append("release:artifact_name_missing")
    for field, value in digests.items():
        if not _sha256(value):
            failures.append(f"release:{field}_invalid")
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
    return release_sha, release_tree, digests


def _check_expected_identity(
    record: dict[str, Any],
    release_sha: str,
    release_tree: str,
    digests: dict[str, str],
    expected_release_sha: str,
    expected_release_tree: str,
    expected_digests: dict[str, str],
    expected_site_id: str,
    expected_configuration_identity: str,
    failures: list[str],
) -> None:
    expected_sha = _norm(expected_release_sha)
    expected_tree = _norm(expected_release_tree)

    if not _sha40(expected_sha):
        failures.append("expected:release_sha_invalid")
    elif release_sha != expected_sha:
        failures.append("release:source_sha_expected_mismatch")

    if not _sha40(expected_tree):
        failures.append("expected:release_tree_invalid")
    elif release_tree != expected_tree:
        failures.append("release:tree_sha_expected_mismatch")

    for field in RELEASE_DIGEST_FIELDS:
        expected = _norm(expected_digests.get(field))
        if not _sha256(expected):
            failures.append(f"expected:{field}_invalid")
        elif digests.get(field) != expected:
            failures.append(f"release:{field}_expected_mismatch")

    site = _dict(record.get("site"))
    site_id = str(site.get("site_id", "")).strip()
    config_identity = str(site.get("configuration_identity", "")).strip()
    if not _text(expected_site_id, 2):
        failures.append("expected:site_id_invalid")
    elif site_id != str(expected_site_id).strip():
        failures.append("site:site_id_expected_mismatch")
    if not _text(expected_configuration_identity, 4):
        failures.append("expected:configuration_identity_invalid")
    elif config_identity != str(expected_configuration_identity).strip():
        failures.append("site:configuration_identity_expected_mismatch")


def _identity_payload(
    release_sha: str,
    release_tree: str,
    digests: dict[str, str],
) -> dict[str, str]:
    payload = {
        "source_sha": release_sha,
        "tree_sha": release_tree,
    }
    payload.update(digests)
    return payload


def _check_identity_payload(
    payload: dict[str, Any],
    expected: dict[str, str],
    prefix: str,
    failures: list[str],
) -> None:
    for key, expected_value in expected.items():
        actual = _norm(payload.get(key))
        if key in {"source_sha", "tree_sha"}:
            if not _sha40(actual):
                failures.append(f"{prefix}:{key}_invalid")
            elif actual != expected_value:
                failures.append(f"{prefix}:{key}_mismatch")
        else:
            if not _sha256(actual):
                failures.append(f"{prefix}:{key}_invalid")
            elif actual != expected_value:
                failures.append(f"{prefix}:{key}_mismatch")


def _check_lane(
    lane_id: str,
    lane: dict[str, Any],
    release_sha: str,
    release_tree: str,
    digests: dict[str, str],
    expected_evidence_digest: str,
    failures: list[str],
) -> None:
    prefix = f"lane:{lane_id}"
    if lane.get("status") != "PASS":
        failures.append(f"{prefix}:status_not_pass")

    source_sha = _norm(lane.get("source_sha"))
    if not _sha40(source_sha):
        failures.append(f"{prefix}:source_sha_invalid")

    if not _text(lane.get("evidence_ref"), 4):
        failures.append(f"{prefix}:evidence_ref_missing")
    evidence_digest = _norm(lane.get("evidence_digest"))
    if not _sha256(evidence_digest):
        failures.append(f"{prefix}:evidence_digest_invalid")

    expected_lane_digest = _norm(expected_evidence_digest)
    if not _sha256(expected_lane_digest):
        failures.append(f"expected:{lane_id}_evidence_digest_invalid")
    elif evidence_digest != expected_lane_digest:
        failures.append(f"{prefix}:evidence_digest_expected_mismatch")

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

    full_identity = _identity_payload(release_sha, release_tree, digests)
    if mode == "exact":
        if source_sha != release_sha:
            failures.append(f"{prefix}:exact_source_sha_mismatch")
        _check_identity_payload(
            binding,
            full_identity,
            f"{prefix}:exact_binding",
            failures,
        )
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
        replay_identity = _dict(binding.get("final_release_identity"))
        _check_identity_payload(
            replay_identity,
            full_identity,
            f"{prefix}:governed_replay_identity",
            failures,
        )


def _check_inverter_profiles(
    lane: dict[str, Any],
    record: dict[str, Any],
    failures: list[str],
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
        if not _sha256(item.get("manual_digest")):
            failures.append(f"{pfx}:manual_digest_invalid")
        if item.get("production_approved") is not True:
            failures.append(f"{pfx}:production_approved_not_true")
        if item.get("bench_read_write_rollback_passed") is not True:
            failures.append(f"{pfx}:bench_read_write_rollback_not_passed")
        if not _text(item.get("signed_approval_ref"), 4):
            failures.append(f"{pfx}:signed_approval_ref_missing")
        if not _sha256(item.get("approval_digest")):
            failures.append(f"{pfx}:approval_digest_invalid")
        if not _sha256(item.get("qualification_evidence_digest")):
            failures.append(f"{pfx}:qualification_evidence_digest_invalid")

    release = _dict(record.get("release"))
    lane_manifest = _norm(lane.get("profile_manifest_digest"))
    release_manifest = _norm(release.get("profile_manifest_digest"))
    if lane_manifest != release_manifest:
        failures.append(f"{prefix}:profile_manifest_digest_mismatch")


def _check_site_binding(
    record: dict[str, Any],
    failures: list[str],
) -> tuple[str, str]:
    release = _dict(record.get("release"))
    site = _dict(record.get("site"))
    site_id = str(site.get("site_id", "")).strip()
    configuration_identity = str(site.get("configuration_identity", "")).strip()

    if not _text(site_id, 2):
        failures.append("site:site_id_missing")
    if not _text(configuration_identity, 4):
        failures.append("site:configuration_identity_missing")
    if _norm(site.get("site_map_digest")) != _norm(release.get("site_map_digest")):
        failures.append("site:site_map_digest_mismatch")
    if _norm(site.get("config_digest")) != _norm(release.get("config_digest")):
        failures.append("site:config_digest_mismatch")
    if site.get("power_sign_used_as_source_authority") is not False:
        failures.append("site:power_sign_source_authority_not_forbidden")
    return site_id, configuration_identity


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


def _check_signoff_binding(
    signoff: dict[str, Any],
    release_sha: str,
    release_tree: str,
    digests: dict[str, str],
    expected_signed_sat_digest: str,
    failures: list[str],
) -> None:
    if signoff.get("zero_critical_blockers") is not True:
        failures.append("signoff:zero_critical_blockers_not_true")
    if signoff.get("fat_sat_signed") is not True:
        failures.append("signoff:fat_sat_signed_not_true")
    if not _text(signoff.get("authorized_representative"), 3):
        failures.append("signoff:authorized_representative_missing")
    if not _text(signoff.get("signed_sat_ref"), 4):
        failures.append("signoff:signed_sat_ref_missing")
    signed_sat_digest = _norm(signoff.get("signed_sat_digest"))
    if not _sha256(signed_sat_digest):
        failures.append("signoff:signed_sat_digest_invalid")
    expected_sat = _norm(expected_signed_sat_digest)
    if not _sha256(expected_sat):
        failures.append("expected:signed_sat_digest_invalid")
    elif signed_sat_digest != expected_sat:
        failures.append("signoff:signed_sat_digest_expected_mismatch")
    if not _text(signoff.get("release_evidence_package_ref"), 4):
        failures.append("signoff:release_evidence_package_ref_missing")
    if not _sha256(signoff.get("release_evidence_package_digest")):
        failures.append("signoff:release_evidence_package_digest_invalid")
    if signoff.get("unexecuted_template") is not False:
        failures.append("signoff:unexecuted_template_must_be_false")

    signed_identity = _dict(signoff.get("signed_release_identity"))
    _check_identity_payload(
        signed_identity,
        _identity_payload(release_sha, release_tree, digests),
        "signoff:signed_release_identity",
        failures,
    )


def evaluate(
    record: dict[str, Any],
    expected_release_sha: str,
    expected_release_tree: str,
    expected_artifact_digest: str,
    expected_application_digest: str,
    expected_config_digest: str,
    expected_site_map_digest: str,
    expected_profile_manifest_digest: str,
    expected_site_id: str,
    expected_configuration_identity: str,
    expected_lane_evidence_digests: dict[str, str],
    expected_signed_sat_digest: str,
) -> ReleaseTraceabilityResult:
    failures: list[str] = []

    if record.get("record_status") != EXECUTED_RECORD_STATUS:
        failures.append("record_status_not_executed")

    release_sha, release_tree, digests = _check_identity(record, failures)
    site_id, configuration_identity = _check_site_binding(record, failures)

    _check_expected_identity(
        record,
        release_sha,
        release_tree,
        digests,
        expected_release_sha,
        expected_release_tree,
        {
            "artifact_digest": expected_artifact_digest,
            "application_digest": expected_application_digest,
            "config_digest": expected_config_digest,
            "site_map_digest": expected_site_map_digest,
            "profile_manifest_digest": expected_profile_manifest_digest,
        },
        expected_site_id,
        expected_configuration_identity,
        failures,
    )

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
        _check_lane(
            lane_id,
            lane,
            release_sha,
            release_tree,
            digests,
            expected_lane_evidence_digests.get(lane_id, ""),
            failures,
        )
        if lane_id == "inverter_profiles":
            _check_inverter_profiles(lane, record, failures)

    _check_optional_reva(record, failures)

    signoff = _dict(record.get("release_signoff"))
    _check_signoff_binding(
        signoff,
        release_sha,
        release_tree,
        digests,
        expected_signed_sat_digest,
        failures,
    )

    return ReleaseTraceabilityResult(
        passed=not failures,
        release_sha=release_sha,
        release_tree=release_tree,
        artifact_digest=digests.get("artifact_digest", ""),
        application_digest=digests.get("application_digest", ""),
        config_digest=digests.get("config_digest", ""),
        site_map_digest=digests.get("site_map_digest", ""),
        profile_manifest_digest=digests.get("profile_manifest_digest", ""),
        site_id=site_id,
        configuration_identity=configuration_identity,
        required_lanes=required_lanes,
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate final release traceability evidence")
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--expected-release-sha", required=True)
    parser.add_argument("--expected-release-tree", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--expected-application-digest", required=True)
    parser.add_argument("--expected-config-digest", required=True)
    parser.add_argument("--expected-site-map-digest", required=True)
    parser.add_argument("--expected-profile-manifest-digest", required=True)
    parser.add_argument("--expected-site-id", required=True)
    parser.add_argument("--expected-configuration-identity", required=True)
    for lane_id in REQUIRED_RELEASE_LANES:
        parser.add_argument(
            f"--expected-{lane_id.replace('_', '-')}-evidence-digest",
            required=True,
        )
    parser.add_argument("--expected-signed-sat-digest", required=True)
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

    lane_digests = {
        lane_id: getattr(args, f"expected_{lane_id}_evidence_digest")
        for lane_id in REQUIRED_RELEASE_LANES
    }
    result = evaluate(
        record,
        expected_release_sha=args.expected_release_sha,
        expected_release_tree=args.expected_release_tree,
        expected_artifact_digest=args.expected_artifact_digest,
        expected_application_digest=args.expected_application_digest,
        expected_config_digest=args.expected_config_digest,
        expected_site_map_digest=args.expected_site_map_digest,
        expected_profile_manifest_digest=args.expected_profile_manifest_digest,
        expected_site_id=args.expected_site_id,
        expected_configuration_identity=args.expected_configuration_identity,
        expected_lane_evidence_digests=lane_digests,
        expected_signed_sat_digest=args.expected_signed_sat_digest,
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
        print(f"- application_digest={result.application_digest}")
        print(f"- config_digest={result.config_digest}")
        print(f"- site_map_digest={result.site_map_digest}")
        print(f"- profile_manifest_digest={result.profile_manifest_digest}")
        print(f"- site_id={result.site_id}")
        print(f"- configuration_identity={result.configuration_identity}")
        print(f"- required_lanes={','.join(result.required_lanes)}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
