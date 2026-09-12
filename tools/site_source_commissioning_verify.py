#!/usr/bin/env python3
"""Fail-closed validator for real-site Grid/Generator/ATS/source evidence.

The validator does not discover registers, infer source state from kW sign, or
operate field equipment. It accepts only an executed commissioning record bound
to an externally frozen firmware/artifact/site/config/SLD/channel-map identity
with authoritative provenance and physical before/after/stale/recovery evidence.
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

VALID_STATUS = {"pass", "unqualified", "not_supported"}
VALID_MAPPING = {"hardwired", "modbus"}
EVIDENCE_STATE = "EXECUTED_SITE_SOURCE_COMMISSIONING_EVIDENCE"
SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^(?:sha256:)?[0-9a-f]{64}$")


@dataclass
class SiteCommissioningResult:
    passed: bool
    site_id: str
    firmware_sha: str
    artifact_digest: str
    config_identity: str
    site_sld_digest: str
    channel_map_digest: str
    channels_seen: list[str]
    failures: list[str]


def _text(value: object, minimum: int = 1) -> bool:
    return len(str(value or "").strip()) >= minimum


def _number(value: object) -> bool:
    if isinstance(value, bool):
        return False
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def _time(value: object) -> datetime | None:
    try:
        parsed = datetime.fromisoformat(str(value or "").replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo is not None else None


def _sha40(value: object) -> bool:
    return bool(SHA40.fullmatch(str(value or "").strip().lower()))


def _digest(value: object) -> bool:
    return bool(SHA256.fullmatch(str(value or "").strip().lower()))


def _normalized_digest(value: object) -> str:
    raw = str(value or "").strip().lower()
    return raw if raw.startswith("sha256:") else f"sha256:{raw}"


def _check_mapping(channel: dict[str, Any], prefix: str, failures: list[str]) -> None:
    mapping = channel.get("mapping")
    if not isinstance(mapping, dict):
        failures.append(f"{prefix}:mapping_missing")
        return
    kind = str(mapping.get("kind", "")).strip()
    if kind not in VALID_MAPPING:
        failures.append(f"{prefix}:mapping_kind_invalid")
        return
    if kind == "hardwired":
        if not _text(mapping.get("terminal_wire_ref"), 3):
            failures.append(f"{prefix}:terminal_wire_ref_missing")
        if not _text(mapping.get("input_channel"), 2):
            failures.append(f"{prefix}:input_channel_missing")
    else:
        if not _text(mapping.get("endpoint"), 3):
            failures.append(f"{prefix}:endpoint_missing")
        unit_id = mapping.get("unit_id")
        if not isinstance(unit_id, int) or isinstance(unit_id, bool) or unit_id < 0 or unit_id > 247:
            failures.append(f"{prefix}:unit_id_invalid")
        if not isinstance(mapping.get("function_code"), int) or isinstance(mapping.get("function_code"), bool):
            failures.append(f"{prefix}:function_code_missing")
        if not _text(mapping.get("address"), 1):
            failures.append(f"{prefix}:address_missing")
        if not _text(mapping.get("bit_mask_or_value"), 1):
            failures.append(f"{prefix}:bit_mask_or_value_missing")

    active = mapping.get("active_raw")
    inactive = mapping.get("inactive_raw")
    if active is None or inactive is None:
        failures.append(f"{prefix}:active_inactive_raw_missing")
    elif active == inactive:
        failures.append(f"{prefix}:active_inactive_raw_equal")


def _check_meter(channel: dict[str, Any], prefix: str, failures: list[str]) -> None:
    meter = channel.get("meter")
    if not isinstance(meter, dict):
        failures.append(f"{prefix}:meter_record_missing")
        return
    applicable = meter.get("applicable")
    if not isinstance(applicable, bool):
        failures.append(f"{prefix}:meter_applicable_missing")
        return
    if not applicable:
        if not _text(meter.get("not_applicable_reason"), 8):
            failures.append(f"{prefix}:meter_not_applicable_reason_missing")
        return

    for key in (
        "meter_id",
        "role",
        "ct_pt_polarity_ref",
        "data_type",
        "word_order",
        "sign_convention",
        "independent_reference",
    ):
        if not _text(meter.get(key), 2):
            failures.append(f"{prefix}:meter_field_missing:{key}")
    if not _number(meter.get("scale")):
        failures.append(f"{prefix}:meter_scale_invalid")
    if not _number(meter.get("raw")):
        failures.append(f"{prefix}:meter_raw_invalid")
    if not _number(meter.get("scaled_kw")):
        failures.append(f"{prefix}:meter_scaled_kw_invalid")
    if _number(meter.get("raw")) and _number(meter.get("scale")) and _number(meter.get("scaled_kw")):
        calculated = float(meter["raw"]) * float(meter["scale"])
        if not math.isclose(calculated, float(meter["scaled_kw"]), rel_tol=1e-9, abs_tol=1e-9):
            failures.append(f"{prefix}:meter_raw_scale_mismatch")
    if not _text(meter.get("known_physical_direction"), 6):
        failures.append(f"{prefix}:meter_known_direction_missing")
    if meter.get("sign_proven") is not True:
        failures.append(f"{prefix}:meter_sign_not_proven")

    connection = meter.get("connection")
    if not isinstance(connection, dict) or not isinstance(connection.get("applicable"), bool):
        failures.append(f"{prefix}:meter_connection_applicability_missing")
    elif connection.get("applicable") is True:
        if not _text(connection.get("endpoint"), 3):
            failures.append(f"{prefix}:meter_endpoint_missing")
        unit_id = connection.get("unit_id")
        if not isinstance(unit_id, int) or isinstance(unit_id, bool) or unit_id < 0 or unit_id > 247:
            failures.append(f"{prefix}:meter_unit_id_invalid")
    elif not _text(connection.get("not_applicable_reason"), 8):
        failures.append(f"{prefix}:meter_connection_not_applicable_reason_missing")


def _check_binding(
    obj: dict[str, Any],
    prefix: str,
    site_id: str,
    config_identity: str,
    channel_map_digest: str,
    failures: list[str],
) -> None:
    if str(obj.get("site_id", "")).strip() != site_id:
        failures.append(f"{prefix}:site_id_mismatch")
    if str(obj.get("config_identity", "")).strip() != config_identity:
        failures.append(f"{prefix}:config_identity_mismatch")
    if _normalized_digest(obj.get("channel_map_digest")) != channel_map_digest:
        failures.append(f"{prefix}:channel_map_digest_mismatch")


def _check_commissioning(
    channel: dict[str, Any],
    prefix: str,
    site_id: str,
    config_identity: str,
    channel_map_digest: str,
    failures: list[str],
) -> None:
    proof = channel.get("commissioning")
    if not isinstance(proof, dict):
        failures.append(f"{prefix}:commissioning_missing")
        return

    _check_binding(proof, f"{prefix}:commissioning", site_id, config_identity, channel_map_digest, failures)

    started = _time(proof.get("started_at"))
    ended = _time(proof.get("ended_at"))
    if started is None or ended is None:
        failures.append(f"{prefix}:commissioning_timestamps_invalid")
    elif ended <= started:
        failures.append(f"{prefix}:commissioning_time_not_increasing")

    for key in (
        "physical_before",
        "physical_after",
        "runtime_before",
        "runtime_after",
        "toggle_evidence_ref",
        "hmi_api_observation_ref",
    ):
        if not _text(proof.get(key), 4):
            failures.append(f"{prefix}:commissioning_field_missing:{key}")
    if _text(proof.get("physical_before")) and _text(proof.get("physical_after")):
        if str(proof.get("physical_before")).strip() == str(proof.get("physical_after")).strip():
            failures.append(f"{prefix}:physical_toggle_no_state_change")
    if _text(proof.get("runtime_before")) and _text(proof.get("runtime_after")):
        if str(proof.get("runtime_before")).strip() == str(proof.get("runtime_after")).strip():
            failures.append(f"{prefix}:runtime_toggle_no_state_change")
    if "raw_before" not in proof or "raw_after" not in proof:
        failures.append(f"{prefix}:raw_before_after_missing")
    elif proof.get("raw_before") == proof.get("raw_after"):
        failures.append(f"{prefix}:physical_toggle_no_raw_change")
    if proof.get("semantic_match") is not True:
        failures.append(f"{prefix}:runtime_semantic_not_proven")
    if not _digest(proof.get("toggle_evidence_digest")):
        failures.append(f"{prefix}:toggle_evidence_digest_invalid")

    stale = proof.get("stale_test")
    if not isinstance(stale, dict):
        failures.append(f"{prefix}:stale_test_missing")
    else:
        if stale.get("performed") is not True:
            failures.append(f"{prefix}:stale_test_not_performed")
        if stale.get("fail_closed") is not True:
            failures.append(f"{prefix}:stale_test_not_fail_closed")
        if not _text(stale.get("observed_fail_closed_state"), 4):
            failures.append(f"{prefix}:stale_observed_fail_closed_state_missing")
        if not _text(stale.get("evidence_ref"), 4):
            failures.append(f"{prefix}:stale_evidence_ref_missing")
        if not _digest(stale.get("evidence_digest")):
            failures.append(f"{prefix}:stale_evidence_digest_invalid")

    recovery = proof.get("recovery_test")
    if not isinstance(recovery, dict):
        failures.append(f"{prefix}:recovery_test_missing")
    else:
        if recovery.get("performed") is not True:
            failures.append(f"{prefix}:recovery_test_not_performed")
        if recovery.get("authority_returned_early") is not False:
            failures.append(f"{prefix}:authority_returned_early_not_false")
        observed = recovery.get("observed_dwell_ms")
        if not _number(observed) or float(observed) < 0:
            failures.append(f"{prefix}:recovery_dwell_invalid")
        timing = channel.get("timing") if isinstance(channel.get("timing"), dict) else {}
        configured = timing.get("recovery_ms")
        if _number(observed) and _number(configured) and float(observed) < float(configured):
            failures.append(f"{prefix}:recovery_dwell_below_configured_authority")
        if not _text(recovery.get("evidence_ref"), 4):
            failures.append(f"{prefix}:recovery_evidence_ref_missing")
        if not _digest(recovery.get("evidence_digest")):
            failures.append(f"{prefix}:recovery_evidence_digest_invalid")

    persistence = proof.get("persistence")
    if not isinstance(persistence, dict):
        failures.append(f"{prefix}:persistence_missing")
    else:
        if persistence.get("written") is not True:
            failures.append(f"{prefix}:config_not_written")
        if persistence.get("readback_match") is not True:
            failures.append(f"{prefix}:config_readback_not_proven")
        if str(persistence.get("config_identity_readback", "")).strip() != config_identity:
            failures.append(f"{prefix}:config_identity_readback_mismatch")
        if _normalized_digest(persistence.get("channel_map_digest_readback")) != channel_map_digest:
            failures.append(f"{prefix}:channel_map_digest_readback_mismatch")
        if not _text(persistence.get("evidence_ref"), 4):
            failures.append(f"{prefix}:persistence_evidence_ref_missing")
        if not _digest(persistence.get("evidence_digest")):
            failures.append(f"{prefix}:persistence_evidence_digest_invalid")

    if proof.get("pass") is not True:
        failures.append(f"{prefix}:commissioning_pass_not_true")
    if not _text(proof.get("pass_reason"), 12):
        failures.append(f"{prefix}:pass_reason_missing")


def _check_channel(
    channel: dict[str, Any],
    site_id: str,
    config_identity: str,
    site_sld_ref: str,
    channel_map_digest: str,
    failures: list[str],
) -> str:
    channel_id = str(channel.get("id", "")).strip()
    prefix = f"channel:{channel_id or 'missing'}"
    if not channel_id:
        failures.append("channel_id_missing")
    if not _text(channel.get("semantic"), 3):
        failures.append(f"{prefix}:semantic_missing")
    if not isinstance(channel.get("required"), bool):
        failures.append(f"{prefix}:required_flag_missing")

    status = str(channel.get("qualification_status", "")).strip()
    if status not in VALID_STATUS:
        failures.append(f"{prefix}:qualification_status_invalid")
        return channel_id

    if status in {"pass", "not_supported"}:
        _check_binding(channel, prefix, site_id, config_identity, channel_map_digest, failures)

    if status == "not_supported":
        if channel.get("required") is True:
            failures.append(f"{prefix}:required_channel_not_supported")
        if not _text(channel.get("not_supported_reason"), 8):
            failures.append(f"{prefix}:not_supported_reason_missing")
        if not _text(channel.get("topology_ref"), 4):
            failures.append(f"{prefix}:topology_ref_missing")
        return channel_id

    if status != "pass":
        if channel.get("required") is True:
            failures.append(f"{prefix}:required_channel_unqualified")
        return channel_id

    provenance = channel.get("provenance")
    if not isinstance(provenance, dict):
        failures.append(f"{prefix}:provenance_missing")
    else:
        for key in ("signal_source", "manufacturer", "model", "manual_revision", "wiring_drawing_ref", "site_sld_ref"):
            if not _text(provenance.get(key), 3):
                failures.append(f"{prefix}:provenance_field_missing:{key}")
        if str(provenance.get("site_sld_ref", "")).strip() != site_sld_ref:
            failures.append(f"{prefix}:provenance_site_sld_ref_mismatch")

    _check_mapping(channel, prefix, failures)

    timing = channel.get("timing")
    if not isinstance(timing, dict):
        failures.append(f"{prefix}:timing_missing")
    else:
        for key in ("debounce_ms", "stale_ms", "recovery_ms"):
            if not _number(timing.get(key)) or float(timing.get(key, -1)) < 0:
                failures.append(f"{prefix}:timing_invalid:{key}")
        if not _text(timing.get("authority_ref"), 4):
            failures.append(f"{prefix}:timing_authority_ref_missing")

    if channel.get("power_sign_used_as_state_authority") is not False:
        failures.append(f"{prefix}:power_sign_source_authority_not_forbidden")

    _check_meter(channel, prefix, failures)
    _check_commissioning(channel, prefix, site_id, config_identity, channel_map_digest, failures)
    return channel_id


def evaluate(
    record: dict[str, Any],
    expected_firmware_sha: str,
    expected_artifact_digest: str,
    expected_site_id: str,
    expected_config_identity: str,
    expected_site_sld_digest: str,
    expected_channel_map_digest: str,
) -> SiteCommissioningResult:
    failures: list[str] = []
    site_id = str(record.get("site_id", "")).strip()
    firmware_sha = str(record.get("firmware_sha", "")).strip().lower()
    artifact_digest = _normalized_digest(record.get("artifact_digest"))
    config_identity = str(record.get("config_identity", "")).strip()
    site_sld_ref = str(record.get("site_sld_ref", "")).strip()
    site_sld_digest = _normalized_digest(record.get("site_sld_digest"))
    channel_map_digest = _normalized_digest(record.get("channel_map_digest"))

    if record.get("evidence_state") != EVIDENCE_STATE:
        failures.append("evidence_state_not_executed")
    if not _text(site_id, 2):
        failures.append("site_id_missing")
    if site_id != str(expected_site_id).strip():
        failures.append("site_id_mismatch")
    if not _sha40(firmware_sha):
        failures.append("firmware_sha_invalid")
    if firmware_sha != str(expected_firmware_sha).strip().lower():
        failures.append("firmware_sha_mismatch")
    if not _digest(record.get("artifact_digest")):
        failures.append("artifact_digest_invalid")
    if artifact_digest != _normalized_digest(expected_artifact_digest):
        failures.append("artifact_digest_mismatch")
    if not _text(config_identity, 4):
        failures.append("config_identity_missing")
    if config_identity != str(expected_config_identity).strip():
        failures.append("config_identity_mismatch")
    if not _text(site_sld_ref, 4):
        failures.append("site_sld_ref_missing")
    if not _digest(record.get("site_sld_digest")):
        failures.append("site_sld_digest_invalid")
    if site_sld_digest != _normalized_digest(expected_site_sld_digest):
        failures.append("site_sld_digest_mismatch")
    if not _text(record.get("channel_map_ref"), 4):
        failures.append("channel_map_ref_missing")
    if not _digest(record.get("channel_map_digest")):
        failures.append("channel_map_digest_invalid")
    if channel_map_digest != _normalized_digest(expected_channel_map_digest):
        failures.append("channel_map_digest_mismatch")
    if not _text(record.get("evidence_package_ref"), 4):
        failures.append("evidence_package_ref_missing")
    if not _digest(record.get("evidence_package_digest")):
        failures.append("evidence_package_digest_invalid")
    if record.get("power_sign_used_as_source_authority") is not False:
        failures.append("power_sign_source_authority_not_forbidden")
    if record.get("automatic_control_enabled_during_commissioning") is not False:
        failures.append("automatic_control_must_remain_disabled_during_commissioning")

    channels_raw = record.get("channels")
    channels = channels_raw if isinstance(channels_raw, list) else []
    if not channels:
        failures.append("channels_missing")
    seen: set[str] = set()
    for index, channel in enumerate(channels):
        if not isinstance(channel, dict):
            failures.append(f"channel_invalid:{index}")
            continue
        channel_id = _check_channel(channel, site_id, config_identity, site_sld_ref, channel_map_digest, failures)
        if channel_id:
            if channel_id in seen:
                failures.append(f"channel_duplicate:{channel_id}")
            seen.add(channel_id)

    required_ids = record.get("required_channel_ids")
    if not isinstance(required_ids, list) or not required_ids or not all(_text(item, 1) for item in required_ids):
        failures.append("required_channel_ids_missing")
        required_ids = []
    by_id = {str(ch.get("id", "")).strip(): ch for ch in channels if isinstance(ch, dict)}
    for required_id in required_ids:
        rid = str(required_id).strip()
        channel = by_id.get(rid)
        if channel is None:
            failures.append(f"required_channel_missing:{rid}")
        elif channel.get("required") is not True:
            failures.append(f"required_channel_flag_false:{rid}")
        elif channel.get("qualification_status") != "pass":
            failures.append(f"required_channel_not_pass:{rid}")

    config_gate = record.get("configuration_acceptance")
    if not isinstance(config_gate, dict):
        failures.append("configuration_acceptance_missing")
    else:
        for key in (
            "readback_exact_match",
            "unqualified_blocks_automatic_control",
            "stale_missing_conflict_fail_closed",
            "physical_toggle_maps_expected_mode",
            "contradictory_power_sign_cannot_override_contacts",
        ):
            if config_gate.get(key) is not True:
                failures.append(f"configuration_acceptance_not_proven:{key}")
        if str(config_gate.get("site_id", "")).strip() != site_id:
            failures.append("configuration_acceptance_site_id_mismatch")
        if str(config_gate.get("config_identity", "")).strip() != config_identity:
            failures.append("configuration_acceptance_config_identity_mismatch")
        if _normalized_digest(config_gate.get("channel_map_digest")) != channel_map_digest:
            failures.append("configuration_acceptance_channel_map_digest_mismatch")
        if not _text(config_gate.get("evidence_ref"), 4):
            failures.append("configuration_acceptance_evidence_ref_missing")
        if not _digest(config_gate.get("evidence_digest")):
            failures.append("configuration_acceptance_evidence_digest_invalid")

    return SiteCommissioningResult(
        passed=not failures,
        site_id=site_id,
        firmware_sha=firmware_sha,
        artifact_digest=artifact_digest,
        config_identity=config_identity,
        site_sld_digest=site_sld_digest,
        channel_map_digest=channel_map_digest,
        channels_seen=sorted(seen),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate real-site Grid/Generator/ATS/source commissioning evidence")
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-firmware-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--expected-site-id", required=True)
    parser.add_argument("--expected-config-identity", required=True)
    parser.add_argument("--expected-site-sld-digest", required=True)
    parser.add_argument("--expected-channel-map-digest", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    if not _sha40(args.expected_firmware_sha):
        print("SITE SOURCE COMMISSIONING FAIL: expected firmware SHA invalid", file=sys.stderr)
        return 2
    for label, value in (
        ("artifact digest", args.expected_artifact_digest),
        ("site SLD digest", args.expected_site_sld_digest),
        ("channel-map digest", args.expected_channel_map_digest),
    ):
        if not _digest(value):
            print(f"SITE SOURCE COMMISSIONING FAIL: expected {label} invalid", file=sys.stderr)
            return 2
    if not _text(args.expected_site_id, 2) or not _text(args.expected_config_identity, 4):
        print("SITE SOURCE COMMISSIONING FAIL: expected site/config identity invalid", file=sys.stderr)
        return 2

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"SITE SOURCE COMMISSIONING FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("SITE SOURCE COMMISSIONING FAIL: JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(
        record,
        args.expected_firmware_sha,
        args.expected_artifact_digest,
        args.expected_site_id,
        args.expected_config_identity,
        args.expected_site_sld_digest,
        args.expected_channel_map_digest,
    )
    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        print("SITE SOURCE COMMISSIONING PASS" if result.passed else "SITE SOURCE COMMISSIONING FAIL")
        for failure in result.failures:
            print(f"- {failure}")
        print(f"- site_id={result.site_id}")
        print(f"- firmware_sha={result.firmware_sha}")
        print(f"- channels_seen={','.join(result.channels_seen)}")
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
