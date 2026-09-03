#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

from generator_transition_physical_verify import REQUIRED_SCENARIOS, evaluate


SHA = "a1620789235d21b515f9f245f2329fab88b50558"
DIGEST = "sha256:test-artifact"


def meter(sign: str) -> dict:
    return {"raw": 123, "scaled_kw": 12.3, "fresh": True, "sign_provenance": sign}


def scenario(scenario_id: str) -> dict:
    blocked = scenario_id in {"stale", "conflict", "transfer_asserted", "source_loss"}
    transition = scenario_id in {"grid_to_generator", "generator_to_grid", "recovery_dwell"}
    expected = ["blocked"] if blocked else (["blocked", "allowed"] if transition else ["allowed"])
    result = {
        "id": scenario_id,
        "outcome": "pass",
        "started_at": "2026-09-03T12:00:00+00:00",
        "ended_at": "2026-09-03T12:05:00+00:00",
        "raw_source_evidence": {"grid_breaker": 1, "generator_run": 0, "transfer": 0},
        "detected_modes": ["grid_only"],
        "expected_authority_sequence": expected,
        "observed_authority_sequence": list(expected),
        "grid_meter": meter("known grid import direction"),
        "generator_meter": meter("known generator supply direction"),
        "command_path": {
            "qualified_inverter_path": False,
            "safe_pv_observation": "controller request observed fail-closed/safe",
        },
        "fatal_counts": {
            "wdt": 0,
            "panic": 0,
            "no_mem": 0,
            "unexpected_reset": 0,
            "resource_collapse": 0
        },
        "references": {"serial_log_ref": "serial.log", "hmi_http_ref": "capture.json"},
        "evidence_note": f"physical observation for {scenario_id} matched expected source authority"
    }
    if blocked:
        result["expected_safe_pv_request"] = 0
        result["observed_safe_pv_request"] = 0
    if transition:
        result["authority_returned_early"] = False
        result["recovery_dwell_ms"] = 5000
        result["recovery_dwell_evidence"] = "timestamps prove full uninterrupted dwell before authority"
    if scenario_id == "generator_meter_sign":
        result["meter_sign_proof"] = {
            "known_physical_direction": "generator supplying the known bench load",
            "independent_reference": "clamp-meter-capture-001",
            "observed_generator_kw": 12.3,
            "sign_matches": True
        }
    return result


def passing_record(supports_sync: bool = True) -> dict:
    scenarios = [scenario(item) for item in REQUIRED_SCENARIOS]
    if not supports_sync:
        sync = next(item for item in scenarios if item["id"] == "synchronized")
        sync.clear()
        sync.update({
            "id": "synchronized",
            "outcome": "not_supported",
            "not_supported_reason": "site SLD has mechanically interlocked sources with no sync path",
            "topology_ref": "SLD-001-revA"
        })
    return {
        "firmware_sha": SHA,
        "artifact_digest": DIGEST,
        "site_id": "bench-A",
        "config_identity": "config-sha256:abc",
        "topology": {
            "topology_ref": "SLD-001-revA",
            "supports_sync": supports_sync,
            "power_sign_used_as_source_authority": False
        },
        "source_signal_refs": ["breaker-drawing-1", "ats-drawing-2"],
        "meter_refs": ["grid-meter-manual", "generator-meter-manual"],
        "manual_wiring_refs": ["source-controller-manual-rev1", "site-wiring-drawing-revA"],
        "scenarios": scenarios
    }


class GeneratorPhysicalEvidenceTests(unittest.TestCase):
    def test_complete_supported_matrix_passes(self) -> None:
        self.assertTrue(evaluate(passing_record(True), SHA, DIGEST).passed)

    def test_sync_not_supported_requires_authoritative_topology(self) -> None:
        record = passing_record(False)
        self.assertTrue(evaluate(record, SHA, DIGEST).passed)
        sync = next(item for item in record["scenarios"] if item["id"] == "synchronized")
        sync["topology_ref"] = ""
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:synchronized:topology_ref_missing", result.failures)

    def test_wrong_identity_fails(self) -> None:
        result = evaluate(passing_record(), "wrong", DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("firmware_sha_mismatch", result.failures)

    def test_power_sign_cannot_be_source_authority(self) -> None:
        record = passing_record()
        record["topology"]["power_sign_used_as_source_authority"] = True
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("power_sign_source_authority_not_forbidden", result.failures)

    def test_missing_scenario_fails(self) -> None:
        record = passing_record()
        record["scenarios"] = [item for item in record["scenarios"] if item["id"] != "conflict"]
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario_missing:conflict", result.failures)

    def test_invalid_evidence_must_remain_blocked(self) -> None:
        record = passing_record()
        stale = next(item for item in record["scenarios"] if item["id"] == "stale")
        stale["expected_authority_sequence"] = ["allowed"]
        stale["observed_authority_sequence"] = ["allowed"]
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:stale:invalid_state_must_remain_blocked", result.failures)

    def test_arbitrary_authority_text_is_rejected(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["expected_authority_sequence"] = ["maybe"]
        island["observed_authority_sequence"] = ["maybe"]
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:island:expected_authority_sequence_invalid", result.failures)
        self.assertIn("scenario:island:observed_authority_sequence_invalid", result.failures)

    def test_transition_requires_block_then_allow_and_full_dwell(self) -> None:
        record = passing_record()
        transition = next(item for item in record["scenarios"] if item["id"] == "grid_to_generator")
        transition["expected_authority_sequence"] = ["allowed"]
        transition["observed_authority_sequence"] = ["allowed"]
        transition["authority_returned_early"] = True
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:grid_to_generator:transition_must_block_then_allow", result.failures)
        self.assertIn("scenario:grid_to_generator:authority_returned_early_not_false", result.failures)

    def test_nonzero_fatal_count_fails(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["fatal_counts"]["wdt"] = 1
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:island:fatal_count_nonzero_or_missing:wdt", result.failures)

    def test_safe_pv_mismatch_or_non_numeric_value_fails(self) -> None:
        record = passing_record()
        conflict = next(item for item in record["scenarios"] if item["id"] == "conflict")
        conflict["observed_safe_pv_request"] = 10
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:conflict:safe_pv_request_mismatch", result.failures)

        record = passing_record()
        conflict = next(item for item in record["scenarios"] if item["id"] == "conflict")
        conflict["expected_safe_pv_request"] = "safe"
        conflict["observed_safe_pv_request"] = "safe"
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:conflict:expected_safe_pv_request_invalid", result.failures)
        self.assertIn("scenario:conflict:observed_safe_pv_request_invalid", result.failures)

    def test_generator_meter_sign_requires_independent_proof(self) -> None:
        record = passing_record()
        sign_case = next(item for item in record["scenarios"] if item["id"] == "generator_meter_sign")
        sign_case["meter_sign_proof"]["sign_matches"] = False
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:generator_meter_sign:generator_meter_sign_not_proven", result.failures)

    def test_qualified_command_and_readback_must_be_numeric(self) -> None:
        record = passing_record()
        island = next(item for item in record["scenarios"] if item["id"] == "island")
        island["command_path"] = {
            "qualified_inverter_path": True,
            "command": None,
            "readback": None,
            "evidence_ref": "capture-1"
        }
        result = evaluate(record, SHA, DIGEST)
        self.assertFalse(result.passed)
        self.assertIn("scenario:island:qualified_command_readback_invalid", result.failures)


if __name__ == "__main__":
    unittest.main(verbosity=2)
