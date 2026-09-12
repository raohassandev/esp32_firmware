#!/usr/bin/env python3
"""Fail-closed validator for generator/source-transition physical evidence.

This tool never controls a generator, breaker, ATS, inverter or site. It only
validates evidence captured by an authorized physical executor. A record must be
bound to one independently frozen firmware/artifact/site/config/topology/source-
map/meter-map identity. Missing, copied, ambiguous or unsafe evidence fails
closed. Power sign may corroborate electrical direction but may never be the
sole source/breaker/synchronism authority.
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

EVIDENCE_STATE = "EXECUTED_GENERATOR_TRANSITION_PHYSICAL_EVIDENCE"
SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^(?:sha256:)?[0-9a-f]{64}$")

REQUIRED_SCENARIOS = (
    "grid_to_generator",
    "generator_to_grid",
    "island",
    "synchronized",
    "stale",
    "conflict",
    "transfer_asserted",
    "source_loss",
    "generator_meter_sign",
    "recovery_dwell",
)

TRANSITION_SCENARIOS = {
    "grid_to_generator",
    "generator_to_grid",
    "recovery_dwell",
}

BLOCKED_SCENARIOS = {
    "stale",
    "conflict",
    "transfer_asserted",
    "source_loss",
}

REQUIRED_FATAL_COUNTS = (
    "wdt",
    "panic",
    "no_mem",
    "unexpected_reset",
    "resource_collapse",
)

VALID_AUTHORITY = {"blocked", "allowed"}


@dataclass
class GeneratorPhysicalResult:
    passed: bool
    firmware_sha: str
    artifact_digest: str
    site_id: str
    config_identity: str
    topology_digest: str
    source_map_digest: str
    meter_map_digest: str
    scenarios_seen: list[str]
    failures: list[str]


def _timestamp(value: object) -> datetime | None:
    text = str(value or "").strip()
    if not text:
        return None
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo is not None else None


def _nonempty_text(value: object, minimum: int = 1) -> bool:
    return len(str(value or "").strip()) >= minimum


def _finite_number(value: object) -> bool:
    if isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def _sha40(value: object) -> bool:
    return bool(SHA40.fullmatch(str(value or "").strip().lower()))


def _digest(value: object) -> bool:
    return bool(SHA256.fullmatch(str(value or "").strip().lower()))


def _normalized_digest(value: object) -> str:
    raw = str(value or "").strip().lower()
    return raw if raw.startswith("sha256:") else f"sha256:{raw}"


def _authority_sequence(value: object) -> list[str] | None:
    if not isinstance(value, list) or not value:
        return None
    sequence = [str(item).strip() for item in value]
    return sequence if all(item in VALID_AUTHORITY for item in sequence) else None


def _check_exact_identity(
    obj: dict[str, Any],
    prefix: str,
    identity: dict[str, str],
    failures: list[str],
) -> None:
    checks = (
        ("firmware_sha", str(obj.get("firmware_sha", "")).strip().lower(), identity["firmware_sha"]),
        ("artifact_digest", _normalized_digest(obj.get("artifact_digest")), identity["artifact_digest"]),
        ("site_id", str(obj.get("site_id", "")).strip(), identity["site_id"]),
        ("config_identity", str(obj.get("config_identity", "")).strip(), identity["config_identity"]),
        ("topology_digest", _normalized_digest(obj.get("topology_digest")), identity["topology_digest"]),
        ("source_map_digest", _normalized_digest(obj.get("source_map_digest")), identity["source_map_digest"]),
        ("meter_map_digest", _normalized_digest(obj.get("meter_map_digest")), identity["meter_map_digest"]),
    )
    for key, actual, expected in checks:
        if actual != expected:
            failures.append(f"{prefix}:{key}_mismatch")


def _check_meter(value: object, prefix: str, failures: list[str]) -> None:
    if not isinstance(value, dict):
        failures.append(f"{prefix}:meter_evidence_missing")
        return
    for key in ("meter_id", "role", "ct_pt_polarity_ref", "data_type", "word_order", "sign_provenance"):
        if not _nonempty_text(value.get(key), 2):
            failures.append(f"{prefix}:meter_field_missing:{key}")
    if not isinstance(value.get("fresh"), bool):
        failures.append(f"{prefix}:meter_fresh_missing")
    for key in ("raw", "scale", "scaled_kw"):
        if not _finite_number(value.get(key)):
            failures.append(f"{prefix}:meter_{key}_invalid")
    if all(_finite_number(value.get(key)) for key in ("raw", "scale", "scaled_kw")):
        calculated = float(value["raw"]) * float(value["scale"])
        if not math.isclose(calculated, float(value["scaled_kw"]), rel_tol=1e-9, abs_tol=1e-9):
            failures.append(f"{prefix}:meter_raw_scale_mismatch")


def _check_recovery(
    scenario: dict[str, Any],
    prefix: str,
    scenario_start: datetime | None,
    scenario_end: datetime | None,
    failures: list[str],
) -> None:
    if scenario.get("authority_returned_early") is not False:
        failures.append(f"{prefix}:authority_returned_early_not_false")

    recovery_start = _timestamp(scenario.get("recovery_started_at"))
    recovery_end = _timestamp(scenario.get("recovery_ended_at"))
    if recovery_start is None or recovery_end is None or recovery_end <= recovery_start:
        failures.append(f"{prefix}:recovery_timestamps_invalid")
    elif scenario_start is not None and scenario_end is not None:
        if recovery_start < scenario_start or recovery_end > scenario_end:
            failures.append(f"{prefix}:recovery_timestamps_outside_scenario")

    dwell = scenario.get("recovery_dwell_ms")
    if not _finite_number(dwell) or float(dwell) <= 0:
        failures.append(f"{prefix}:recovery_dwell_missing")
    elif recovery_start is not None and recovery_end is not None and recovery_end > recovery_start:
        measured_ms = (recovery_end - recovery_start).total_seconds() * 1000.0
        if measured_ms + 1e-6 < float(dwell):
            failures.append(f"{prefix}:recovery_timestamp_duration_shorter_than_claimed_dwell")

    if not _nonempty_text(scenario.get("recovery_evidence_ref"), 4):
        failures.append(f"{prefix}:recovery_evidence_ref_missing")
    if not _digest(scenario.get("recovery_evidence_digest")):
        failures.append(f"{prefix}:recovery_evidence_digest_invalid")


def _check_scenario(
    scenario: dict[str, Any],
    supports_sync: bool,
    topology_ref: str,
    identity: dict[str, str],
    failures: list[str],
) -> None:
    scenario_id = str(scenario.get("id", "")).strip()
    prefix = f"scenario:{scenario_id or 'missing'}"
    outcome = str(scenario.get("outcome", "")).strip()

    _check_exact_identity(scenario, prefix, identity, failures)
    if not _nonempty_text(scenario.get("evidence_ref"), 4):
        failures.append(f"{prefix}:evidence_ref_missing")
    if not _digest(scenario.get("evidence_digest")):
        failures.append(f"{prefix}:evidence_digest_invalid")

    if scenario_id == "synchronized" and not supports_sync:
        if outcome != "not_supported":
            failures.append(f"{prefix}:must_be_not_supported")
        if not _nonempty_text(scenario.get("not_supported_reason"), 8):
            failures.append(f"{prefix}:not_supported_reason_missing")
        if str(scenario.get("topology_ref", "")).strip() != topology_ref:
            failures.append(f"{prefix}:topology_ref_mismatch")
        return

    if outcome != "pass":
        failures.append(f"{prefix}:outcome_not_pass")

    started = _timestamp(scenario.get("started_at"))
    ended = _timestamp(scenario.get("ended_at"))
    if started is None:
        failures.append(f"{prefix}:started_at_invalid")
    if ended is None:
        failures.append(f"{prefix}:ended_at_invalid")
    if started is not None and ended is not None and ended <= started:
        failures.append(f"{prefix}:time_not_increasing")

    raw_source = scenario.get("raw_source_evidence")
    if not isinstance(raw_source, dict) or not raw_source:
        failures.append(f"{prefix}:raw_source_evidence_missing")
    if not _nonempty_text(scenario.get("raw_source_evidence_ref"), 4):
        failures.append(f"{prefix}:raw_source_evidence_ref_missing")
    if not _digest(scenario.get("raw_source_evidence_digest")):
        failures.append(f"{prefix}:raw_source_evidence_digest_invalid")

    detected_modes = scenario.get("detected_modes")
    if not isinstance(detected_modes, list) or not detected_modes or not all(_nonempty_text(item) for item in detected_modes):
        failures.append(f"{prefix}:detected_modes_missing")

    expected = _authority_sequence(scenario.get("expected_authority_sequence"))
    observed = _authority_sequence(scenario.get("observed_authority_sequence"))
    if expected is None:
        failures.append(f"{prefix}:expected_authority_sequence_invalid")
        expected = []
    if observed is None:
        failures.append(f"{prefix}:observed_authority_sequence_invalid")
        observed = []
    if expected and observed and expected != observed:
        failures.append(f"{prefix}:authority_sequence_mismatch")

    if scenario_id in TRANSITION_SCENARIOS:
        if not expected or expected[0] != "blocked" or expected[-1] != "allowed":
            failures.append(f"{prefix}:transition_must_block_then_allow")
        _check_recovery(scenario, prefix, started, ended, failures)

    if scenario_id in BLOCKED_SCENARIOS:
        if not expected or any(item != "blocked" for item in expected):
            failures.append(f"{prefix}:invalid_state_must_remain_blocked")
        expected_safe = scenario.get("expected_safe_pv_request")
        observed_safe = scenario.get("observed_safe_pv_request")
        if not _finite_number(expected_safe):
            failures.append(f"{prefix}:expected_safe_pv_request_invalid")
        if not _finite_number(observed_safe):
            failures.append(f"{prefix}:observed_safe_pv_request_invalid")
        if _finite_number(expected_safe) and _finite_number(observed_safe):
            if not math.isclose(float(expected_safe), float(observed_safe), rel_tol=0.0, abs_tol=1e-9):
                failures.append(f"{prefix}:safe_pv_request_mismatch")

    if scenario_id in {"island", "synchronized"} and expected and expected[-1] != "allowed":
        failures.append(f"{prefix}:stable_carrying_mode_not_allowed")

    _check_meter(scenario.get("grid_meter"), f"{prefix}:grid", failures)
    _check_meter(scenario.get("generator_meter"), f"{prefix}:generator", failures)

    if scenario_id == "generator_meter_sign":
        proof = scenario.get("meter_sign_proof")
        if not isinstance(proof, dict):
            failures.append(f"{prefix}:meter_sign_proof_missing")
        else:
            if not _nonempty_text(proof.get("known_physical_direction"), 8):
                failures.append(f"{prefix}:known_physical_direction_missing")
            if not _nonempty_text(proof.get("independent_reference"), 4):
                failures.append(f"{prefix}:independent_reference_missing")
            if not _finite_number(proof.get("observed_generator_kw")):
                failures.append(f"{prefix}:observed_generator_kw_invalid")
            generator_meter = scenario.get("generator_meter")
            if isinstance(generator_meter, dict) and _finite_number(proof.get("observed_generator_kw")) and _finite_number(generator_meter.get("scaled_kw")):
                if not math.isclose(float(proof["observed_generator_kw"]), float(generator_meter["scaled_kw"]), rel_tol=1e-9, abs_tol=1e-9):
                    failures.append(f"{prefix}:generator_meter_sign_value_mismatch")
            if proof.get("sign_matches") is not True:
                failures.append(f"{prefix}:generator_meter_sign_not_proven")

    command_path = scenario.get("command_path")
    if not isinstance(command_path, dict):
        failures.append(f"{prefix}:command_path_missing")
    else:
        qualified = command_path.get("qualified_inverter_path")
        if not isinstance(qualified, bool):
            failures.append(f"{prefix}:qualified_inverter_path_missing")
        elif qualified:
            if not _finite_number(command_path.get("command")) or not _finite_number(command_path.get("readback")):
                failures.append(f"{prefix}:qualified_command_readback_invalid")
            if not _nonempty_text(command_path.get("evidence_ref"), 4):
                failures.append(f"{prefix}:command_evidence_ref_missing")
            if not _digest(command_path.get("evidence_digest")):
                failures.append(f"{prefix}:command_evidence_digest_invalid")
        else:
            if not _nonempty_text(command_path.get("safe_pv_observation"), 8):
                failures.append(f"{prefix}:safe_pv_observation_missing")
            if not _nonempty_text(command_path.get("evidence_ref"), 4):
                failures.append(f"{prefix}:safe_pv_evidence_ref_missing")
            if not _digest(command_path.get("evidence_digest")):
                failures.append(f"{prefix}:safe_pv_evidence_digest_invalid")

    fatal_counts = scenario.get("fatal_counts")
    if not isinstance(fatal_counts, dict):
        failures.append(f"{prefix}:fatal_counts_missing")
    else:
        for key in REQUIRED_FATAL_COUNTS:
            value = fatal_counts.get(key)
            if not isinstance(value, int) or isinstance(value, bool) or value != 0:
                failures.append(f"{prefix}:fatal_count_nonzero_or_missing:{key}")

    references = scenario.get("references")
    if not isinstance(references, dict):
        failures.append(f"{prefix}:references_missing")
    else:
        for key in ("serial_log_ref", "hmi_http_ref"):
            if not _nonempty_text(references.get(key), 4):
                failures.append(f"{prefix}:reference_missing:{key}")

    if not _nonempty_text(scenario.get("evidence_note"), 12):
        failures.append(f"{prefix}:evidence_note_missing")


def evaluate(
    record: dict[str, Any],
    expected_firmware_sha: str,
    expected_artifact_digest: str,
    expected_site_id: str,
    expected_config_identity: str,
    expected_topology_digest: str,
    expected_source_map_digest: str,
    expected_meter_map_digest: str,
) -> GeneratorPhysicalResult:
    failures: list[str] = []
    firmware_sha = str(record.get("firmware_sha", "")).strip().lower()
    artifact_digest = _normalized_digest(record.get("artifact_digest"))
    site_id = str(record.get("site_id", "")).strip()
    config_identity = str(record.get("config_identity", "")).strip()
    topology_digest = _normalized_digest(record.get("topology_digest"))
    source_map_digest = _normalized_digest(record.get("source_map_digest"))
    meter_map_digest = _normalized_digest(record.get("meter_map_digest"))

    identity = {
        "firmware_sha": firmware_sha,
        "artifact_digest": artifact_digest,
        "site_id": site_id,
        "config_identity": config_identity,
        "topology_digest": topology_digest,
        "source_map_digest": source_map_digest,
        "meter_map_digest": meter_map_digest,
    }

    if record.get("evidence_state") != EVIDENCE_STATE:
        failures.append("evidence_state_not_executed")
    if not _sha40(firmware_sha):
        failures.append("firmware_sha_invalid")
    if firmware_sha != str(expected_firmware_sha).strip().lower():
        failures.append("firmware_sha_mismatch")
    if not _digest(record.get("artifact_digest")):
        failures.append("artifact_digest_invalid")
    if artifact_digest != _normalized_digest(expected_artifact_digest):
        failures.append("artifact_digest_mismatch")
    if not _nonempty_text(site_id, 2):
        failures.append("site_id_missing")
    if site_id != str(expected_site_id).strip():
        failures.append("site_id_mismatch")
    if not _nonempty_text(config_identity, 4):
        failures.append("config_identity_missing")
    if config_identity != str(expected_config_identity).strip():
        failures.append("config_identity_mismatch")

    for field, actual, expected in (
        ("topology", record.get("topology_digest"), expected_topology_digest),
        ("source_map", record.get("source_map_digest"), expected_source_map_digest),
        ("meter_map", record.get("meter_map_digest"), expected_meter_map_digest),
    ):
        if not _digest(actual):
            failures.append(f"{field}_digest_invalid")
        if _normalized_digest(actual) != _normalized_digest(expected):
            failures.append(f"{field}_digest_mismatch")

    for key in ("source_map_ref", "meter_map_ref", "evidence_package_ref"):
        if not _nonempty_text(record.get(key), 4):
            failures.append(f"{key}_missing")
    if not _digest(record.get("evidence_package_digest")):
        failures.append("evidence_package_digest_invalid")

    topology = record.get("topology")
    if not isinstance(topology, dict):
        topology = {}
        failures.append("topology_missing")
    topology_ref = str(topology.get("topology_ref", "")).strip()
    if not _nonempty_text(topology_ref, 4):
        failures.append("topology_ref_missing")
    supports_sync = topology.get("supports_sync")
    if not isinstance(supports_sync, bool):
        failures.append("supports_sync_missing")
        supports_sync = False
    if topology.get("power_sign_used_as_source_authority") is not False:
        failures.append("power_sign_source_authority_not_forbidden")

    source_refs = record.get("source_signal_refs")
    if not isinstance(source_refs, list) or len(source_refs) < 2 or not all(_nonempty_text(item, 4) for item in source_refs):
        failures.append("source_signal_refs_incomplete")

    meter_refs = record.get("meter_refs")
    if not isinstance(meter_refs, list) or len(meter_refs) < 2 or not all(_nonempty_text(item, 4) for item in meter_refs):
        failures.append("meter_refs_incomplete")

    manual_refs = record.get("manual_wiring_refs")
    if not isinstance(manual_refs, list) or not manual_refs or not all(_nonempty_text(item, 4) for item in manual_refs):
        failures.append("manual_wiring_refs_incomplete")

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
        if scenario_id in seen:
            _check_scenario(seen[scenario_id], bool(supports_sync), topology_ref, identity, failures)

    return GeneratorPhysicalResult(
        passed=not failures,
        firmware_sha=firmware_sha,
        artifact_digest=artifact_digest,
        site_id=site_id,
        config_identity=config_identity,
        topology_digest=topology_digest,
        source_map_digest=source_map_digest,
        meter_map_digest=meter_map_digest,
        scenarios_seen=sorted(seen),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate generator/source-transition physical qualification evidence")
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-firmware-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--expected-site-id", required=True)
    parser.add_argument("--expected-config-identity", required=True)
    parser.add_argument("--expected-topology-digest", required=True)
    parser.add_argument("--expected-source-map-digest", required=True)
    parser.add_argument("--expected-meter-map-digest", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    if not _sha40(args.expected_firmware_sha):
        print("GENERATOR PHYSICAL EVIDENCE FAIL: expected firmware SHA invalid", file=sys.stderr)
        return 2
    for label, value in (
        ("artifact", args.expected_artifact_digest),
        ("topology", args.expected_topology_digest),
        ("source-map", args.expected_source_map_digest),
        ("meter-map", args.expected_meter_map_digest),
    ):
        if not _digest(value):
            print(f"GENERATOR PHYSICAL EVIDENCE FAIL: expected {label} digest invalid", file=sys.stderr)
            return 2
    if not _nonempty_text(args.expected_site_id, 2) or not _nonempty_text(args.expected_config_identity, 4):
        print("GENERATOR PHYSICAL EVIDENCE FAIL: expected site/config identity invalid", file=sys.stderr)
        return 2

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"GENERATOR PHYSICAL EVIDENCE FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("GENERATOR PHYSICAL EVIDENCE FAIL: JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        args.expected_firmware_sha,
        args.expected_artifact_digest,
        args.expected_site_id,
        args.expected_config_identity,
        args.expected_topology_digest,
        args.expected_source_map_digest,
        args.expected_meter_map_digest,
    )
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("GENERATOR PHYSICAL EVIDENCE PASS" if result.passed else "GENERATOR PHYSICAL EVIDENCE FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- firmware_sha={result.firmware_sha}")
        print(f"- site_id={result.site_id}")
        print(f"- scenarios_seen={','.join(result.scenarios_seen)}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
