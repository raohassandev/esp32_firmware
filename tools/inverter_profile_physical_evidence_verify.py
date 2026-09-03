#!/usr/bin/env python3
"""Fail-closed manufacturer/model inverter qualification evidence validator.

This validator checks an evidence record; it never discovers register maps,
performs a field write, or upgrades a profile by inference. Production approval
requires exact manual/profile/firmware identity plus physical read/write/
readback/failure/rollback evidence and a signed approval record.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

STAGES = {"documented": 1, "read_only_qualified": 2, "write_qualified": 3, "production_approved": 4}
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


def aware_time(v: object) -> bool:
    try:
        d = datetime.fromisoformat(str(v or "").replace("Z", "+00:00"))
    except ValueError:
        return False
    return d.tzinfo is not None


def exact_sha(v: object) -> bool:
    return bool(SHA40.fullmatch(str(v or "").strip().lower()))


def digest(v: object) -> bool:
    return bool(SHA256.fullmatch(str(v or "").strip().lower()))


def req(obj: dict[str, Any], keys: tuple[str, ...], prefix: str, failures: list[str], minimum: int = 1) -> None:
    for key in keys:
        if not text(obj.get(key), minimum):
            failures.append(f"{prefix}:{key}_missing")


def check_manual(rec: dict[str, Any], manufacturer: str, model: str, failures: list[str]) -> None:
    m = rec.get("manual")
    if not isinstance(m, dict):
        failures.append("manual_missing")
        return
    req(m, ("title", "revision", "publication_date", "evidence_ref"), "manual", failures, 3)
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


def check_connection(rec: dict[str, Any], failures: list[str]) -> None:
    c = rec.get("connection")
    if not isinstance(c, dict):
        failures.append("connection_missing")
        return
    if c.get("topology") not in {"direct_tcp", "direct_rtu", "rtu_gateway", "manufacturer_logger"}:
        failures.append("connection:topology_invalid")
    req(c, ("endpoint", "identity_probe_ref"), "connection", failures, 3)
    unit = c.get("unit_id")
    if not isinstance(unit, int) or isinstance(unit, bool) or unit < 0 or unit > 247:
        failures.append("connection:unit_id_invalid")


def check_map(entry: object, prefix: str, failures: list[str], writable: bool = False) -> None:
    if not isinstance(entry, dict):
        failures.append(f"{prefix}:mapping_missing")
        return
    req(entry, ("address", "data_type", "word_order", "units", "manual_ref"), prefix, failures, 1)
    if not finite(entry.get("scale")):
        failures.append(f"{prefix}:scale_invalid")
    if entry.get("manual_backed") is not True:
        failures.append(f"{prefix}:manual_backed_not_true")
    if writable:
        fc = entry.get("function_code")
        if fc not in {6, 16}:
            failures.append(f"{prefix}:function_code_invalid")
        if not finite(entry.get("raw_min")) or not finite(entry.get("raw_max")):
            failures.append(f"{prefix}:raw_range_invalid")
        elif float(entry["raw_min"]) >= float(entry["raw_max"]):
            failures.append(f"{prefix}:raw_range_not_increasing")
        req(entry, ("enable_semantics", "disable_semantics"), prefix, failures, 3)


def check_read_only(rec: dict[str, Any], failures: list[str]) -> None:
    ro = rec.get("physical_read_only")
    if not isinstance(ro, dict):
        failures.append("physical_read_only_missing")
        return
    if ro.get("performed") is not True or ro.get("pass") is not True:
        failures.append("physical_read_only_not_pass")
    req(ro, ("started_at", "ended_at", "identity_raw", "identity_decoded", "telemetry_evidence_ref", "status_evidence_ref"), "physical_read_only", failures, 2)
    if not aware_time(ro.get("started_at")) or not aware_time(ro.get("ended_at")):
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
        return
    for key in ("identity", "active_power", "status", "fault"):
        check_map(maps.get(key), f"register_map:{key}", failures)
    telemetry = maps.get("telemetry")
    if not isinstance(telemetry, list) or not telemetry:
        failures.append("register_map:telemetry_missing")
    else:
        for i, item in enumerate(telemetry):
            check_map(item, f"register_map:telemetry:{i}", failures)


def check_write(rec: dict[str, Any], failures: list[str]) -> None:
    maps = rec.get("register_map")
    if not isinstance(maps, dict):
        failures.append("register_map_missing_for_write")
    else:
        check_map(maps.get("command"), "register_map:command", failures, writable=True)
        check_map(maps.get("readback"), "register_map:readback", failures)
        rb = maps.get("readback") if isinstance(maps.get("readback"), dict) else {}
        if not finite(rb.get("tolerance")) or float(rb.get("tolerance", -1)) < 0:
            failures.append("register_map:readback:tolerance_invalid")

    w = rec.get("physical_write")
    if not isinstance(w, dict):
        failures.append("physical_write_missing")
        return
    if w.get("performed") is not True or w.get("pass") is not True:
        failures.append("physical_write_not_pass")
    req(w, ("evidence_ref", "safe_start_state", "requested_engineering_value", "requested_raw_value"), "physical_write", failures, 1)
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

    reconnect = w.get("reconnect")
    if not isinstance(reconnect, dict):
        failures.append("physical_write:reconnect_missing")
    else:
        if reconnect.get("identity_revalidated_before_write_authority") is not True:
            failures.append("physical_write:reconnect_identity_not_revalidated")
        if reconnect.get("stale_identity_blocks_write") is not True:
            failures.append("physical_write:stale_identity_did_not_block_write")
        req(reconnect, ("evidence_ref",), "physical_write:reconnect", failures, 3)


def check_approval(rec: dict[str, Any], failures: list[str]) -> None:
    a = rec.get("production_approval")
    if not isinstance(a, dict):
        failures.append("production_approval_missing")
        return
    if a.get("approved") is not True:
        failures.append("production_approval:not_approved")
    req(a, ("approver", "role", "approved_at", "approval_record_ref", "manual_identity", "bench_evidence_identity"), "production_approval", failures, 3)
    if not aware_time(a.get("approved_at")):
        failures.append("production_approval:approved_at_invalid")
    if a.get("profile_source_sha_confirmed") is not True:
        failures.append("production_approval:profile_sha_not_confirmed")
    if a.get("firmware_build_sha_confirmed") is not True:
        failures.append("production_approval:firmware_sha_not_confirmed")
    if a.get("manual_revision_confirmed") is not True:
        failures.append("production_approval:manual_revision_not_confirmed")
    if a.get("write_permission_authorized") is not True:
        failures.append("production_approval:write_permission_not_authorized")


def evaluate(rec: dict[str, Any], expected_stage: str | None = None) -> Result:
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

    check_manual(rec, manufacturer, model, failures)
    check_connection(rec, failures)

    if level >= 2:
        check_read_only(rec, failures)
    if level >= 3:
        check_write(rec, failures)
    if level >= 4:
        check_approval(rec, failures)

    return Result(not failures, stage, manufacturer, model, inv_fw, profile_sha, failures)


def main() -> int:
    p = argparse.ArgumentParser(description="Validate manufacturer/model inverter physical qualification evidence")
    p.add_argument("evidence_json", type=Path)
    p.add_argument("--expected-stage", choices=tuple(STAGES))
    p.add_argument("--json", action="store_true")
    args = p.parse_args()
    try:
        rec = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"INVERTER PROFILE EVIDENCE FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(rec, dict):
        print("INVERTER PROFILE EVIDENCE FAIL: JSON root must be an object", file=sys.stderr)
        return 2
    result = evaluate(rec, args.expected_stage)
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
