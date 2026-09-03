#!/usr/bin/env python3
"""Fail-closed validator for real-site Grid/Generator/ATS/source evidence.

The validator does not discover registers, infer source state from kW sign, or
operate field equipment. It accepts only a commissioning record made against
an exact firmware/artifact/config identity with authoritative site/manual
provenance and physical before/after/stale/recovery observations.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

VALID_STATUS = {"pass", "unqualified", "not_supported"}
VALID_MAPPING = {"hardwired", "modbus"}


@dataclass
class SiteCommissioningResult:
    passed: bool
    site_id: str
    firmware_sha: str
    artifact_digest: str
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
        if not isinstance(mapping.get("unit_id"), int) or isinstance(mapping.get("unit_id"), bool):
            failures.append(f"{prefix}:unit_id_missing")
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

    for key in ("meter_id", "role", "ct_pt_polarity_ref", "data_type", "word_order", "sign_convention", "independent_reference"):
        if not _text(meter.get(key), 2):
            failures.append(f"{prefix}:meter_field_missing:{key}")
    if not _number(meter.get("scale")):
        failures.append(f"{prefix}:meter_scale_invalid")
    if "raw" not in meter:
        failures.append(f"{prefix}:meter_raw_missing")
    if not _number(meter.get("scaled_kw")):
        failures.append(f"{prefix}:meter_scaled_kw_invalid")
    if not _text(meter.get("known_physical_direction"), 6):
        failures.append(f"{prefix}:meter_known_direction_missing")
    if meter.get("sign_proven") is not True:
        failures.append(f"{prefix}:meter_sign_not_proven")


def _check_commissioning(channel: dict[str, Any], prefix: str, failures: list[str]) -> None:
    proof = channel.get("commissioning")
    if not isinstance(proof, dict):
        failures.append(f"{prefix}:commissioning_missing")
        return

    started = _time(proof.get("started_at"))
    ended = _time(proof.get("ended_at"))
    if started is None or ended is None:
        failures.append(f"{prefix}:commissioning_timestamps_invalid")
    elif ended <= started:
        failures.append(f"{prefix}:commissioning_time_not_increasing")

    for key in ("physical_before", "physical_after", "runtime_before", "runtime_after", "toggle_evidence_ref", "hmi_api_observation_ref"):
        if not _text(proof.get(key), 4):
            failures.append(f"{prefix}:commissioning_field_missing:{key}")
    if "raw_before" not in proof or "raw_after" not in proof:
        failures.append(f"{prefix}:raw_before_after_missing")
    elif proof.get("raw_before") == proof.get("raw_after"):
        failures.append(f"{prefix}:physical_toggle_no_raw_change")
    if proof.get("semantic_match") is not True:
        failures.append(f"{prefix}:runtime_semantic_not_proven")

    stale = proof.get("stale_test")
    if not isinstance(stale, dict):
        failures.append(f"{prefix}:stale_test_missing")
    else:
        if stale.get("performed") is not True:
            failures.append(f"{prefix}:stale_test_not_performed")
        if stale.get("fail_closed") is not True:
            failures.append(f"{prefix}:stale_test_not_fail_closed")
        if not _text(stale.get("evidence_ref"), 4):
            failures.append(f"{prefix}:stale_evidence_ref_missing")

    recovery = proof.get("recovery_test")
    if not isinstance(recovery, dict):
        failures.append(f"{prefix}:recovery_test_missing")
    else:
        if recovery.get("performed") is not True:
            failures.append(f"{prefix}:recovery_test_not_performed")
        if recovery.get("authority_returned_early") is not False:
            failures.append(f"{prefix}:authority_returned_early_not_false")
        if not _number(recovery.get("observed_dwell_ms")) or float(recovery.get("observed_dwell_ms", 0)) < 0:
            failures.append(f"{prefix}:recovery_dwell_invalid")
        if not _text(recovery.get("evidence_ref"), 4):
            failures.append(f"{prefix}:recovery_evidence_ref_missing")

    persistence = proof.get("persistence")
    if not isinstance(persistence, dict):
        failures.append(f"{prefix}:persistence_missing")
    else:
        if persistence.get("written") is not True:
            failures.append(f"{prefix}:config_not_written")
        if persistence.get("readback_match") is not True:
            failures.append(f"{prefix}:config_readback_not_proven")
        if not _text(persistence.get("evidence_ref"), 4):
            failures.append(f"{prefix}:persistence_evidence_ref_missing")

    if proof.get("pass") is not True:
        failures.append(f"{prefix}:commissioning_pass_not_true")
    if not _text(proof.get("pass_reason"), 12):
        failures.append(f"{prefix}:pass_reason_missing")


def _check_channel(channel: dict[str, Any], failures: list[str]) -> str:
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
    _check_commissioning(channel, prefix, failures)
    return channel_id


def evaluate(record: dict[str, Any], expected_firmware_sha: str, expected_artifact_digest: str) -> SiteCommissioningResult:
    failures: list[str] = []
    site_id = str(record.get("site_id", "")).strip()
    firmware_sha = str(record.get("firmware_sha", "")).strip()
    artifact_digest = str(record.get("artifact_digest", "")).strip().lower()

    if not _text(site_id, 2):
        failures.append("site_id_missing")
    if firmware_sha != expected_firmware_sha:
        failures.append("firmware_sha_mismatch")
    if artifact_digest != expected_artifact_digest.strip().lower():
        failures.append("artifact_digest_mismatch")
    if not _text(record.get("config_identity"), 4):
        failures.append("config_identity_missing")
    if not _text(record.get("site_sld_ref"), 4):
        failures.append("site_sld_ref_missing")
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
        channel_id = _check_channel(channel, failures)
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
        if not _text(config_gate.get("evidence_ref"), 4):
            failures.append("configuration_acceptance_evidence_ref_missing")

    return SiteCommissioningResult(
        passed=not failures,
        site_id=site_id,
        firmware_sha=firmware_sha,
        artifact_digest=artifact_digest,
        channels_seen=sorted(seen),
        failures=failures,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate real-site Grid/Generator/ATS/source commissioning evidence")
    parser.add_argument("evidence_json", type=Path)
    parser.add_argument("--expected-firmware-sha", required=True)
    parser.add_argument("--expected-artifact-digest", required=True)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        record = json.loads(args.evidence_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"SITE SOURCE COMMISSIONING FAIL: unreadable JSON: {exc}", file=sys.stderr)
        return 2
    if not isinstance(record, dict):
        print("SITE SOURCE COMMISSIONING FAIL: JSON must be an object", file=sys.stderr)
        return 2

    result = evaluate(record, args.expected_firmware_sha, args.expected_artifact_digest)
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
