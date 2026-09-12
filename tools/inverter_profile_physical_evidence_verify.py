#!/usr/bin/env python3
"""Fail-closed manufacturer/model inverter qualification evidence validator.

This validator checks an evidence record; it never discovers register maps,
performs a field write, or upgrades a profile by inference. Production approval
requires one externally frozen manufacturer/model/inverter-firmware/manual/
profile/controller/endpoint identity plus physical read/write/readback/failure/
rollback evidence and a signed approval record.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from datetime import date, datetime
from pathlib import Path
from typing import Any

STAGES = {"documented": 1, "read_only_qualified": 2, "write_qualified": 3, "production_approved": 4}
STAGE_EVIDENCE_STATE = {
    "documented": "DOCUMENTED_MANUFACTURER_EVIDENCE",
    "read_only_qualified": "EXECUTED_PHYSICAL_READ_ONLY_EVIDENCE",
    "write_qualified": "EXECUTED_PHYSICAL_WRITE_EVIDENCE",
    "production_approved": "EXECUTED_PRODUCTION_APPROVAL_EVIDENCE",
}
TOPOLOGIES = {"direct_tcp", "direct_rtu", "rtu_gateway", "manufacturer_logger"}
SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^(?:sha256:)?[0-9a-f]{64}$")


@dataclass
class Result:
    passed: bool
    stage: str
    manufacturer: str
    model: str
    inverter_firmware: str
    profile_source_sha: str
    failures: list[str]


def text(v: object, n: int = 1) -> bool:
    return len(str(v or "").strip()) >= n


def finite(v: object) -> bool:
    if isinstance(v, bool):
        return False
    try:
        return math.isfinite(float(v))
    except (TypeError, ValueError):
        return False


def aware_time(v: object) -> datetime | None:
    try:
        d = datetime.fromisoformat(str(v or "").replace("Z", "+00:00"))
    except ValueError:
        return None
    return d if d.tzinfo is not None else None


def valid_date(v: object) -> bool:
    try:
        date.fromisoformat(str(v or "").strip())
    except ValueError:
        return False
    return True


def exact_sha(v: object) -> bool:
    return bool(SHA40.fullmatch(str(v or "").strip().lower()))


def digest(v: object) -> bool:
    return bool(SHA256.fullmatch(str(v or "").strip().lower()))


def normalized_digest(v: object) -> str:
    raw = str(v or "").strip().lower()
    return raw if raw.startswith("sha256:") else f"sha256:{raw}"


def req(obj: dict[str, Any], keys: tuple[str, ...], prefix: str, failures: list[str], minimum: int = 1) -> None:
    for key in keys:
        if not text(obj.get(key), minimum):
            failures.append(f"{prefix}:{key}_missing")


def check_expected_identity(rec: dict[str, Any], expected: dict[str, Any], failures: list[str]) -> None:
    comparisons = (
        ("manufacturer", str(rec.get("manufacturer", "")).strip(), str(expected.get("manufacturer", "")).strip()),
        ("model", str(rec.get("model", "")).strip(), str(expected.get("model", "")).strip()),
        ("inverter_firmware", str(rec.get("inverter_firmware", "")).strip(), str(expected.get("inverter_firmware", "")).strip()),
        ("profile_source_sha", str(rec.get("profile_source_sha", "")).strip().lower(), str(expected.get("profile_source_sha", "")).strip().lower()),
        ("controller_firmware_sha", str(rec.get("controller_firmware_sha", "")).strip().lower(), str(expected.get("controller_firmware_sha", "")).strip().lower()),
    )
    for key, actual, wanted in comparisons:
        if actual != wanted:
            failures.append(f"expected_identity:{key}_mismatch")

    if normalized_digest(rec.get("controller_artifact_digest")) != normalized_digest(expected.get("controller_artifact_digest")):
        failures.append("expected_identity:controller_artifact_digest_mismatch")

    manual = rec.get("manual") if isinstance(rec.get("manual"), dict) else {}
    if str(manual.get("revision", "")).strip() != str(expected.get("manual_revision", "")).strip():
        failures.append("expected_identity:manual_revision_mismatch")
    if normalized_digest(manual.get("document_sha256")) != normalized_digest(expected.get("manual_document_sha256")):
        failures.append("expected_identity:manual_document_sha256_mismatch")

    connection = rec.get("connection") if isinstance(rec.get("connection"), dict) else {}
    if str(connection.get("topology", "")).strip() != str(expected.get("topology", "")).strip():
        failures.append("expected_identity:topology_mismatch")
    if str(connection.get("endpoint", "")).strip() != str(expected.get("endpoint", "")).strip():
        failures.append("expected_identity:endpoint_mismatch")
    if connection.get("unit_id") != expected.get("unit_id"):
        failures.append("expected_identity:unit_id_mismatch")


def check_manual(rec: dict[str, Any], manufacturer: str, model: str, inv_fw: str, failures: list[str]) -> tuple[str, str]:
    m = rec.get("manual")
    if not isinstance(m, dict):
        failures.append("manual_missing")
        return "", ""
    req(m, ("title", "revision", "publication_date", "evidence_ref"), "manual", failures, 3)
    if not valid_date(m.get("publication_date")):
        failures.append("manual:publication_date_invalid")
    if m.get("official_manufacturer_source") is not True:
        failures.append("manual:not_official_manufacturer_source")
    if str(m.get("manufacturer", "")).strip().casefold() != manufacturer.casefold():
        failures.append("manual:manufacturer_mismatch")
    scope = m.get("model_scope")
    if not isinstance(scope, list) or model not in [str(x).strip() for x in scope]:
        failures.append("manual:exact_model_not_in_scope")
    if not digest(m.get("document_sha256")):
        failures.append("manual:document_sha256_invalid")
    fw_scope = m.get("firmware_scope")
    if not isinstance(fw_scope, dict):
        failures.append("manual:firmware_scope_missing")
    else:
        if fw_scope.get("applicability_confirmed") is not True:
            failures.append("manual:firmware_applicability_not_confirmed")
        if not text(fw_scope.get("basis"), 8):
            failures.append("manual:firmware_applicability_basis_missing")
        if str(fw_scope.get("exact_inverter_firmware", "")).strip() != inv_fw:
            failures.append("manual:exact_inverter_firmware_mismatch")
    return str(m.get("revision", "")).strip(), normalized_digest(m.get("document_sha256"))


def check_connection(rec: dict[str, Any], failures: list[str]) -> None:
    c = rec.get("connection")
    if not isinstance(c, dict):
        failures.append("connection_missing")
        return
    if c.get("topology") not in TOPOLOGIES:
        failures.append("connection:topology_invalid")
    req(c, ("endpoint", "identity_probe_ref"), "connection", failures, 3)
    unit = c.get("unit_id")
    if not isinstance(unit, int) or isinstance(unit, bool) or unit < 0 or unit > 247:
        failures.append("connection:unit_id_invalid")


def check_map(
    entry: object,
    prefix: str,
    failures: list[str],
    manual_revision: str,
    manual_digest: str,
    writable: bool = False,
) -> None:
    if not isinstance(entry, dict):
        failures.append(f"{prefix}:mapping_missing")
        return
    req(entry, ("address", "data_type", "word_order", "units", "manual_ref", "manual_revision", "manual_document_sha256"), prefix, failures, 1)
    if not finite(entry.get("scale")):
        failures.append(f"{prefix}:scale_invalid")
    if entry.get("manual_backed") is not True:
        failures.append(f"{prefix}:manual_backed_not_true")
    if str(entry.get("manual_revision", "")).strip() != manual_revision:
        failures.append(f"{prefix}:manual_revision_mismatch")
    if normalized_digest(entry.get("manual_document_sha256")) != manual_digest:
        failures.append(f"{prefix}:manual_document_sha256_mismatch")
    if writable:
        fc = entry.get("function_code")
        if fc not in {6, 16}:
            failures.append(f"{prefix}:function_code_invalid")
        if not finite(entry.get("raw_min")) or not finite(entry.get("raw_max")):
            failures.append(f"{prefix}:raw_range_invalid")
        elif float(entry["raw_min"]) >= float(entry["raw_max"]):
            failures.append(f"{prefix}:raw_range_not_increasing")
        req(entry, ("enable_semantics", "disable_semantics"), prefix, failures, 3)


def check_read_only(
    rec: dict[str, Any], failures: list[str], manual_revision: str, manual_digest: str
) -> tuple[datetime | None, datetime | None]:
    ro = rec.get("physical_read_only")
    if not isinstance(ro, dict):
        failures.append("physical_read_only_missing")
        return None, None
    if ro.get("performed") is not True or ro.get("pass") is not True:
        failures.append("physical_read_only_not_pass")
    req(
        ro,
        (
            "started_at",
            "ended_at",
            "identity_raw",
            "identity_decoded",
            "telemetry_evidence_ref",
            "status_evidence_ref",
            "evidence_package_ref",
        ),
        "physical_read_only",
        failures,
        2,
    )
    if not digest(ro.get("evidence_package_digest")):
        failures.append("physical_read_only:evidence_package_digest_invalid")
    start = aware_time(ro.get("started_at"))
    end = aware_time(ro.get("ended_at"))
    if start is None or end is None or end <= start:
        failures.append("physical_read_only:timestamps_invalid")
    if ro.get("identity_matches_exact_model_firmware") is not True:
        failures.append("physical_read_only:identity_mismatch")
    if ro.get("status_register_physically_correlated") is not True:
        failures.append("physical_read_only:status_not_physically_correlated")
    if ro.get("write_attempted") is not False:
        failures.append("physical_read_only:write_must_not_be_attempted")

    maps = rec.get("register_map")
    if not isinstance(maps, dict):
        failures.append("register_map_missing")
        return start, end
    for key in ("identity", "active_power", "status", "fault"):
        check_map(maps.get(key), f"register_map:{key}", failures, manual_revision, manual_digest)
    telemetry = maps.get("telemetry")
    if not isinstance(telemetry, list) or not telemetry:
        failures.append("register_map:telemetry_missing")
    else:
        for i, item in enumerate(telemetry):
            check_map(item, f"register_map:telemetry:{i}", failures, manual_revision, manual_digest)
    return start, end


def check_write(
    rec: dict[str, Any], failures: list[str], manual_revision: str, manual_digest: str
) -> tuple[datetime | None, datetime | None]:
    maps = rec.get("register_map")
    readback_tolerance: float | None = None
    if not isinstance(maps, dict):
        failures.append("register_map_missing_for_write")
    else:
        check_map(maps.get("command"), "register_map:command", failures, manual_revision, manual_digest, writable=True)
        check_map(maps.get("readback"), "register_map:readback", failures, manual_revision, manual_digest)
        rb = maps.get("readback") if isinstance(maps.get("readback"), dict) else {}
        if not finite(rb.get("tolerance")) or float(rb.get("tolerance", -1)) < 0:
            failures.append("register_map:readback:tolerance_invalid")
        else:
            readback_tolerance = float(rb["tolerance"])

    w = rec.get("physical_write")
    if not isinstance(w, dict):
        failures.append("physical_write_missing")
        return None, None
    if w.get("performed") is not True or w.get("pass") is not True:
        failures.append("physical_write_not_pass")
    req(
        w,
        (
            "started_at",
            "ended_at",
            "evidence_ref",
            "safe_start_state",
            "command_transmitted_ref",
            "readback_ref",
            "safe_zero_evidence_ref",
            "timeout_evidence_ref",
            "exception_evidence_ref",
        ),
        "physical_write",
        failures,
        2,
    )
    if not digest(w.get("evidence_digest")):
        failures.append("physical_write:evidence_digest_invalid")
    start = aware_time(w.get("started_at"))
    end = aware_time(w.get("ended_at"))
    if start is None or end is None or end <= start:
        failures.append("physical_write:timestamps_invalid")

    for key in (
        "requested_engineering_value",
        "requested_raw_value",
        "observed_readback_engineering_value",
        "observed_readback_raw_value",
    ):
        if not finite(w.get(key)):
            failures.append(f"physical_write:{key}_invalid")

    if readback_tolerance is not None and finite(w.get("requested_engineering_value")) and finite(w.get("observed_readback_engineering_value")):
        error = abs(float(w["observed_readback_engineering_value"]) - float(w["requested_engineering_value"]))
        if error > readback_tolerance:
            failures.append("physical_write:measured_readback_outside_tolerance")

    for key in ("readback_within_tolerance", "observed_response_matches_command", "safe_zero_proven", "timeout_fail_safe_proven", "exception_fail_safe_proven"):
        if w.get(key) is not True:
            failures.append(f"physical_write:{key}_not_true")
    if w.get("automatic_control_enabled_during_test") is not False:
        failures.append("physical_write:automatic_control_must_be_disabled")

    rollback = w.get("rollback")
    if not isinstance(rollback, dict):
        failures.append("physical_write:rollback_missing")
    else:
        for key in ("performed", "original_value_restored", "readback_match", "failure_path_exercised", "safe_fallback_observed"):
            if rollback.get(key) is not True:
                failures.append(f"physical_write:rollback:{key}_not_true")
        req(rollback, ("evidence_ref",), "physical_write:rollback", failures, 3)
        if not digest(rollback.get("evidence_digest")):
            failures.append("physical_write:rollback:evidence_digest_invalid")
        if not finite(rollback.get("original_engineering_value")) or not finite(rollback.get("restored_engineering_value")):
            failures.append("physical_write:rollback:measured_values_invalid")
        elif readback_tolerance is not None:
            restore_error = abs(float(rollback["restored_engineering_value"]) - float(rollback["original_engineering_value"]))
            if restore_error > readback_tolerance:
                failures.append("physical_write:rollback:measured_restore_outside_tolerance")

    reconnect = w.get("reconnect")
    if not isinstance(reconnect, dict):
        failures.append("physical_write:reconnect_missing")
    else:
        if reconnect.get("identity_revalidated_before_write_authority") is not True:
            failures.append("physical_write:reconnect_identity_not_revalidated")
        if reconnect.get("stale_identity_blocks_write") is not True:
            failures.append("physical_write:stale_identity_did_not_block_write")
        req(reconnect, ("evidence_ref",), "physical_write:reconnect", failures, 3)
        if not digest(reconnect.get("evidence_digest")):
            failures.append("physical_write:reconnect:evidence_digest_invalid")
    return start, end


def check_approval(
    rec: dict[str, Any], failures: list[str], write_end: datetime | None
) -> datetime | None:
    a = rec.get("production_approval")
    if not isinstance(a, dict):
        failures.append("production_approval_missing")
        return None
    if a.get("approved") is not True:
        failures.append("production_approval:not_approved")
    req(
        a,
        (
            "approver",
            "role",
            "approved_at",
            "approval_record_ref",
            "manual_identity",
            "bench_evidence_identity",
            "manufacturer",
            "model",
            "inverter_firmware",
            "profile_source_sha",
            "controller_firmware_sha",
            "controller_artifact_digest",
            "manual_revision",
            "manual_document_sha256",
            "topology",
            "endpoint",
        ),
        "production_approval",
        failures,
        2,
    )
    if not digest(a.get("approval_record_digest")):
        failures.append("production_approval:approval_record_digest_invalid")
    approved_at = aware_time(a.get("approved_at"))
    if approved_at is None:
        failures.append("production_approval:approved_at_invalid")
    elif write_end is not None and approved_at <= write_end:
        failures.append("production_approval:approved_before_write_evidence_completed")
    if a.get("signature_present") is not True:
        failures.append("production_approval:signature_missing")
    if a.get("profile_source_sha_confirmed") is not True:
        failures.append("production_approval:profile_sha_not_confirmed")
    if a.get("firmware_build_sha_confirmed") is not True:
        failures.append("production_approval:firmware_sha_not_confirmed")
    if a.get("manual_revision_confirmed") is not True:
        failures.append("production_approval:manual_revision_not_confirmed")
    if a.get("write_permission_authorized") is not True:
        failures.append("production_approval:write_permission_not_authorized")
    if a.get("no_identity_or_mapping_change_after_bench") is not True:
        failures.append("production_approval:post_bench_identity_or_mapping_change_not_forbidden")

    manual = rec.get("manual") if isinstance(rec.get("manual"), dict) else {}
    connection = rec.get("connection") if isinstance(rec.get("connection"), dict) else {}
    identity_checks = (
        ("manufacturer", a.get("manufacturer"), rec.get("manufacturer")),
        ("model", a.get("model"), rec.get("model")),
        ("inverter_firmware", a.get("inverter_firmware"), rec.get("inverter_firmware")),
        ("profile_source_sha", str(a.get("profile_source_sha", "")).lower(), str(rec.get("profile_source_sha", "")).lower()),
        ("controller_firmware_sha", str(a.get("controller_firmware_sha", "")).lower(), str(rec.get("controller_firmware_sha", "")).lower()),
        ("manual_revision", a.get("manual_revision"), manual.get("revision")),
        ("topology", a.get("topology"), connection.get("topology")),
        ("endpoint", a.get("endpoint"), connection.get("endpoint")),
        ("unit_id", a.get("unit_id"), connection.get("unit_id")),
    )
    for key, actual, wanted in identity_checks:
        if actual != wanted:
            failures.append(f"production_approval:{key}_mismatch")
    if normalized_digest(a.get("controller_artifact_digest")) != normalized_digest(rec.get("controller_artifact_digest")):
        failures.append("production_approval:controller_artifact_digest_mismatch")
    if normalized_digest(a.get("manual_document_sha256")) != normalized_digest(manual.get("document_sha256")):
        failures.append("production_approval:manual_document_sha256_mismatch")
    return approved_at


def evaluate(rec: dict[str, Any], expected_stage: str | None = None, expected_identity: dict[str, Any] | None = None) -> Result:
    failures: list[str] = []
    stage = str(rec.get("stage", "")).strip()
    manufacturer = str(rec.get("manufacturer", "")).strip()
    model = str(rec.get("model", "")).strip()
    inv_fw = str(rec.get("inverter_firmware", "")).strip()
    profile_sha = str(rec.get("profile_source_sha", "")).strip().lower()

    if stage not in STAGES:
        failures.append("stage_invalid")
        level = 0
    else:
        level = STAGES[stage]
        if rec.get("evidence_state") != STAGE_EVIDENCE_STATE[stage]:
            failures.append("evidence_state_mismatch")
    if expected_stage and stage != expected_stage:
        failures.append("stage_mismatch")
    for key, value in (("manufacturer", manufacturer), ("model", model), ("inverter_firmware", inv_fw)):
        if not text(value, 2):
            failures.append(f"{key}_missing")
    if not exact_sha(profile_sha):
        failures.append("profile_source_sha_invalid")
    if not exact_sha(rec.get("controller_firmware_sha")):
        failures.append("controller_firmware_sha_invalid")
    if not digest(rec.get("controller_artifact_digest")):
        failures.append("controller_artifact_digest_invalid")
    if rec.get("third_party_or_guessed_map_used") is not False:
        failures.append("third_party_or_guessed_map_not_forbidden")
    if rec.get("automatic_production_write_allowed_before_approval") is not False:
        failures.append("preapproval_automatic_write_not_forbidden")

    manual_revision, manual_digest = check_manual(rec, manufacturer, model, inv_fw, failures)
    check_connection(rec, failures)

    if expected_identity is None:
        failures.append("expected_identity_missing")
    else:
        check_expected_identity(rec, expected_identity, failures)

    ro_start = ro_end = write_start = write_end = None
    if level >= 2:
        ro_start, ro_end = check_read_only(rec, failures, manual_revision, manual_digest)
    if level >= 3:
        write_start, write_end = check_write(rec, failures, manual_revision, manual_digest)
        if ro_end is not None and write_start is not None and write_start < ro_end:
            failures.append("physical_write:started_before_read_only_evidence_completed")
    if level >= 4:
        check_approval(rec, failures, write_end)

    return Result(not failures, stage, manufacturer, model, inv_fw, profile_sha, failures)


def main() -> int:
    p = argparse.ArgumentParser(description="Validate manufacturer/model inverter physical qualification evidence")
    p.add_argument("evidence_json", type=Path)
    p.add_argument("--expected-stage", choices=tuple(STAGES), required=True)
    p.add_argument("--expected-manufacturer", required=True)
    p.add_argument("--expected-model", required=True)
    p.add_argument("--expected-inverter-firmware", required=True)
    p.add_argument("--expected-profile-sha", required=True)
    p.add_argument("--expected-controller-sha", required=True)
    p.add_argument("--expected-controller-artifact-digest", required=True)
    p.add_argument("--expected-manual-revision", required=True)
    p.add_argument("--expected-manual-digest", required=True)
    p.add_argument("--expected-topology", choices=tuple(sorted(TOPOLOGIES)), required=True)
    p.add_argument("--expected-endpoint", required=True)
    p.add_argument("--expected-unit-id", type=int, required=True)
    p.add_argument("--json", action="store_true")
    args = p.parse_args()

    for label, value, checker in (
        ("expected_profile_sha", args.expected_profile_sha, exact_sha),
        ("expected_controller_sha", args.expected_controller_sha, exact_sha),
        ("expected_controller_artifact_digest", args.expected_controller_artifact_digest, digest),
        ("expected_manual_digest", args.expected_manual_digest, digest),
    ):
        if not checker(value):
            print(f"INVERTER PROFILE EVIDENCE FAIL: {label} invalid", file=sys.stderr)
            return 2
    if args.expected_unit_id < 0 or args.expected_unit_id > 247:
        print("INVERTER PROFILE EVIDENCE FAIL: expected_unit_id invalid", file=sys.stderr)
        return 2

    try:
        rec = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"INVERTER PROFILE EVIDENCE FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(rec, dict):
        print("INVERTER PROFILE EVIDENCE FAIL: JSON root must be an object", file=sys.stderr)
        return 2

    expected_identity = {
        "manufacturer": args.expected_manufacturer,
        "model": args.expected_model,
        "inverter_firmware": args.expected_inverter_firmware,
        "profile_source_sha": args.expected_profile_sha,
        "controller_firmware_sha": args.expected_controller_sha,
        "controller_artifact_digest": args.expected_controller_artifact_digest,
        "manual_revision": args.expected_manual_revision,
        "manual_document_sha256": args.expected_manual_digest,
        "topology": args.expected_topology,
        "endpoint": args.expected_endpoint,
        "unit_id": args.expected_unit_id,
    }
    result = evaluate(rec, args.expected_stage, expected_identity)
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("INVERTER PROFILE EVIDENCE PASS" if result.passed else "INVERTER PROFILE EVIDENCE FAIL")
        print(f"stage={result.stage} manufacturer={result.manufacturer} model={result.model} inverter_firmware={result.inverter_firmware}")
        for failure in result.failures:
            print(f"- {failure}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
